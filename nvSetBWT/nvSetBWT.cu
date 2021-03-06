/*
 * nvbio
 * Copyright (C) 2011-2014, NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// nvSetBWT.cu
//

//#define NVBIO_CUDA_DEBUG

#include <cub/cub.cuh>
#include <nvbio/basic/omp.h>

#include <nvbio/sufsort/sufsort.h>
#include <nvbio/sufsort/sufsort_utils.h>
#include <nvbio/sufsort/file_bwt.h>
#include <nvbio/basic/timer.h>
#include <nvbio/basic/shared_pointer.h>
#include <nvbio/basic/exceptions.h>
#include <nvbio/basic/dna.h>
#include <nvbio/basic/vector.h>
#include <nvbio/strings/string_set.h>
#include <nvbio/io/sequence/sequence.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>

using namespace nvbio;

/// Our in-memory reads container
///
struct Reads
{
    typedef uint32 word_type;

    static const uint32 WORD_SIZE        = 32;
    static const uint32 SYMBOL_SIZE      = 2;
    static const uint32 SYMBOLS_PER_WORD = 16;

    typedef PackedStream<word_type*,uint8,SYMBOL_SIZE,true,uint64>  packed_stream_type;

    Reads() : n_reads(0), n_symbols(0), min_len(uint32(-1)), max_len(0), reserved_words(0u), h_read_storage( NULL ) {}
    ~Reads() { free( h_read_storage ); }

    uint32                      n_reads;            // number of reads
    uint64                      n_symbols;          // number of symbols
    uint32                      min_len;            // minimum read length
    uint32                      max_len;            // maximum read length
    uint64                      reserved_words;     // allocated words
    word_type*                  h_read_storage;     // read storage
    thrust::host_vector<uint64> h_read_index;       // read index
};

bool read(const char* reads_name, const io::QualityEncoding qencoding, const io::SequenceEncoding flags, Reads* reads)
{
    typedef Reads::word_type word_type;
    const uint32 WORD_SIZE        = Reads::WORD_SIZE;
    const uint32 SYMBOL_SIZE      = Reads::SYMBOL_SIZE;
    const uint32 SYMBOLS_PER_WORD = Reads::SYMBOLS_PER_WORD;

    log_visible(stderr, "opening read file \"%s\"\n", reads_name);
    SharedPointer<nvbio::io::SequenceDataStream> read_data_file(
        nvbio::io::open_sequence_file(
            reads_name,
            qencoding,
            uint32(-1),
            uint32(-1),
            flags )
    );

    if (read_data_file == NULL || read_data_file->is_ok() == false)
    {
        log_error(stderr, "    failed opening file \"%s\"\n", reads_name);
        return false;
    }

    const uint32 batch_size = 512*1024;
    const uint32 batch_bps  = batch_size * 100;

    float io_time = 0.0f;

    io::SequenceDataHost h_read_data;

    while (1)
    {
        nvbio::Timer timer;
        timer.start();

        // load a new batch of reads
        if (io::next( DNA, &h_read_data, read_data_file.get(), batch_size, batch_bps ) == 0)
            break;

        // build a view
        const io::SequenceDataAccess<DNA> h_read_view( h_read_data );

        const uint64 required_words = util::divide_ri( reads->n_symbols + h_read_data.bps(), SYMBOLS_PER_WORD );

        // realloc the storage
        if (reads->reserved_words <= required_words)
        {
            reads->reserved_words = nvbio::max( required_words, (reads->reserved_words * 5)/4 );

            reads->h_read_storage = (word_type*)realloc( reads->h_read_storage, reads->reserved_words * sizeof(word_type) );
            if (reads->h_read_storage == NULL)
                throw bad_alloc( "unable to allocate %.1fGB of read storage", float(reads->reserved_words * sizeof(word_type))/float(1024*1024*1024) );
        }

        // pack the first few symbols to fill the last word
        const uint32 word_offset = reads->n_symbols & (SYMBOLS_PER_WORD-1);
              uint32 word_rem    = 0;

        typedef io::SequenceDataAccess<DNA>::sequence_stream_type src_read_stream_type;
        const src_read_stream_type src( h_read_view.sequence_stream() );

        if (word_offset)
        {
            const uint64 word_idx = reads->n_symbols / SYMBOLS_PER_WORD;

            // compute how many symbols we still need to encode to fill the current word
            word_rem = SYMBOLS_PER_WORD - word_offset;

            // fetch the word in question
            word_type word = reads->h_read_storage[ word_idx ];

            for (uint32 i = 0; i < word_rem; ++i)
            {
                const uint32       bit_idx = (word_offset + i) * SYMBOL_SIZE;
                const uint32 symbol_offset = (WORD_SIZE - SYMBOL_SIZE - bit_idx);
                const word_type     symbol = word_type(src[i]) << symbol_offset;

                // set bits
                word |= symbol;
            }

            // write out the word
            reads->h_read_storage[ word_idx ] = word;
        }

        #pragma omp parallel for
        for (int i = word_rem; i < int( h_read_data.bps() ); i += SYMBOLS_PER_WORD)
        {
            // encode a word's worth of characters
            word_type word = 0u;

            const uint32 n_symbols = nvbio::min( SYMBOLS_PER_WORD, h_read_data.bps() - i );

            for (uint32 j = 0; j < n_symbols; ++j)
            {
                const uint32       bit_idx = j * SYMBOL_SIZE;
                const uint32 symbol_offset = (WORD_SIZE - SYMBOL_SIZE - bit_idx);
                const word_type     symbol = word_type(src[i + j]) << symbol_offset;

                // set bits
                word |= symbol;
            }

            // write out the given word
            const uint64 word_idx = (reads->n_symbols + i) / SYMBOLS_PER_WORD;

            reads->h_read_storage[ word_idx ] = word;
        }

        // update the read index
        reads->h_read_index.resize( reads->n_reads + h_read_data.size() + 1u );

        const uint32* src_index = h_read_view.sequence_index();
        for (uint32 i = 0; i < h_read_data.size(); ++i)
            reads->h_read_index[ reads->n_reads + i ] = reads->n_symbols + src_index[i];

        // advance the destination pointer
        reads->n_symbols += h_read_data.bps();
        reads->n_reads   += h_read_data.size();
        reads->min_len = nvbio::min( reads->min_len, h_read_data.min_sequence_len() );
        reads->max_len = nvbio::max( reads->max_len, h_read_data.max_sequence_len() );

        timer.stop();
        io_time += timer.seconds();

        log_verbose(stderr,"\r    %u reads, %llu symbols read (%.1fs - %.1fGB)    ",
            reads->n_reads,
            reads->n_symbols,
            io_time,
            float(reads->reserved_words*sizeof(word_type))/float(1024*1024*1024));
    }
    log_verbose_cont(stderr,"\n");
    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        log_visible(stderr, "nvSetBWT - Copyright 2013-2014, NVIDIA Corporation\n");
        log_info(stderr, "usage:\n");
        log_info(stderr, "  nvSetBWT [options] input_file output_file\n");
        log_info(stderr, "  options:\n");
        log_info(stderr, "   -v       | --verbosity     int (0-6) [5]\n");
        log_info(stderr, "   -cpu-mem | --cpu-memory    int (MB)  [8192]\n");
        log_info(stderr, "   -gpu-mem | --gpu-memory    int (MB)  [2048]\n");
        log_info(stderr, "   -c       | --compression   string    [1R]   (e.g. \"1\", ..., \"9\", \"1R\")\n");
        log_info(stderr, "   -t       | --threads       int       [auto]\n");
        log_info(stderr, "   -b       | --bucketing     int       [16]   (# of bits used for bucketing)\n");
        log_info(stderr, "   -F       | --skip-forward\n");
        log_info(stderr, "   -R       | --skip-reverse\n");
        log_info(stderr, "  output formats:\n");
        log_info(stderr, "    .txt      ASCII\n");
        log_info(stderr, "    .txt.gz   ASCII, gzip compressed\n");
        log_info(stderr, "    .txt.bgz  ASCII, block-gzip compressed\n");
        log_info(stderr, "    .bwt      2-bit packed binary\n");
        log_info(stderr, "    .bwt.gz   2-bit packed binary, gzip compressed\n");
        log_info(stderr, "    .bwt.bgz  2-bit packed binary, block-gzip compressed\n");
        log_info(stderr, "    .bwt4     4-bit packed binary\n");
        log_info(stderr, "    .bwt4.gz  4-bit packed binary, gzip compressed\n");
        log_info(stderr, "    .bwt4.bgz 4-bit packed binary, block-gzip compressed\n");
        return 0;
    }

    typedef Reads::word_type word_type;
    NVBIO_VAR_UNUSED static const uint32 SYMBOL_SIZE      = Reads::SYMBOL_SIZE;
    NVBIO_VAR_UNUSED static const uint32 SYMBOLS_PER_WORD = Reads::SYMBOLS_PER_WORD;

    const char* reads_name        = argv[argc-2];
    const char* output_name       = argv[argc-1];
    bool  forward                 = true;
    bool  reverse                 = true;
    const char* comp_level        = "1R";
    io::QualityEncoding qencoding = io::Phred33;
    int   threads                 = 0;

    BWTParams params;

    for (int i = 0; i < argc - 2; ++i)
    {
        if ((strcmp( argv[i], "-cpu-mem" )            == 0) ||
            (strcmp( argv[i], "--cpu-mem" )           == 0) ||
            (strcmp( argv[i], "--cpu-memory" )        == 0))
        {
            params.host_memory = atoi( argv[++i] ) * uint64(1024u*1024u);
        }
        else if ((strcmp( argv[i], "-gpu-mem" )       == 0) ||
                 (strcmp( argv[i], "--gpu-mem" )      == 0) ||
                 (strcmp( argv[i], "--gpu-memory" )   == 0))
        {
            params.device_memory = atoi( argv[++i] ) * uint64(1024u*1024u);
        }
        else if ((strcmp( argv[i], "-v" )             == 0) ||
                 (strcmp( argv[i], "-verbosity" )     == 0) ||
                 (strcmp( argv[i], "--verbosity" )    == 0))
        {
            set_verbosity( Verbosity( atoi( argv[++i] ) ) );
        }
        else if ((strcmp( argv[i], "-F" )             == 0) ||
                 (strcmp( argv[i], "--skip-forward" ) == 0))  // skip forward strand
        {
            forward = false;
        }
        else if ((strcmp( argv[i], "-R" )             == 0) ||
                 (strcmp( argv[i], "--skip-reverse" ) == 0))  // skip reverse strand
        {
            reverse = false;
        }
        else if ((strcmp( argv[i], "-c" )             == 0) ||
                 (strcmp( argv[i], "--compression" )  == 0))  // setup compression level
        {
            comp_level = argv[++i];
        }
        else if ((strcmp( argv[i], "-t" )             == 0) ||
                 (strcmp( argv[i], "--threads" )      == 0))  // setup number of threads
        {
            threads = atoi( argv[++i] );
        }
        else if ((strcmp( argv[i], "-s" )             == 0) ||
                 (strcmp( argv[i], "--radix-slice" )  == 0))  // radix slice
        {
            params.radix_slice = atoi( argv[++i] );
        }
        else if ((strcmp( argv[i], "-b" )             == 0) ||
                 (strcmp( argv[i], "--bucketing" )    == 0))  // bucketing bits
        {
            params.bucketing_bits = atoi( argv[++i] );
        }
    }

    try
    {
        log_visible(stderr,"nvSetBWT... started\n");

        // build an output file
        SharedPointer<BaseBWTHandler> output_handler = SharedPointer<BaseBWTHandler>( open_bwt_file( output_name, comp_level ) );
        if (output_handler == NULL)
        {
            log_error(stderr, "  failed to create an output handler\n");
            return 1;
        }

        // gather device memory stats
        size_t free_device, total_device;
        cudaMemGetInfo(&free_device, &total_device);
        log_stats(stderr, "  device has %ld of %ld MB free\n", free_device/1024/1024, total_device/1024/1024);

    #ifdef _OPENMP
        // now set the number of CPU threads
        omp_set_num_threads( threads > 0 ? threads : omp_get_num_procs() );
        #pragma omp parallel
        {
            log_verbose(stderr, "  running on multiple threads (%d)\n", omp_get_thread_num());
        }
    #endif

        Reads reads;

        log_info(stderr,"  reading input... started\n");

        uint32       encoding_flags  = 0u;
        if (forward) encoding_flags |= io::FORWARD;
        if (reverse) encoding_flags |= io::REVERSE_COMPLEMENT;

        if (read( reads_name, qencoding, io::SequenceEncoding(encoding_flags), &reads ) == false)
            return 1;

        // push sentinel value
        reads.h_read_index[ reads.n_reads ] = reads.n_symbols;

        log_info(stderr,"  reading input... done\n");

        const uint64 input_words = util::divide_ri( reads.n_symbols, SYMBOLS_PER_WORD );
        const uint64 input_size  = input_words * sizeof(word_type);
        log_stats(stderr,"    reads   : %u (min len: %u, max len: %u)\n", reads.n_reads, reads.min_len, reads.max_len);
        log_stats(stderr,"    symbols : %llu\n", reads.n_symbols);
        log_stats(stderr,"    size    : %llu MB\n", input_size / uint64(1024*1024));

        typedef Reads::packed_stream_type                               packed_stream_type;
        typedef ConcatenatedStringSet<packed_stream_type,uint64*>       string_set;

        // start the real work
        log_info(stderr, "  bwt... started\n");

        nvbio::Timer timer;
        timer.start();

        if (input_size + params.device_memory < free_device)
        {
            log_verbose(stderr, "  using fast path\n");

            thrust::device_vector<word_type> d_read_storage( input_words );
            thrust::device_vector<uint64>    d_read_index( reads.h_read_index );

            thrust::copy( reads.h_read_storage, reads.h_read_storage + input_words, d_read_storage.begin() );

            const packed_stream_type d_packed_string( (word_type*)nvbio::plain_view( d_read_storage ) );

            const string_set d_string_set(
                reads.n_reads,
                d_packed_string,
                nvbio::plain_view( d_read_index ) );

            cuda::bwt<SYMBOL_SIZE,true>(
                d_string_set,
                *output_handler,
                &params );
        }
        else
        {
            log_verbose(stderr, "  using hybrid path\n");

            const packed_stream_type h_packed_string( reads.h_read_storage );

            const string_set h_string_set(
                reads.n_reads,
                h_packed_string,
                nvbio::plain_view( reads.h_read_index ) );

            large_bwt<SYMBOL_SIZE,true>(
                h_string_set,
                *output_handler,
                &params );
        }

        timer.stop();

        //if (output_handler->n_dollars != reads.n_reads)
        //    log_warning(stderr, "    expected %u dollars, found %u\n", reads.n_reads, output_handler->n_dollars );

        log_info(stderr, "  bwt... done: %.2fs\n", timer.seconds());

        log_visible(stderr,"nvSetBWT... done\n");
    }
    catch (nvbio::cuda_error e)
    {
        log_error(stderr, "caught a nvbio::cuda_error exception:\n");
        log_error(stderr, "  %s\n", e.what());
        return 1;
    }
    catch (nvbio::bad_alloc e)
    {
        log_error(stderr, "caught a nvbio::bad_alloc exception:\n");
        log_error(stderr, "  %s\n", e.what());
        return 1;
    }
    catch (nvbio::logic_error e)
    {
        log_error(stderr, "caught a nvbio::logic_error exception:\n");
        log_error(stderr, "  %s\n", e.what());
        return 1;
    }
    catch (nvbio::runtime_error e)
    {
        log_error(stderr, "caught a nvbio::runtime_error exception:\n");
        log_error(stderr, "  %s\n", e.what());
        return 1;
    }
    catch (std::bad_alloc e)
    {
        log_error(stderr, "caught a std::bad_alloc exception:\n");
        log_error(stderr, "  %s\n", e.what());
        return 1;
    }
    catch (std::logic_error e)
    {
        log_error(stderr, "caught a std::logic_error exception:\n");
        log_error(stderr, "  %s\n", e.what());
        return 1;
    }
    catch (std::runtime_error e)
    {
        log_error(stderr, "caught a std::runtime_error exception:\n");
        log_error(stderr, "  %s\n", e.what());
        return 1;
    }
    catch (...)
    {
        log_error(stderr, "caught an unknown exception!\n");
        return 1;
    }
    return 0;
}
