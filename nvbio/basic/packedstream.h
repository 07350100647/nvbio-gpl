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

#include <nvbio/basic/types.h>
#include <nvbio/basic/numbers.h>
#include <nvbio/basic/strided_iterator.h>
#include <nvbio/basic/iterator.h>
#if defined(__CUDACC__)
#include <nvbio/basic/cuda/arch.h>
#endif

#if defined(BIG_ENDIAN)
#undef BIG_ENDIAN
#endif

namespace nvbio {

/// \page packed_streams_page Packed Streams
///
/// This module implements interfaces to hold binary packed streams expressed using compile-time specified alphabet sizes.
/// The idea is that a packed stream is an open-ended sequence of symbols encoded with a fixed number of bits on an underlying
/// stream of words.
/// The words themselves can be of different types, ranging from uint32 to uint4, to support different kind of memory access
/// patterns.
///
/// \section AtAGlanceSection At a Glance
///
/// The main classes are:
///
/// - PackedVector :             a packed vector object
/// - PackedStream :             a packed stream object
/// - PackedStreamRef :          a proxy object to represent packed symbol references
/// - \ref PackedStringLoaders : a packed stream loader which allows to cache portions of a packed stream into
///                              different memory spaces (e.g. local memory)
///
/// \section ExampleSection Example
///
///\code
/// // pack 16 DNA symbols using a 2-bit alphabet into a single word
/// uint32 word;
/// PackedStream<uint32,uint8,2u,false> packed_string( &word );
///
/// const uint32 string_len = 16;
/// const char   string[]   = "ACGTTGCAACGTTGCA";
/// for (uint32 i = 0; i < string_len; ++i)
///     packed_string[i] = char_to_dna( string[i] );
///
/// // and count the occurrences of T
/// const uint32 occ = util::count_occurrences( packed_string, string_len, char_to_dna('T') );
///\endcode
///
/// \section TechnicalDetailsSection Technical Details
///
/// A detailed description of all the classes and functions in this module can be found in the
/// \ref PackedStreams module documentation.
///

///@addtogroup Basic
///@{

///@defgroup PackedStreams Packed Streams
/// This module implements interfaces to hold binary packed streams expressed using compile-time specified alphabet sizes.
/// The idea is that a packed stream is an open-ended sequence of symbols encoded with a fixed number of bits on an underlying
/// stream of words.
/// The words themselves can be of different types, ranging from uint32 to uint4, to support different kind of memory access
/// patterns.
///@{

/// Basic stream traits class, providing compile-time information about a string type
///
template <typename T> struct stream_traits
{
    typedef uint32 index_type;
    typedef char   symbol_type;

    static const uint32 SYMBOL_SIZE  = 8u;
    static const uint32 SYMBOL_COUNT = 256u;
};

/// T* specialization of the stream_traits class, providing compile-time information about a string type
///
template <typename T> struct stream_traits<T*>
{
    typedef uint32 index_type;
    typedef T      symbol_type;

    static const uint32 SYMBOL_SIZE  = uint32( 8u * sizeof(T) );
    static const uint32 SYMBOL_COUNT = uint32( (uint64(1u) << SYMBOL_SIZE) - 1u );
};

/// const T* specialization of the stream_traits class, providing compile-time information about a string type
///
template <typename T> struct stream_traits<const T*>
{
    typedef uint32 index_type;
    typedef T      symbol_type;

    static const uint32 SYMBOL_SIZE  = uint32( 8u * sizeof(T) );
    static const uint32 SYMBOL_COUNT = uint32( (uint64(1u) << SYMBOL_SIZE) - 1u );
};

///
/// PackedStream reference wrapper
///
template <typename Stream>
struct PackedStreamRef
{
    typedef typename Stream::symbol_type Symbol;
    typedef typename Stream::symbol_type symbol_type;
    typedef symbol_type                  value_type;
    typedef typename Stream::index_type  index_type;
    typedef typename Stream::sindex_type sindex_type;

    /// constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamRef(Stream stream)
        : m_stream( stream ) {}

    /// copy constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamRef(const PackedStreamRef& ref)
        : m_stream( ref.m_stream ) {}

    /// assignment operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamRef& operator= (const PackedStreamRef& ref);

    /// assignment operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamRef& operator= (const Symbol s);

    /// conversion operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE operator Symbol() const;

    Stream m_stream;
};

/// redefine the to_const meta-function for PackedStreamRef to just return a symbol
///
template <typename Stream> struct to_const< PackedStreamRef<Stream> >
{
    typedef typename PackedStreamRef<Stream>::symbol_type type;
};

///
/// A class to represent a packed stream of symbols, where the size of the
/// symbol is specified at compile-time as a template parameter.
/// The sequence is packed on top of an underlying stream of words, whose type can also be specified at compile-time
/// in order to allow for different memory access patterns.
///
/// \tparam InputStream         the underlying stream of words used to hold the packed stream (e.g. uint32, uint4)
/// \tparam Symbol              the unpacked symbol type (e.g. uint8)
/// \tparam SYMBOL_SIZE_T       the number of bits needed for each symbol
/// \tparam BIG_ENDIAN_T        the "endianness" of the words: if true, symbols will be packed from right to left within each word
/// \tparam IndexType           the type of integer used to address the stream (e.g. uint32, uint64)
///
template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType = uint32>
struct PackedStream
{
    typedef PackedStream<InputStream,Symbol,SYMBOL_SIZE_T, BIG_ENDIAN_T,IndexType> This;

    static const uint32 SYMBOL_SIZE  = SYMBOL_SIZE_T;
    static const uint32 SYMBOL_COUNT = 1u << SYMBOL_SIZE;
    static const uint32 SYMBOL_MASK  = SYMBOL_COUNT - 1u;
    static const uint32 BIG_ENDIAN   = BIG_ENDIAN_T;
    static const uint32 ALPHABET_SIZE = SYMBOL_COUNT;

    typedef typename unsigned_type<IndexType>::type                  index_type;
    typedef typename   signed_type<IndexType>::type                 sindex_type;
    typedef typename std::iterator_traits<InputStream>::value_type  storage_type;

    typedef InputStream                                                     stream_type;
    typedef InputStream                                                     storage_iterator;
    typedef Symbol                                                          symbol_type;
    typedef This                                                            iterator;
    typedef PackedStreamRef<This>                                           reference;
    typedef Symbol                                                          const_reference;
    typedef reference*                                                      pointer;
    typedef typename std::iterator_traits<InputStream>::iterator_category   iterator_category;
    typedef symbol_type                                                     value_type;
    typedef sindex_type                                                     difference_type;
    typedef sindex_type                                                     distance_type;

    /// empty constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream() {}

    /// constructor
    ///
    template <typename UInputStream>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE explicit PackedStream(const UInputStream stream, const index_type index = 0) : m_stream( static_cast<InputStream>(stream) ), m_index( index ) {}

    /// constructor
    ///
    template <typename UInputStream, typename USymbol>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream(const PackedStream<UInputStream,USymbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& other) :
        m_stream( static_cast<InputStream>( other.stream() ) ), m_index( other.index() ) {}

    /// dereference operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE reference operator* () const { return reference( *this ); }

    /// get the i-th symbol
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol operator[] (const index_type i) const { return get(i); }
    /// get the i-th symbol
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE reference operator[] (const index_type i) { return reference( *this + i ); }

    /// get the i-th symbol
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get(const index_type i) const;

    /// set the i-th symbol
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set(const index_type i, const Symbol s);

    /// return begin iterator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE iterator begin() const;

    /// return the base stream
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    InputStream stream() const { return m_stream; }

    /// return the offset this iterator refers to
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    index_type index() const { return m_index; }

    /// assignment operator
    ///
    template <typename UInputStream, typename USymbol>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    PackedStream& operator=(const PackedStream<UInputStream,USymbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& other)
    {
        m_stream = static_cast<InputStream>( other.stream() );
        return *this;
    }

    /// pre-increment operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream& operator++ ();

    /// post-increment operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream operator++ (int dummy);

    /// pre-decrement operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream& operator-- ();

    /// post-decrement operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream operator-- (int dummy);

    /// add offset
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream& operator+= (const sindex_type distance);

    /// subtract offset
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream& operator-= (const sindex_type distance);

    /// add offset
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream operator+ (const sindex_type distance) const;

    /// subtract offset
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStream operator- (const sindex_type distance) const;

    /// difference
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE sindex_type operator- (const PackedStream it) const;

private:
    InputStream m_stream;
    index_type  m_index;
};

/// less than
///
template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator< (
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it1,
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it2);

/// greater than
///
template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator> (
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it1,
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it2);

/// equality test
///
template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator== (
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it1,
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it2);

/// inequality test
///
template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator!= (
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it1,
    const PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>& it2);

/// assign a sequence to a packed stream
///
template <typename InputIterator, typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_HOST_DEVICE
void assign(
    const IndexType                                                                                 input_len,
    InputIterator                                                                                   input_string,
    PackedStream<InputStream,Symbol,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>                           packed_string);

/// PackedStream specialization of the stream_traits class, providing compile-time information about the
/// corresponding string type
///
template <typename InputStream, typename SymbolType, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
struct stream_traits< PackedStream<InputStream,SymbolType,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType> >
{
    typedef IndexType   index_type;
    typedef SymbolType  symbol_type;

    static const uint32 SYMBOL_SIZE  = PackedStream<InputStream,SymbolType,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>::SYMBOL_SIZE;
    static const uint32 SYMBOL_COUNT = PackedStream<InputStream,SymbolType,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>::SYMBOL_COUNT;
};

///
/// A utility class to view a uint4 iterator as a uint32 one
///
template <typename IteratorType>
struct uint4_as_uint32_iterator
{
    typedef uint32                                                          value_type;
    typedef value_type*                                                     pointer;
    typedef value_type                                                      reference;
    typedef typename std::iterator_traits<IteratorType>::difference_type    difference_type;
    //typedef typename std::iterator_traits<IteratorType>::distance_type      distance_type;
    typedef typename std::iterator_traits<IteratorType>::iterator_category  iterator_category;

    /// empty constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint4_as_uint32_iterator() {}

    /// constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint4_as_uint32_iterator(const IteratorType it) : m_it( it )  {}

    /// indexing operator
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    value_type operator[] (const uint32 i) const
    {
        const uint32 c = i & 3u;
        const uint32 w = i >> 2;
        return nvbio::comp( m_it[w], c );
    }

    IteratorType m_it;
};

///
/// A utility function to transpose a set of packed input streams:
///   the symbols of the i-th input stream is supposed to be stored contiguously in the range [offset(i), offset + N(i)]
///   the *words* of i-th output stream will be stored in strided fashion at out_stream[tid, tid + (N(i)+symbols_per_word-1/symbols_per_word) * stride]
///
/// \param stride       output stride
/// \param N            length of this thread's string in the input stream
/// \param in_offset    offset of this thread's string in the input stream
/// \param in_stream    input stream
/// \param out_stream   output stream (usually of the form ptr + thread_id)
///
template <uint32 BLOCKDIM, uint32 BITS, bool BIG_ENDIAN, typename InStreamIterator, typename OutStreamIterator>
NVBIO_HOST_DEVICE
void transpose_packed_streams(const uint32 stride, const uint32 N, const uint32 in_offset, const InStreamIterator in_stream, OutStreamIterator out_stream);

///@} PackedStreams
///@} Basic

} // namespace nvbio

namespace std {

/// overload swap for PackedStreamRef to make sure it does the right thing
///
template <typename Stream>
void swap(
    nvbio::PackedStreamRef<Stream> ref1,
    nvbio::PackedStreamRef<Stream> ref2)
{
    typename nvbio::PackedStreamRef<Stream>::value_type tmp = ref1;

    ref1 = ref2;
    ref2 = tmp;
}

template <typename InputStream, typename SymbolType, nvbio::uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
void iter_swap(
    nvbio::PackedStream<InputStream,SymbolType,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType> it1,
    nvbio::PackedStream<InputStream,SymbolType,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType> it2)
{
    typename nvbio::PackedStream<InputStream,SymbolType,SYMBOL_SIZE_T,BIG_ENDIAN_T,IndexType>::value_type tmp = *it1;

    *it1 = *it2;
    *it2 = tmp;
}

} // std

#include <nvbio/basic/packedstream_inl.h>
