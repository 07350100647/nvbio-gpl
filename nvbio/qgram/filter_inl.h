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

#pragma once

namespace nvbio {

namespace qgram {

// return the size of a given range
struct range_size
{
    typedef uint2  argument_type;
    typedef uint32 result_type;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 operator() (const uint2 range) const { return range.y - range.x; }
};

template <typename qgram_index_type, typename index_iterator, typename coord_type>
struct filter_results {};

template <typename qgram_index_type, typename index_iterator>
struct filter_results< qgram_index_type, index_iterator, uint32 >
{
    typedef uint32  argument_type;
    typedef uint2   result_type;

    // constructor
    filter_results(
        const qgram_index_type  _qgram_index,
        const uint32            _n_queries,
        const uint32*           _slots,
        const uint2*            _ranges,
        const index_iterator    _index) :
    qgram_index ( _qgram_index ),
    n_queries   ( _n_queries ),
    slots       ( _slots ),
    ranges      ( _ranges ),
    index       ( _index ) {}

    // functor operator
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint2 operator() (const uint32 output_index) const
    {
        // find the text q-gram slot corresponding to this output index
        const uint32 slot = uint32( upper_bound(
            output_index,
            slots,
            n_queries ) - slots );

        // fetch the corresponding text position
        const uint32 text_pos    = index[ slot ];

        // locate the hit q-gram position
        const uint2  range       = ranges[ slot ];
        const uint32 base_slot   = slot ? slots[ slot-1 ] : 0u;
        const uint32 local_index = output_index - base_slot;

        const uint32 qgram_pos = qgram_index.locate( range.x + local_index );

        // and write out the pair (qgram_pos,text_pos)
        return make_uint2( qgram_pos, text_pos );
    }

    const qgram_index_type  qgram_index;
    const uint32            n_queries;
    const uint32*           slots;
    const uint2*            ranges;
    const index_iterator    index;
};

template <typename qgram_index_type, typename index_iterator>
struct filter_results< qgram_index_type, index_iterator, uint2 >
{
    typedef uint32  argument_type;
    typedef uint2   result_type;

    // constructor
    filter_results(
        const qgram_index_type  _qgram_index,
        const uint32            _n_queries,
        const uint32*           _slots,
        const uint2*            _ranges,
        const index_iterator    _index) :
    qgram_index ( _qgram_index ),
    n_queries   ( _n_queries ),
    slots       ( _slots ),
    ranges      ( _ranges ),
    index       ( _index ) {}

    // functor operator
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint2 operator() (const uint32 output_index) const
    {
        // find the text q-gram slot corresponding to this output index
        const uint32 slot = uint32( upper_bound(
            output_index,
            slots,
            n_queries ) - slots );

        // fetch the corresponding text position
        const uint32 text_pos    = index[ slot ];

        // locate the hit q-gram position
        const uint2  range       = ranges[ slot ];
        const uint32 base_slot   = slot ? slots[ slot-1 ] : 0u;
        const uint32 local_index = output_index - base_slot;

        const uint2 qgram_pos = qgram_index.locate( range.x + local_index );

        // and write out the pair (string-id,text-diagonal)
        return make_uint2( qgram_pos.x, text_pos - qgram_pos.y );
    }

    const qgram_index_type  qgram_index;
    const uint32            n_queries;
    const uint32*           slots;
    const uint2*            ranges;
    const index_iterator    index;
};

} // namespace qgram 

// enact the q-gram filter
//
// \param qgram_index      the q-gram index
// \param n_queries        the number of query q-grams
// \param queries          the query q-grams
// \param indices          the query indices
//
template <typename qgram_index_type, typename query_iterator, typename index_iterator>
void QGramFilter<host_tag>::enact(
    const qgram_index_type& qgram_index,
    const uint32            n_queries,
    const query_iterator    queries,
    const index_iterator    indices)
{
    typedef typename qgram_index_type::coord_type coord_type;

    m_ranges.resize( n_queries );
    m_slots.resize( n_queries );

    // search the q-grams in the index, obtaining a set of ranges
    thrust::transform(
        queries,
        queries + n_queries,
        m_ranges.begin(),
        qgram_index );

    // scan their size to determine the slots
    thrust::inclusive_scan(
        thrust::make_transform_iterator( m_ranges.begin(), qgram::range_size() ),
        thrust::make_transform_iterator( m_ranges.begin(), qgram::range_size() ) + n_queries,
        m_slots.begin() );

    // determine the total number of occurrences
    const uint32 n_occurrences = m_slots[ n_queries-1 ];

    // resize the output buffer
    m_output.resize( n_occurrences );

    // and fill it
    thrust::transform(
        thrust::make_counting_iterator<uint32>(0u),
        thrust::make_counting_iterator<uint32>(0u) + n_occurrences,
        m_output.begin(),
        qgram::filter_results<qgram_index_type,index_iterator,coord_type>(
            qgram_index,
            n_queries,
            nvbio::plain_view( m_slots ),
            nvbio::plain_view( m_ranges ),
            indices ) );

    /*
    // now sort the results by (id, diagonal)
    thrust::device_ptr<uint64> output_ptr( (uint64*)nvbio::plain_view( m_output ) );
    thrust::sort(
        output_ptr,
        output_ptr + n_occurrences );
        */
}

// enact the q-gram filter
//
// \param qgram_index      the q-gram index
// \param n_queries        the number of query q-grams
// \param queries          the query q-grams
// \param indices          the query indices
//
template <typename qgram_index_type, typename query_iterator, typename index_iterator>
void QGramFilter<device_tag>::enact(
    const qgram_index_type& qgram_index,
    const uint32            n_queries,
    const query_iterator    queries,
    const index_iterator    indices)
{
    typedef typename qgram_index_type::coord_type coord_type;

    m_ranges.resize( n_queries );
    m_slots.resize( n_queries );

    // search the q-grams in the index, obtaining a set of ranges
    thrust::transform(
        queries,
        queries + n_queries,
        m_ranges.begin(),
        qgram_index );

    // scan their size to determine the slots
    cuda::inclusive_scan(
        n_queries,
        thrust::make_transform_iterator( m_ranges.begin(), qgram::range_size() ),
        m_slots.begin(),
        thrust::plus<uint32>(),
        d_temp_storage );

    // determine the total number of occurrences
    const uint32 n_occurrences = m_slots[ n_queries-1 ];

    // resize the output buffer
    m_output.resize( n_occurrences );

    // and fill it
    thrust::transform(
        thrust::make_counting_iterator<uint32>(0u),
        thrust::make_counting_iterator<uint32>(0u) + n_occurrences,
        m_output.begin(),
        qgram::filter_results<qgram_index_type,index_iterator,coord_type>(
            qgram_index,
            n_queries,
            nvbio::plain_view( m_slots ),
            nvbio::plain_view( m_ranges ),
            indices ) );

    /*
    // now sort the results by (id, diagonal)
    thrust::device_ptr<uint64> output_ptr( (uint64*)nvbio::plain_view( m_output ) );
    thrust::sort(
        output_ptr,
        output_ptr + n_occurrences );
        */
}

} // namespace nvbio
