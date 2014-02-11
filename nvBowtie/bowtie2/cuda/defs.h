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
///\file defs.h
///

/// \defgroup nvBowtie nvBowtie
/// see \ref nvBowtieArch

#pragma once

//#define NVBIO_CUDA_DEBUG
//#define NVBIO_CUDA_NON_BLOCKING_ASSERTS
//#define NVBIO_CUDA_ASSERTS

#define USE_TEX             1
#define USE_TEX_READS       1
#define USE_UINT4_PACKING   0

#if USE_TEX
#define TEX_STATEMENT(x) x
#define TEX_SELECTOR( vec, tex ) tex
#else
#define TEX_STATEMENT(x)
#define TEX_SELECTOR( vec, tex ) vec
#endif

#if USE_TEX_READS
#define READ_TEX_STATEMENT(x) x
#define READ_TEX_SELECTOR( vec, tex ) tex
#else
#define READ_TEX_STATEMENT(x)
#define READ_TEX_SELECTOR( vec, tex ) vec
#endif

#define USE_WARP_SYNCHRONOUS_QUEUES 1
#define USE_REVERSE_INDEX           0

#define DO_OPTIONAL_SYNCHRONIZE     1
#define DO_DEVICE_TIMING            0

#include <nvbio/basic/cuda/arch.h>
#include <nvbio/basic/cached_iterator.h>
#include <nvbio/basic/packedstream.h>
#include <nvbio/basic/pod.h>
#include <nvbio/io/reads/reads.h> // FIXME: for PE_POLICY!
#include <algorithm>

namespace nvbio {
namespace bowtie2 {
namespace cuda {

///@addtogroup nvBowtie
///@{

///@addtogroup Defs
///@{

enum EndType { kSingleEnd = 0u, kPairedEnds = 1u };

// We process reads in batches of up to MAX_BATCH size.
enum { MAX_BATCH = 512*1024 };
// The device will launch blocks of size BLOCKDIM.
enum { BLOCKDIM  = 96 };
// The device will launch blocks of size BLOCKDIM.
enum { SCORE_MATE_BLOCKDIM = 128 };
// Edit distance kernel band size.
// for N errors we need band length 2*N+1
enum { MAX_BAND_LEN = 63 };
// We allow for at most this read length
enum { MAXIMUM_READ_LENGTH = 512 };
// We allow for at most this insert length
enum { MAXIMUM_INSERT_LENGTH = 1024 };
// We allow for at most this band length
enum { MAXIMUM_BAND_LENGTH = 31 };
// We allow at most MAXIMUM_BAND_LENGTH*MAXIMUM_BAND_LEN_MULT errors
enum { MAXIMUM_BAND_LEN_MULT = 4 };

enum { BANDED_DP_CHECKPOINTS = 16 };
enum { FULL_DP_CHECKPOINTS   = 64 };

// Maximum number of words allocated for local memory strings
enum { LMEM_CACHE_WORDS = 64 };

struct edit_distance_scoring_tag {};
struct smith_waterman_scoring_tag {};

///
/// A helper class to track debugging info
///
struct DebugState
{
    uint32  read_id;
    bool    select;
    bool    locate;
    bool    score;
    bool    score_bad;
    bool    score_info;
    bool    reduce;
    bool    traceback;
    bool    asserts;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool show_select        (const uint32 id)                   const { return select && id == read_id; }
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool show_locate        (const uint32 id)                   const { return locate && id == read_id; }
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool show_score         (const uint32 id, const bool good)  const { return score && id == read_id && (score_bad || good); }
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool show_score_info    (const uint32 id)                   const { return score_info && id == read_id; }
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool show_reduce        (const uint32 id)                   const { return reduce && id == read_id; }
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool show_traceback     (const uint32 id)                   const { return traceback && id == read_id; }
};

enum {
    HIT_STATS_RANGES    = 0,
    HIT_STATS_MAX_RANGE = 1,
    HIT_STATS_TOTAL     = 2,
    HIT_STATS_MAX       = 3,
    HIT_STATS_TOP       = 4,
    HIT_STATS_TOP_MAX   = 5,
    HIT_STATS_BINS      = 6,
    HIT_STATS_TOP_BINS  = 6 + 32,
};

///@}  // group Defs

///
/// A simple POD struct to represent a read id and its top flag status in a packed uint32
///
struct packed_read
{
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
    packed_read() {}

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
    packed_read(const uint32 _read_id, const uint32 _top_flag = 1u) :
        read_id( _read_id ), top_flag(_top_flag) {}

    uint32 read_id:31, top_flag:1;
};

///
/// A simple POD struct to represent a seed's information in a packed uint32:
///  - position in read
///  - FM-index direction
///  - reverse-complement flag
///  - top flag
///
struct packed_seed
{
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
    packed_seed() {}

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
    packed_seed(const uint32 _pos_in_read, const uint32 _index_dir, const uint32 _rc, const uint32 _top_flag) :
        pos_in_read(_pos_in_read), index_dir(_index_dir), rc(_rc), top_flag(_top_flag) {}

    uint32 pos_in_read:12, index_dir:1, rc:1, top_flag:1;
};

/// pack-read functor, converting an integer read id into a packed_read
///
struct pack_read
{
    typedef uint32      argument_type;
    typedef packed_read result_type;

    /// default constructor
    ///
    pack_read() : top_flag(0u) {}

    /// constructor
    ///
    /// \param _top_flag     top flag
    ///
    pack_read(const uint32 _top_flag) : top_flag(_top_flag) {}

    /// functor operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE 
    packed_read operator() (const uint32 i) const { return packed_read(i, top_flag); }

    uint32 top_flag;
};

///@}  // group nvBowtie

} // namespace cuda
} // namespace bowtie2
} // namespace nvbio
