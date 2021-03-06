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

///
///\file reduce_inl.h
///

#pragma once

#include <nvBowtie/bowtie2/cuda/defs.h>
#include <nvBowtie/bowtie2/cuda/seed_hit.h>
#include <nvBowtie/bowtie2/cuda/params.h>
#include <nvBowtie/bowtie2/cuda/alignment_utils.h>
#include <nvBowtie/bowtie2/cuda/pipeline_states.h>
#include <nvbio/io/alignments.h>
#include <nvbio/io/utils.h>
#include <nvbio/basic/exceptions.h>

namespace nvbio {
namespace bowtie2 {
namespace cuda {

namespace detail {

///@addtogroup nvBowtie
///@{

///@addtogroup Reduce
///@{

///@addtogroup ReduceDetail
///@{

///
///
/// Reduce the list of scores associated to each read in the input queue to find
/// the best 2 alignments.
///
/// \details
/// this kernel takes a batch of extension results (one per active read, indexed by a sorting id)
/// and 'reduces' them with best 2 results found so far.
/// The kernel is parameterized by a templated context which can take further actions upon
/// updates to the best or second best scores, as well as bailing-out on failures.
///
/// \param context             the template context
/// \param pipeline            the pipeline state
/// \param params              algorithm parameters
///
template <typename ScoringScheme, typename PipelineType, typename ReduceContext> __global__ 
void score_reduce_kernel(
    const ReduceContext     context,
          PipelineType      pipeline,
    const ParamsPOD         params)
{
    typedef ScoringQueuesDeviceView                 scoring_queues_type;
    typedef ReadHitsReference<scoring_queues_type>  read_hits_type;
    typedef typename read_hits_type::reference      hit_type;

    scoring_queues_type& scoring_queues = pipeline.scoring_queues;

    const uint32 thread_id = threadIdx.x + BLOCKDIM*blockIdx.x;
    if (thread_id >= scoring_queues.active_read_count()) return;

    // fetch the active read corresponding to this thread
    read_hits_type read_hits( scoring_queues, thread_id );

    const uint32 read_id = read_hits.read_info().read_id;

    // fetch the best alignments
    io::BestAlignments best = pipeline.best_alignments[ read_id ];

    // setup the read stream
    const uint2  read_range = pipeline.reads.get_range(read_id);
    const uint32 read_len   = read_range.y - read_range.x;

    const uint32 count = read_hits.size();

    for (uint32 i = 0; i < count; ++i)
    {
        hit_type hit = read_hits[i];
        NVBIO_CUDA_DEBUG_ASSERT( hit.read_id == read_id, "reduce hit[%u][%u]: expected id %u, got: %u (slot: %u)\n", thread_id, i, read_id, hit.read_id, scoring_queues.hits_index( thread_id, i ));

        const packed_seed seed_info = hit.seed;
        const uint32 read_rc        = seed_info.rc;
        const uint32 top_flag       = seed_info.top_flag;

        const  int32 score          = hit.score;
        const uint32 g_pos          = hit.loc;

        // skip locations that we have already visited without paying the extension attempt
        if ((read_rc == best.m_a1.m_rc && g_pos == best.m_a1.m_align) ||
            (read_rc == best.m_a2.m_rc && g_pos == best.m_a2.m_align))
            continue;

        if (score > best.m_a1.score())
        {
            context.best_score( read_id, params );

            // set the first best score
            best.m_a2 = best.m_a1;
            best.m_a1 = io::Alignment( g_pos, 0u, score, read_rc );
            NVBIO_CUDA_DEBUG_PRINT_IF( params.debug.show_reduce( read_id ), "update best:   (parent[%u:%u])\n  score[%d], rc[%u], pos[%u]\n", thread_id, i, score, read_rc, g_pos );
        }
        else if ((score > best.m_a2.score()) && io::distinct_alignments( best.m_a1.m_align, best.m_a1.m_rc, g_pos, read_rc, read_len/2 ))
        {
            context.second_score( read_id, params );

            // set the second best score
            best.m_a2 = io::Alignment( g_pos, 0u, score, read_rc );
            NVBIO_CUDA_DEBUG_PRINT_IF( params.debug.show_reduce( read_id ), "update second: (parent[%u:%u])\n  score[%d], rc[%u], pos[%u]\n", thread_id, i, score, read_rc, g_pos );
        }
        else if (context.failure( i, read_id, top_flag, params ))
        {
            // stop traversal
            pipeline.hits.erase( read_id );

            // NOTE: we keep using the score entries rather than breaking here,
            // as we've already gone through the effort of computing them, and
            // this reduction is only a minor amount of work.
        }
    }

    // report best alignments
    pipeline.best_alignments[ read_id ] = best;
}

///
/// Reduce the list of scores associated to each read in the input queue to find
/// the best 2 alignments.
///
/// \details
/// this kernel takes a batch of extension results (one per active read, indexed by a sorting id)
/// and 'reduces' them with best 2 results found so far.
/// The kernel is parameterized by a templated context which can take further actions upon
/// updates to the best or second best scores, as well as bailing-out on failures.
///
/// \param context             the template context
/// \param pipeline            the pipeline state
/// \param params              algorithm parameters
///
template <typename ScoringScheme, typename PipelineType, typename ReduceContext> __global__ 
void score_reduce_paired_kernel(
    const ReduceContext     context,
          PipelineType      pipeline,
    const ParamsPOD         params)
{
    typedef ScoringQueuesDeviceView                 scoring_queues_type;
    typedef ReadHitsReference<scoring_queues_type>  read_hits_type;
    typedef typename read_hits_type::reference      hit_type;

    scoring_queues_type& scoring_queues = pipeline.scoring_queues;

    const uint32 thread_id = threadIdx.x + BLOCKDIM*blockIdx.x;
    if (thread_id >= scoring_queues.active_read_count()) return;

    const uint32 anchor = pipeline.anchor;

    // fetch the active read corresponding to this thread
    read_hits_type read_hits( scoring_queues, thread_id );

    const uint32 read_id = read_hits.read_info().read_id;

    // fetch current best alignments
    io::BestPairedAlignments best_pairs = io::BestPairedAlignments(
        read( pipeline.best_alignments   + read_id ),
        read( pipeline.best_alignments_o + read_id ) );

    // setup the read stream
    const uint2  read_range = pipeline.reads.get_range(read_id);
    const uint32 read_len   = read_range.y - read_range.x;

    const uint32 count = read_hits.size();

    // sort the entries in the score queue so that we process them in the order they were selected
    for (uint32 i = 0; i < count; ++i)
    {
        hit_type hit = read_hits[i];
        NVBIO_CUDA_DEBUG_ASSERT( hit.read_id == read_id, "reduce hit[%u][%u]: expected id %u, got: %u (slot: %u)\n", thread_id, i, read_id, hit.read_id, read_hits.slot(i) );

        const packed_seed seed_info = hit.seed;
        const uint32 read_rc        = seed_info.rc;
        const uint32 top_flag       = seed_info.top_flag;

        const uint2  g_pos          = make_uint2( hit.loc, hit.sink );
        const  int32 score1         = hit.score;
        const  int32 score2         = hit.opposite_score;
        const  int32 score          = score1 + score2;

        // compute opposite mate placement & orientation
        bool o_left;
        bool o_fw;

        detail::frame_opposite_mate(
            params.pe_policy,
            anchor,
            !read_rc,
            o_left,
            o_fw );

        const uint2  o_g_pos   = make_uint2( hit.opposite_loc, hit.opposite_sink );
        const uint32 o_read_rc = !o_fw;

        const io::PairedAlignments pair(
            io::Alignment(   g_pos.x,   g_pos.y -   g_pos.x, score1,   read_rc,  anchor, true ),
            io::Alignment( o_g_pos.x, o_g_pos.y - o_g_pos.x, score2, o_read_rc, !anchor, true ) );

        // skip locations that we have already visited without paying the extension attempt
        if (distinct_alignments( best_pairs.pair<0>(), pair ) == false ||
            distinct_alignments( best_pairs.pair<1>(), pair ) == false)
            continue;

        if (score > best_pairs.best_score())
        {
            context.best_score( read_id, params );

            // set the first best score
            best_pairs.m_a2 = best_pairs.m_a1;
            best_pairs.m_o2 = best_pairs.m_o1;
            best_pairs.m_a1 = pair.m_a;
            best_pairs.m_o1 = pair.m_o;
            write( pipeline.best_alignments   + read_id, best_pairs.best_anchor() );
            write( pipeline.best_alignments_o + read_id, best_pairs.best_opposite() );
            NVBIO_CUDA_DEBUG_PRINT_IF( params.debug.show_reduce( read_id ), "update best (anchor[%u]):  (parent[%u:%u])\n  1. score[%d], rc[%u], pos[%u]\n  2. score[%d], rc[%u], pos[%u,%u]\n", anchor, thread_id, i, score1, read_rc, g_pos.y, score2, o_read_rc, o_g_pos.x, o_g_pos.y );
        }
        else if ((score > best_pairs.second_score()) && distinct_alignments( best_pairs.pair<0>(), pair, read_len/2 ))
        {
            context.second_score( read_id, params );

            // set the second best score
            best_pairs.m_a2 = pair.m_a;
            best_pairs.m_o2 = pair.m_o;
            write( pipeline.best_alignments   + read_id, best_pairs.best_anchor() );
            write( pipeline.best_alignments_o + read_id, best_pairs.best_opposite() );
            NVBIO_CUDA_DEBUG_PRINT_IF( params.debug.show_reduce( read_id ), "update second (anchor[%u]):  (parent[%u:%u])\n  1. score[%d], rc[%u], pos[%u]\n  2. score[%d], rc[%u], pos[%u,%u]\n", anchor, thread_id, i, score1, read_rc, g_pos.y, score2, o_read_rc, o_g_pos.x, o_g_pos.y );
        }
        else if ((params.pe_unpaired == true) && (best_pairs.is_paired() == false))
        {
            //
            // We didn't find a paired alignment yet - hence we proceed keeping track of the best unpaired alignments
            // for the first and second mate separately.
            // We store best two alignments for the first mate in a_best_data, and the ones of the second mate in o_best_data.
            //

            io::BestAlignments best = best_pairs.mate( anchor );

            //
            // update the first mate alignments
            //
            if (score1 > best.m_a1.score())
            {
                best.m_a2 = best.m_a1;
                best.m_a1 = io::Alignment( g_pos.x, g_pos.y - g_pos.x, score1, read_rc, anchor, false );
                write( (anchor ? pipeline.best_alignments_o : pipeline.best_alignments) + read_id, best );
                NVBIO_CUDA_DEBUG_PRINT_IF( params.debug.show_reduce( read_id ), "update best unpaired[%u]:  (parent[%u:%u])\n  1. score[%d], rc[%u], pos[%u]\n", anchor, thread_id, i, score1, read_rc, g_pos );
            }
            else if ((score1 > best.m_a2.score()) && io::distinct_alignments( best.m_a1.alignment(), best.m_a1.is_rc(), g_pos.x, read_rc, read_len/2 ))
            {
                best.m_a2 = io::Alignment( g_pos.x, g_pos.y - g_pos.x, score1, read_rc, anchor, false );
                write( (anchor ? pipeline.best_alignments_o : pipeline.best_alignments) + read_id, best );
                NVBIO_CUDA_DEBUG_PRINT_IF( params.debug.show_reduce( read_id ), "update second unpaired[%u]:  (parent[%u:%u])\n  score[%d], rc[%u], pos[%u]\n", anchor, thread_id, i, score1, read_rc, g_pos );
            }
            else if (context.failure( i, read_id, top_flag, params ))
            {
                // stop traversal
                pipeline.hits.erase( read_id );
            }
        }
        else if (context.failure( i, read_id, top_flag, params ))
        {
            //NVBIO_CUDA_DEBUG_PRINT_IF( params.debug.show_reduce( read_id ), "discard:  (parent[%u:%u])\n  score[%d + %d], rc[%u], pos[%u]\n", anchor, thread_id, i, score1, score2, read_rc, g_pos );
            // stop traversal
            pipeline.hits.erase( read_id );
        }
    }
}

///@}  // group ReduceDetail
///@}  // group Reduce
///@}  // group nvBowtie

} // namespace detail

//
// Reduce the scores associated to each read in the input queue to find
// the best 2 alignments.
//
template <typename ScoringScheme, typename ReduceContext>
void score_reduce(
    const ReduceContext                                     context,
    const BestApproxScoringPipelineState<ScoringScheme>&    pipeline,
    const ParamsPOD                                         params)
{
    typedef BestApproxScoringPipelineState<ScoringScheme> pipeline_type;

    const int blocks = (pipeline.scoring_queues.active_reads.in_size + BLOCKDIM-1) / BLOCKDIM;

    detail::score_reduce_kernel<typename pipeline_type::scheme_type> <<<blocks, BLOCKDIM>>>(
        context,
        pipeline,
        params );
}

//
// call the scoring kernel
//
template <typename ScoringScheme, typename ReduceContext>
void score_reduce_paired(
    const ReduceContext                                     context,
    const BestApproxScoringPipelineState<ScoringScheme>&    pipeline,
    const ParamsPOD                                         params)
{
    typedef BestApproxScoringPipelineState<ScoringScheme> pipeline_type;

    const int blocks = (pipeline.scoring_queues.active_reads.in_size + BLOCKDIM-1) / BLOCKDIM;

    detail::score_reduce_paired_kernel<typename pipeline_type::scheme_type> <<<blocks, BLOCKDIM>>>(
        context,
        pipeline,
        params );
}

} // namespace cuda
} // namespace bowtie2
} // namespace nvbio
