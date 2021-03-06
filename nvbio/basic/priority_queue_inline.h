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

namespace nvbio {

// constructor
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE priority_queue<Key,Container,Compare>::priority_queue(const Container cont, const Compare cmp)
    : m_size(0), m_queue(cont), m_cmp(cmp)
{
    // TODO: heapify if not empty!
    assert( m_queue.empty() == true );
}

// is queue empty?
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool priority_queue<Key,Container,Compare>::empty() const
{
    return m_size == 0;
}

// return queue size
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE uint32 priority_queue<Key,Container,Compare>::size() const
{
    return m_size;
}

// push an element
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void priority_queue<Key,Container,Compare>::push(const Key key)
{
    // check whether the queue is full
    m_size++;
    m_queue.resize( m_size+1 ); // we need one more entry for things to work out

	uint32 r = m_size;
	while (r > 1) // sift up new item
	{		
		const uint32 p = r/2;
        if (! m_cmp( m_queue[p], key )) // in proper order
			break;

        m_queue[r] = m_queue[p]; // else swap with parent
		r = p;
	}
    m_queue[r] = key; // insert new item at final location
}

// pop an element
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void priority_queue<Key,Container,Compare>::pop()
{
	Key dn = m_queue[m_size--];  // last item in queue
    m_queue.resize( m_size+1 );  // we need one more entry for things to work out

    uint32 p = 1;                // p points to item out of position
	uint32 r = p << 1;           // left child of p

	while (r <= m_size) // while r is still within the heap
	{
		// set r to smaller child of p
		if (r < m_size  && m_cmp( m_queue[r], m_queue[r+1] )) r++;
		if (! m_cmp( dn, m_queue[r] ))	// in proper order
			break;

		m_queue[p] = m_queue[r];    // else swap with child
		p = r;                      // advance pointers
		r = p<<1;
	}
    m_queue[p] = m_queue[m_size+1]; // insert last item in proper place
}

// top of the queue
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Key& priority_queue<Key,Container,Compare>::top()
{
    return m_queue[1];
}

// top of the queue
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Key priority_queue<Key,Container,Compare>::top() const
{
    return m_queue[1];
}

// return the i-th element in the heap
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE const Key& priority_queue<Key,Container,Compare>::operator[] (const uint32 i) const
{
    return m_queue[1+i];
}

// clear the queue
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void priority_queue<Key,Container,Compare>::clear()
{
    m_size = 0;
    m_queue.resize(0);
}

namespace priqueue
{
    // returns the index of the first node at the same level as node i
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32 leftmost(uint32 i)
    {
        const uint32 msb = 1u << (sizeof(i) * 8u - 1u);
        return msb >> lzc(i);
    }

    // returns the width of the tree at the level of node i
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32 width(uint32 i)
    {
        return 1u << (sizeof(i) * 8u - lzc(i) - 1u);
    }

    // returns the parent node of i
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32 parent(uint32 i)
    {
        return i >> 1u;
    }
}

// locate the largest element v such that v <= x; return end() if no
// such element exists
//
template <typename Key, typename Container, typename Compare>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE priority_queue<Key,Container,Compare>::iterator
priority_queue<Key,Container,Compare>::upper_bound(const Key& x)
{
    uint32 max_i = 0;
    Key    max;
#if 1
    for (uint32 j = 1; j < size()+1; ++j)
    {
        if (!m_cmp( x, m_queue[j] )) // m_queue[j] <= x
        {
            if (max_i == 0 || !m_cmp( m_queue[j], max )) // m_queue[j] >= max
            {
                // found a new maximum
                max = m_queue[j];
                max_i = j;
            }
        }
    }

    if (max_i == 0)
        return end();

    return begin() + max_i-1;
#else
    uint32 i;
    bool   stop;

    // start with the leftmost leaf node
    i = priqueue::leftmost(m_size);
    stop = false;

    while(!stop && i > 0)
    {
        const uint32 num_nodes = nvbio::min( priqueue::width(i), m_size - i );

        // visit all nodes at the same level of i
        //stop = true;
        stop = (num_nodes == priqueue::width(i) ? true : false);
        for(uint32 j = i; j < i + num_nodes; j++)
        {
            if (!m_cmp( x, m_queue[j] )) // m_queue[j] <= x
            {
                // if at least one of the nodes at this level is <= x, then visit the level above
                // (this is overly conservative: we can skip the parent if one of the children is > x)
                stop = false;

                if (max_i == 0 || !m_cmp( m_queue[j], max )) // m_queue[j] >= max
                {
                    // found a new maximum
                    if (j > max_i)
                    {
                        max = m_queue[j];
                        max_i = j;
                    }
                }
            }
        }

        // go up one level
        i = priqueue::parent(i);
    }

    if (max_i == 0)
        return end();

    return begin() + max_i-1;
#endif
}

} // namespace nvbio
