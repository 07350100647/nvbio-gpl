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

#include <nvBowtie/bowtie2/cuda/aligner_inst.h>
#include <nvBowtie/bowtie2/cuda/aligner_best_approx.h>

namespace nvbio {
namespace bowtie2 {
namespace cuda {

void best_approx_sw(
          Aligner&                  aligner,
    const Params&                   params,
    const FMIndexDef::type          fmi,
    const FMIndexDef::type          rfmi,
    const UberScoringScheme&        scoring_scheme,
    const io::SequenceDataDevice&   reference_data,
    const io::FMIndexDataDevice&    driver_data,
    io::SequenceDataDevice&         read_data,
    Stats&                          stats)
{
    aligner.best_approx<smith_waterman_scoring_tag>(
        params,
        fmi,
        rfmi,
        scoring_scheme,
        reference_data,
        driver_data,
        read_data,
        stats );
}

} // namespace cuda
} // namespace bowtie2
} // namespace nvbio
