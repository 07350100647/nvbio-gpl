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

// nvExtractReads.cu
//

#include <nvbio/basic/timer.h>
#include <nvbio/basic/shared_pointer.h>
#include <nvbio/io/sequence/sequence.h>
#include <nvbio/basic/dna.h>
#include <thrust/host_vector.h>
#include <thrust/device_vector.h>
#include <zlib/zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>

using namespace nvbio;

bool to_ascii(const char* reads_name, void* output_file, void* output_index, const io::QualityEncoding qencoding, const io::SequenceEncoding flags)
{
    log_visible(stderr, "opening read file \"%s\"\n", reads_name);
    SharedPointer<nvbio::io::SequenceDataStream> read_data_file(
        nvbio::io::open_sequence_file(reads_name,
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

    std::vector<char>   char_read( 1024*1024 );
    std::vector<uint64> index( 512*1024 + 1u );

    uint64 offset = 0u;

    uint32 n_reads = 0;

    io::SequenceDataHost h_read_data;

    // loop through all read batches
    while (1)
    {
        // load a new batch of reads
        if (io::next( DNA_N, &h_read_data, read_data_file.get(), batch_size ) == 0)
            break;

        const io::SequenceDataAccess<DNA_N> h_read_access( h_read_data );

        // loop through all reads
        for (uint32 i = 0; i < h_read_data.size(); ++i)
        {
            const io::SequenceDataAccess<DNA_N>::sequence_string read = h_read_access.get_read(i);

            dna_to_string( read, read.length(), &char_read[0] );

            char_read[ read.length() ] = '\n';

            const uint32 n_written = (uint32)gzwrite( output_file, &char_read[0], sizeof(char) * (read.length()+1) );
            if (n_written < read.length()+1)
            {
                log_error( stderr, "unable to write to output\n");
                return false;
            }
        }

        if (output_index)
        {
            // collect the sequence offsets
            for (uint32 i = 0; i < h_read_data.size(); ++i)
                index[i] = offset + h_read_data.sequence_index()[i+1];

            // write the sequence offsets
            gzwrite( output_file, &index[0], sizeof(uint64) * h_read_data.size() );
        }

        // update the global sequence offset
        offset += h_read_data.bps();

        // update the global number of output reads
        n_reads += h_read_data.size();

        const uint64 n_bytes = gzoffset( output_file );
        log_verbose(stderr,"\r    %u reads (%.2fGB - %.2fB/read - %.2fB/bp)    ", n_reads, float( n_bytes ) / float(1024*1024*1024), float(n_bytes)/float(n_reads), float(n_bytes)/float(offset));
    }
    log_verbose_cont(stderr,"\n");
    return true;
}

template <uint32 SYMBOL_SIZE>
bool to_packed(const char* reads_name, void* output_file, void* output_index, const io::QualityEncoding qencoding, const io::SequenceEncoding flags)
{
    log_visible(stderr, "opening read file \"%s\"\n", reads_name);
    SharedPointer<nvbio::io::SequenceDataStream> read_data_file(
        nvbio::io::open_sequence_file(reads_name,
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

    static const uint32 SYMBOLS_PER_WORD = 32u / SYMBOL_SIZE;

    const uint32 batch_size = 512*1024;

    uint32 n_reads = 0;

    io::SequenceDataHost h_read_data;

    typedef PackedStream<uint32*,uint8,SYMBOL_SIZE,true> packed_stream_type;

    std::vector<uint32> words( 1024*1024 );
    std::vector<uint64> index( 512*1024 + 1u );

    uint32 rem    = 0u;
    uint64 offset = 0u;

    // loop through all read batches
    while (1)
    {
        // load a new batch of reads
        if (io::next( DNA_N, &h_read_data, read_data_file.get(), batch_size ) == 0)
            break;

        // reserve enough storage
        words.resize( h_read_data.words() + 1u );

        packed_stream_type packed_reads( &words[0] );

        const io::SequenceDataAccess<DNA_N> h_read_access( h_read_data );

        nvbio::assign( h_read_access.bps(), h_read_access.sequence_stream(), packed_reads + rem );

        // write all whole words
        const uint32 n_bps       = h_read_access.bps() + rem;
        const uint32 whole_words = n_bps / SYMBOLS_PER_WORD;

        gzwrite( output_file, &words[0], sizeof(uint32) * whole_words );

        // save the last non-whole word
        words[0] = words[ whole_words ];

        // save the number of unwritten symbols left
        rem = n_bps & (SYMBOLS_PER_WORD-1);

        if (output_index)
        {
            // collect the sequence offsets
            for (uint32 i = 0; i < h_read_data.size(); ++i)
                index[i] = offset + h_read_data.sequence_index()[i+1];

            // write the sequence offsets
            gzwrite( output_file, &index[0], sizeof(uint64) * h_read_data.size() );
        }

        // update the global sequence offset
        offset += h_read_data.bps();

        // update the global number of output reads
        n_reads += h_read_data.size();

        const uint64 n_bytes = gzoffset( output_file );
        log_verbose(stderr,"\r    %u reads (%.2fGB - %.2fB/read - %.2fB/bp)    ", n_reads, float( n_bytes ) / float(1024*1024*1024), float(n_bytes)/float(n_reads), float(n_bytes)/float(offset));
    }
    log_verbose_cont(stderr,"\n");
    return true;
}

enum Format
{
    ASCII   = 0u,
    PACKED2 = 1u,
    PACKED4 = 2u,
};

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        log_info(stderr, "nvExtractReads [options] input output\n");
        log_info(stderr, "  extract a set of reads to a plain ASCII or packed file with one read per line (.txt)\n\n");
        log_info(stderr, "options:\n");
        log_info(stderr, "  --verbosity\n");
        log_info(stderr, "  -F | --skip-forward          skip forward strand\n");
        log_info(stderr, "  -R | --skip-reverse          skip forward strand\n");
        log_info(stderr, "  -a | --ascii                 ASCII output\n");
        log_info(stderr, "  -p2 | --packed-2             2-bits packed output\n");
        log_info(stderr, "  -p4 | --packed-4             4-bits packed output\n");
        log_info(stderr, "  -i  | --idx string           save an index file\n");
        exit(0);
    }

    const char* reads_name  = argv[argc-2];
    const char* out_name    = argv[argc-1];
    const char* idx_name    = NULL;
    bool  forward           = true;
    bool  reverse           = true;
    Format format           = ASCII;
    io::QualityEncoding qencoding = io::Phred33;

    for (int i = 0; i < argc - 2; ++i)
    {
        if (strcmp( argv[i], "-verbosity" ) == 0 ||
                 strcmp( argv[i], "--verbosity" ) == 0)
        {
            set_verbosity( Verbosity( atoi( argv[++i] ) ) );
        }
        else if (strcmp( argv[i], "-F" )             == 0 ||
                 strcmp( argv[i], "--skip-forward" ) == 0)  // skip forward strand
        {
            forward = false;
        }
        else if (strcmp( argv[i], "-R" ) == 0 ||
                 strcmp( argv[i], "--skip-reverse" ) == 0)  // skip reverse strand
        {
            reverse = false;
        }
        else if (strcmp( argv[i], "-a" ) == 0 ||
                 strcmp( argv[i], "--ascii" ) == 0)  // ascii format
        {
            format = ASCII;
        }
        else if (strcmp( argv[i], "-p2" ) == 0 ||
                 strcmp( argv[i], "--packed-2" ) == 0)  // 2-bits packed
        {
            format = PACKED2;
        }
        else if (strcmp( argv[i], "-p4" ) == 0 ||
                 strcmp( argv[i], "--packed-4" ) == 0)  // 4-bits packed
        {
            format = PACKED4;
        }
        else if (strcmp( argv[i], "-i" ) == 0 ||
                 strcmp( argv[i], "--idx" ) == 0)       // index file
        {
            idx_name = argv[++i];
        }
    }

    std::string out_string = out_name;
    // parse out file extension; look for .fastq.gz, .fastq suffixes
    uint32 len = uint32( strlen(out_name) );
    bool is_gzipped = false;

    // do we have a .gz suffix?
    if (len >= strlen(".gz"))
    {
        if (strcmp(&out_name[len - strlen(".gz")], ".gz") == 0)
        {
            is_gzipped = true;
            len = uint32(len - strlen(".gz"));
        }
    }

    void* output_file  = NULL;
    void* output_index = NULL;

    if (format == ASCII)
    {
        // open a plain ASCII file
        output_file = gzopen( out_name, is_gzipped ? "w1R" : "w" );
    }
    else
    {
        // open a binary file
        output_file = gzopen( out_name, is_gzipped ? "wb1R" : "wbT" );
    }

    if (output_file == NULL)
    {
        log_error(stderr, "    failed opening file \"%s\"\n", out_name);
        return 1;
    }

    if (idx_name)
    {
        output_index = fopen( idx_name, "wb" );
        if (output_index == NULL)
        {
            log_error(stderr, "    failed opening file \"%s\"\n", idx_name);
            return 1;
        }
    }

    log_visible(stderr,"nvExtractReads... started\n");

    uint32       encoding_flags  = 0u;
    if (forward) encoding_flags |= io::FORWARD;
    if (reverse) encoding_flags |= io::REVERSE_COMPLEMENT;

    bool success;

    switch (format)
    {
    case ASCII:
        success = to_ascii( reads_name, output_file, output_index, qencoding, io::SequenceEncoding(encoding_flags) );
        break;
    case PACKED2:
        success = to_packed<2u>( reads_name, output_file, output_index, qencoding, io::SequenceEncoding(encoding_flags) );
        break;
    case PACKED4:
        success = to_packed<4u>( reads_name, output_file, output_index, qencoding, io::SequenceEncoding(encoding_flags) );
        break;
    }

    if (output_file)  gzclose( output_file );
    if (output_index) gzclose( output_index );

    log_visible(stderr,"nvExtractReads... done\n");
    return success ? 0u : 1u;
}
