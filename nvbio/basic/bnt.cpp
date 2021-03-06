/*
 * nvbio
 * Copyright (C) 2012-2014, NVIDIA Corporation
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

#include <nvbio/basic/bnt.h>

namespace nvbio {

//
// NOTE: the code below is a derivative of bntseq.h, originally distributed
// under the MIT License, Copyright (c) 2008 Genome Research Ltd (GRL).
//

void save_bns(const BNTSeq& bns, const char *prefix)
{
    //
    // save the BNT sequence information to two distinct files,
    // one for annotations (.ann) and one for amiguities (.amb).
    // The file formats are taken from BWA 0.6.1.
    //

    { // dump .ann
        std::string filename = std::string( prefix ) + ".ann";
		FILE* file = fopen( filename.c_str(), "w" );
        if (file == NULL)
            throw bns_fopen_failure();

        fprintf( file, "%lld %d %u\n", (long long)bns.l_pac, bns.n_seqs, bns.seed );

        for (int32 i = 0; i != bns.n_seqs; ++i)
        {
            const BNTAnnData& ann_data = bns.anns_data[i];
            const BNTAnnInfo& ann_info = bns.anns_info[i];

            fprintf(file, "%d %s", ann_data.gi, ann_info.name.c_str());
			if (ann_info.anno.length())
                fprintf(file, " %s\n", ann_info.anno.c_str());
			else
                fprintf(file, "\n");

            fprintf( file, "%lld %d %d\n", (long long)ann_data.offset, ann_data.len, ann_data.n_ambs );
		}
		fclose( file );
	}
	{ // dump .amb
        std::string filename = std::string( prefix ) + ".amb";
		FILE* file = fopen( filename.c_str(), "w" );
        if (file == NULL)
            throw bns_fopen_failure();

		fprintf( file, "%lld %d %u\n", (long long)bns.l_pac, bns.n_seqs, bns.n_holes );
		for (int32 i = 0; i != bns.n_holes; ++i)
        {
            const BNTAmb& amb = bns.ambs[i];
			fprintf( file, "%lld %d %c\n", (long long)amb.offset, amb.len, amb.amb );
		}
		fclose( file );
	}
}
void load_bns(BNTSeq& bns, const char *prefix)
{
    //
    // load the BNT sequence information from two distinct files,
    // one for annotations (.ann) and one for amiguities (.amb).
    // The file formats are taken from BWA 0.6.1.
    //

    { // read .ann
        std::string filename = std::string( prefix ) + ".ann";
		FILE* file = fopen( filename.c_str(), "r" );
        if (file == NULL)
            throw bns_fopen_failure();

        fscanf( file, "%lld%d%u\n", &bns.l_pac, &bns.n_seqs, &bns.seed );

        bns.anns_data.resize( bns.n_seqs );
        bns.anns_info.resize( bns.n_seqs );

        for (int32 i = 0; i != bns.n_seqs; ++i)
        {
            BNTAnnData& ann_data = bns.anns_data[i];
            BNTAnnInfo& ann_info = bns.anns_info[i];

            char buffer[2048];

            // read gi and name
            fscanf( file, "%d%s", &ann_data.gi, buffer );

            ann_info.name = buffer;

            // read fasta comments 
            {
                char* p = buffer;
                char c;
			    while ((c = fgetc( file )) != '\n' && c != EOF)
                    *p++ = c;

			    *p = '\0';

                // skip leading spaces
                for (p = buffer; *p == ' '; ++p) {}

                ann_info.anno = p;
            }

            fscanf(file, "%lld%d%d\n", &ann_data.offset, &ann_data.len, &ann_data.n_ambs);
		}
		fclose( file );
    }
    { // read .amb
        std::string filename = std::string( prefix ) + ".amb";
		FILE* file = fopen( filename.c_str(), "r" );
        if (file == NULL)
            throw bns_fopen_failure();

        try
        {
            int64 l_pac;
            int32 n_seqs;
		    fscanf( file, "%lld%d%u\n", &l_pac, &n_seqs, &bns.n_holes );

            if (l_pac != bns.l_pac || n_seqs != bns.n_seqs)
                throw bns_files_mismatch();

            bns.ambs.resize( bns.n_holes );

            for (int32 i = 0; i != bns.n_holes; ++i)
            {
                BNTAmb& amb = bns.ambs[i];
			    fscanf( file, "%lld%d%c\n", &amb.offset, &amb.len, &amb.amb );
		    }
		    fclose( file );
        }
        catch (...)
        {
		    fclose( file );
            throw;
        }
    }
}

void load_bns_info(BNTInfo& bns, const char *prefix)
{
    { // read .ann
        std::string filename = std::string( prefix ) + ".ann";
		FILE* file = fopen( filename.c_str(), "r" );
        if (file == NULL)
            throw bns_fopen_failure();

        fscanf( file, "%lld%d%u\n", &bns.l_pac, &bns.n_seqs, &bns.seed );
    }
    { // read .amb
        std::string filename = std::string( prefix ) + ".amb";
		FILE* file = fopen( filename.c_str(), "r" );
        if (file == NULL)
            throw bns_fopen_failure();

        try
        {
            int64 l_pac;
            int32 n_seqs;
		    fscanf( file, "%lld%d%u\n", &l_pac, &n_seqs, &bns.n_holes );

            if (l_pac != bns.l_pac || n_seqs != bns.n_seqs)
                throw bns_files_mismatch();
        }
        catch (...)
        {
		    fclose( file );
            throw;
        }
    }
}
void load_bns(BNTSeqLoader* bns, const char *prefix)
{
    //
    // load the BNT sequence information from two distinct files,
    // one for annotations (.ann) and one for amiguities (.amb).
    // The file formats are taken from BWA 0.6.1.
    //

    BNTInfo info;

    load_bns_info( info, prefix );

    bns->set_info( info );

    { // read .ann
        std::string filename = std::string( prefix ) + ".ann";
		FILE* file = fopen( filename.c_str(), "r" );
        if (file == NULL)
            throw bns_fopen_failure();

        int64 l_pac;
        int32 n_seqs;
        uint32 seed;
        fscanf( file, "%lld%d%u\n", &l_pac, &n_seqs, &seed );

        BNTAnnData ann_data;
        BNTAnnInfo ann_info;

        for (int32 i = 0; i != n_seqs; ++i)
        {
            char buffer[2048];

            // read gi and name
            fscanf( file, "%d%s", &ann_data.gi, buffer );

            ann_info.name = buffer;

            // read fasta comments 
            {
                char* p = buffer;
                char c;
			    while ((c = fgetc( file )) != '\n' && c != EOF)
                    *p++ = c;

			    *p = '\0';

                // skip leading spaces
                for (p = buffer; *p == ' '; ++p) {}

                ann_info.anno = p;
            }

            fscanf(file, "%lld%d%d\n", &ann_data.offset, &ann_data.len, &ann_data.n_ambs);
            bns->read_ann( ann_info, ann_data );
		}
		fclose( file );
    }
    { // read .amb
        std::string filename = std::string( prefix ) + ".amb";
		FILE* file = fopen( filename.c_str(), "r" );
        if (file == NULL)
            throw bns_fopen_failure();

        try
        {
            int64 l_pac;
            int32 n_seqs;
            int32 n_holes;
		    fscanf( file, "%lld%d%u\n", &l_pac, &n_seqs, &n_holes );

            BNTAmb amb;
            for (int32 i = 0; i != n_holes; ++i)
            {
			    fscanf( file, "%lld%d%c\n", &amb.offset, &amb.len, &amb.amb );
                bns->read_amb( amb );
		    }
		    fclose( file );
        }
        catch (...)
        {
		    fclose( file );
            throw;
        }
    }
}

} // namespace nvbio
