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
#include <nvbio/basic/iterator.h>

namespace nvbio {

///\page tries_page Tries Module
///\htmlonly
/// <img src="nvidia_cubes.png" style="position:relative; bottom:-10px; border:0px;"/>
///\endhtmlonly
///
///\n
/// NVBIO contains a few abstractions for representing suffix tries of different kinds.\n
/// At the moment, the supported ones are:
/// - \ref SuffixTriesModule - explicitly stored tries
/// - \ref SortedDictionarySuffixTriesModule - implicit suffix tries built on the fly over sorted dictionaries
///

///@addtogroup TriesModule Tries
///@{

///@addtogroup SuffixTriesModule Suffix Tries
///@{

///
/// A suffix trie can be stored either with an uncompressed layout,
/// where each inner node reseves storage for |alphabet| children
/// and some of them are marked as invalid, or with a compressed one,
/// where only storage for the active children is actually reserved;
/// the latter type requires just a little more logic during traversal,
/// as a popcount is necessary to select the child corresponding to the
/// i-th character.
///
enum TrieType { CompressedTrie, UncompressedTrie };

///
/// A suffix trie node
///
/// \tparam TYPE_T      a suffix trie can be stored either with an uncompressed layout,
///                     where each inner node reseves storage for |alphabet| children
///                     and some of them are marked as invalid, or with a compressed one,
///                     where only storage for the active children is actually reserved;
///                     the latter type requires just a little more logic during traversal,
///                     as a popcount is necessary to select the child corresponding to the
///                     i-th character.
///
template <TrieType TYPE_T>
struct TrieNode
{
    static const uint32     invalid_node = uint32(-1);
    static const TrieType   trie_type    = TYPE_T;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    TrieNode();

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    TrieNode(const uint32 _child, const uint32 _mask);

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    bool is_leaf() const;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 child() const;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 child(const uint32 c) const;
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 nth_child(const uint32 c) const;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 first_child() const;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 mask() const;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void set_child_bit(const uint32 c);

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 child_bit(const uint32 c) const;

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void set_size(const uint32 size);

    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 size() const;

    uint32 m_child;
    uint32 m_mask:5, m_size:27;         // FIXME: using 5 bits is only valid for 5-letter alphabets!
};

///
/// A suffix trie type built on a generic dictionary of sorted strings
///
/// \tparam ALPHABET_SIZE_T         size of the alphabet
/// \tparam NodeIterator            the type of node used, must be a TrieNode<T>
///
template <uint32 ALPHABET_SIZE_T, typename NodeIterator>
struct SuffixTrie
{
    const static uint32 ALPHABET_SIZE = ALPHABET_SIZE_T;

    typedef typename std::iterator_traits<NodeIterator>::value_type node_type;

    /// constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    SuffixTrie() {}

    /// constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    SuffixTrie(const NodeIterator seq);

    /// return the root node of the dictionary seen as a trie
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    node_type root() const;

    /// visit the children of a given node
    ///
    /// \tparam Visitor     a visitor implementing the following interface:
    /// \code
    /// struct Visitor
    /// {
    ///     // do something with the node corresponding to character c
    ///     void visit(const uint8 c, const NodeType node);
    /// }
    /// \endcode
    ///
    template <typename Visitor>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void children(const node_type node, Visitor& visitor) const;

    /// nodes
    ///
    const NodeIterator& nodes() const { return m_seq; }

private:
    NodeIterator m_seq;
};

/// copy a generic trie into a SuffixTrie.
///
/// \tparam TrieType            input trie
/// \tparam NodeVector          output vector of trie nodes; this class
///                             must expose the following interface:
///
///\code
/// struct NodeVector
/// {
///     // return the vector size
///     uint32 size();
///
///     // resize the vector
///     void resize(uint32 size);
///
///     // return a reference to the i-th node
///     value_type& operator[] (uint32 i);
/// }
///\endcode
///
/// Example:
///\code
/// // a string set of some kind
/// string_set_type string_set;
/// ...
///
/// // an implicit suffix trie over a sorted dictionary
/// SortedDictionarySuffixTrie<2,string_set_type> sorted_dictionary(
///     string_set.begin(),
///     string_set.size() );
///
/// // build an explicit suffix trie
/// std::vector< TrieNode<CompressedTrie> > trie_nodes;
/// build_suffix_trie( sorted_dictionary, trie_nodes );
///\endcode
///
template <typename TrieType, typename NodeVector>
void build_suffix_trie(
    const TrieType&     in_trie,
    NodeVector&         out_nodes);

///@} // SuffixTriesModule
///@} // TriesModule

} // namespace nvbio

#include <nvbio/trie/suffix_trie_inl.h>
