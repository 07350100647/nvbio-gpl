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

#pragma once

#include <nvBowtie/bowtie2/cuda/defs.h>
#include <nvbio/io/alignments.h>
#include <nvBowtie/bowtie2/cuda/params.h>
#include <nvBowtie/bowtie2/cuda/pipeline_states.h>

namespace nvbio {
namespace bowtie2 {
namespace cuda {

namespace detail {

///@addtogroup nvBowtie
///@{

///@addtogroup Locate
///@{

///@addtogroup LocateDetail
///@{

///
/// transform an SA index to linear coordinates using either the forward or reverse FM-index
///
template <typename FMType, typename rFMType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
uint32 locate(const FMType& fmi, const rFMType& rfmi, const uint32 dir, const uint32 r)
{
    if (same_type<FMType, rFMType>::pred)
    {
        const FMType fmi_ref = dir == FORWARD ? fmi : rfmi;
        const uint32 g_pos = locate( fmi_ref, r );
        NVBIO_CUDA_ASSERT( g_pos < fmi_ref.length() );
        return (dir == REVERSE) ?
            rfmi.length()-1 - g_pos : g_pos;
    }
    else
    {
        if (dir == FORWARD)
            return locate(fmi, r);
        else
            return rfmi.length()-1 - locate(rfmi, r);
    }
}
///
/// first pass of a two pass method to transform an SA index to linear coordinates using either the forward or reverse FM-index
///
template <typename FMType, typename rFMType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
uint2 locate_init(const FMType& fmi, const rFMType& rfmi, const uint32 dir, const uint32 r)
{
    if (same_type<FMType, rFMType>::pred)
    {
        const FMType fmi_ref = dir == FORWARD ? fmi : rfmi;
        return locate_ssa_iterator( fmi_ref, r );
    }
    else
    {
        if (dir == FORWARD)
            return locate_ssa_iterator( fmi, r );
        else
            return locate_ssa_iterator( rfmi, r );
    }
}

///
/// second pass of a two pass method to transform an SA index to linear coordinates using either the forward or reverse FM-index
///
template <typename FMType, typename rFMType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
uint32 locate_lookup(const FMType& fmi, const rFMType& rfmi, const uint32 dir, const uint2 it)
{
    if (same_type<FMType, rFMType>::pred)
    {
        const FMType fmi_ref = dir == FORWARD ? fmi : rfmi;
        const uint32 g_pos = lookup_ssa_iterator( fmi_ref, it );
        return (dir == REVERSE) ?
            rfmi.length()-1 - g_pos : g_pos;
    }
    else
    {
        if (dir == FORWARD)
            return lookup_ssa_iterator( fmi, it );
        else
            return rfmi.length()-1 - lookup_ssa_iterator( rfmi, it );
    }
}

///
/// Locate the next SA row in the queue.
/// Since the input loc_queue might have been sorted to gather locality, the
/// corresponding entry in seed_queue is now specified by an index (idx_queue).
///
template <typename BatchType, typename FMType, typename rFMType> __global__ 
void locate_kernel(
    const BatchType             read_batch, const FMType fmi, const rFMType rfmi,
    const uint32                in_count,
    const uint32*               idx_queue,
          HitQueuesDeviceView   hits,
    const ParamsPOD             params)
{
    const uint32 thread_id = threadIdx.x + BLOCKDIM*blockIdx.x;
    if (thread_id >= in_count) return;

    const uint32 sa_idx = idx_queue[ thread_id ];  // fetch the sorting queue index

    HitReference<HitQueuesDeviceView> hit( hits, sa_idx );
    const uint32 sa_pos      = hit.loc;                 // fetch the SA coordinate
    const packed_seed info   = hit.seed;                // fetch the attached info
    const uint32 index_dir   = info.index_dir;          // decode the index direction
    const uint32 pos_in_read = info.pos_in_read;        // decode the seed's position in the read

    // locate the SA row and calculate the global position
    const uint32 g_pos = locate( fmi, rfmi, index_dir, sa_pos ) - pos_in_read;

    // overwrite the locate queue with the final position
    hit.loc = g_pos;
}

///
/// Locate the next SA row in the queue.
/// Since the input loc_queue might have been sorted to gather locality, the
/// corresponding entry in seed_queue is now specified by an index (idx_queue).
///
template <typename BatchType, typename FMType, typename rFMType> __global__ 
void locate_init_kernel(
    const BatchType             read_batch, const FMType fmi, const rFMType rfmi,
    const uint32                in_count,
    const uint32*               idx_queue,
          HitQueuesDeviceView   hits,
    const ParamsPOD             params)
{
    const uint32 thread_id = threadIdx.x + BLOCKDIM*blockIdx.x;
    if (thread_id >= in_count) return;

    const uint32 sa_idx = idx_queue[ thread_id ];  // fetch the sorting queue index

    HitReference<HitQueuesDeviceView> hit( hits, sa_idx );
    const uint32 sa_pos      = hit.loc;                 // fetch the SA coordinate
    const packed_seed info   = hit.seed;                // fetch the attached info
    const uint32 index_dir   = info.index_dir;          // decode the index direction

    // locate the SA row and calculate the global position
    const uint2 ssa = locate_init( fmi, rfmi, index_dir, sa_pos );

    // overwrite the locate queue with the final position
    hit.loc = ssa.x;
    hit.ssa = ssa.y;
}
///
/// Locate the next SA row in the queue.
/// Since the input loc_queue might have been sorted to gather locality, the
/// corresponding entry in seed_queue is now specified by an index (idx_queue).
///
template <typename BatchType, typename FMType, typename rFMType> __global__ 
void locate_lookup_kernel(
    const BatchType             read_batch, const FMType fmi, const rFMType rfmi,
    const uint32                in_count,
    const uint32*               idx_queue,
          HitQueuesDeviceView   hits,
    const ParamsPOD             params)
{
    const uint32 thread_id = threadIdx.x + BLOCKDIM*blockIdx.x;
    if (thread_id >= in_count) return;

    const uint32 sa_idx = idx_queue[ thread_id ]; // fetch the sorting queue index

    HitReference<HitQueuesDeviceView> hit( hits, sa_idx );
    const uint32 sa_pos      = hit.loc;                 // fetch the SA coordinate
    const uint32 sa_off      = hit.ssa;                 // fetch the SSA offset
    const packed_seed info   = hit.seed;                // fetch the attached info
    const uint32 index_dir   = info.index_dir;          // decode the index direction
    const uint32 pos_in_read = info.pos_in_read;        // decode the seed's position in the read

    // locate the SA row and calculate the global position
    const uint32 g_pos = locate_lookup( fmi, rfmi, index_dir, make_uint2( sa_pos, sa_off ) ) - pos_in_read;

    // overwrite the locate queue with the final position
    hit.loc = g_pos;
}

///@}  // group LocateDetail
///@}  // group Locate
///@}  // group nvBowtie

} // namespace detail

//
// Locate the next SA row in the queue.
// Since the input loc_queue might have been sorted to gather locality, the
// corresponding entry in seed_queue is now specified by an index (idx_queue).
//
template <typename BatchType, typename FMType, typename rFMType>
void locate(
    const BatchType             read_batch, const FMType fmi, const rFMType rfmi,
    const uint32                in_count,
    const uint32*               idx_queue,
          HitQueuesDeviceView   hits,
    const ParamsPOD             params)
{
    const int blocks = (in_count + BLOCKDIM-1) / BLOCKDIM;

    detail::locate_kernel<<<blocks, BLOCKDIM>>>(
        read_batch, fmi, rfmi,
        in_count,
        idx_queue,
        hits,
        params );
}

//
// Locate the next SA row in the queue.
// Since the input loc_queue might have been sorted to gather locality, the
// corresponding entry in seed_queue is now specified by an index (idx_queue).
//
template <typename BatchType, typename FMType, typename rFMType>
void locate_init(
    const BatchType             read_batch, const FMType fmi, const rFMType rfmi,
    const uint32                in_count,
    const uint32*               idx_queue,
          HitQueuesDeviceView   hits,
    const ParamsPOD             params)
{
    const int blocks = (in_count + BLOCKDIM-1) / BLOCKDIM;

    detail::locate_init_kernel<<<blocks, BLOCKDIM>>>(
        read_batch, fmi, rfmi,
        in_count,
        idx_queue,
        hits,
        params );
}

//
// Locate the next SA row in the queue.
// Since the input loc_queue might have been sorted to gather locality, the
// corresponding entry in seed_queue is now specified by an index (idx_queue).
//
template <typename ScoringScheme>
void locate_init(
    const BaseScoringPipelineState<ScoringScheme>&  pipeline,
    const ParamsPOD                                 params)
{
    locate_init(
        pipeline.reads,
        pipeline.fmi,
        pipeline.rfmi,
        pipeline.hits_queue_size,
        pipeline.idx_queue,
        pipeline.scoring_queues.hits,
        params );
}

//
// Locate the next SA row in the queue.
// Since the input loc_queue might have been sorted to gather locality, the
// corresponding entry in seed_queue is now specified by an index (idx_queue).
//
template <typename BatchType, typename FMType, typename rFMType>
void locate_lookup(
    const BatchType             read_batch, const FMType fmi, const rFMType rfmi,
    const uint32                in_count,
    const uint32*               idx_queue,
          HitQueuesDeviceView   hits,
    const ParamsPOD             params)
{
    const int blocks = (in_count + BLOCKDIM-1) / BLOCKDIM;

    detail::locate_lookup_kernel<<<blocks, BLOCKDIM>>>(
        read_batch, fmi, rfmi,
        in_count,
        idx_queue,
        hits,
        params );
}

//
// Locate the next SA row in the queue.
// Since the input loc_queue might have been sorted to gather locality, the
// corresponding entry in seed_queue is now specified by an index (idx_queue).
//
template <typename ScoringScheme>
void locate_lookup(
    const BaseScoringPipelineState<ScoringScheme>&  pipeline,
    const ParamsPOD                                 params)
{
    const int blocks = (pipeline.hits_queue_size + BLOCKDIM-1) / BLOCKDIM;

    detail::locate_lookup_kernel<<<blocks, BLOCKDIM>>>(
        pipeline.reads,
        pipeline.fmi,
        pipeline.rfmi,
        pipeline.hits_queue_size,
        pipeline.idx_queue,
        pipeline.scoring_queues.hits,
        params );
}

} // namespace cuda
} // namespace bowtie2
} // namespace nvbio
