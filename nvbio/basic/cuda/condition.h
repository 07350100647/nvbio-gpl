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
#include <thrust/device_vector.h>

namespace nvbio {
namespace cuda {

/// implements a simple inter-CTA condition variable
///
/// The condition variable is actually an integer, and the interface
/// offers the possibility to test if / wait until the variable is greater
/// than a given value.
///
/// Upon signaling, the condition variable is atomically increased.
///
struct condition
{
    /// internal constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    condition(uint32* cond) :
        m_cond( cond ) {}

    /// test the condition without waiting
    ///
    NVBIO_FORCEINLINE NVBIO_DEVICE
    bool test(const uint32 i = 1) { return *(volatile uint32*)m_cond >= i; }

    /// poll until the condition is met
    ///
    NVBIO_FORCEINLINE NVBIO_DEVICE
    void wait(const uint32 i = 1) const { while (*(volatile uint32*)m_cond < i) {} }

    /// poll until the condition is met
    ///
    NVBIO_FORCEINLINE NVBIO_DEVICE
    bool wait(const uint32 i, const uint32 n_iter) const { for (uint32 iter = 0; iter < n_iter && *(volatile uint32*)m_cond < i; ++iter) {} return *(volatile uint32*)m_cond >= i; }

    /// set the condition variable
    ///
    NVBIO_FORCEINLINE NVBIO_DEVICE
    void signal(const uint32 d = 1) { __threadfence(); atomicAdd( m_cond, d ); }

    /// set the condition variable to a specific value
    ///
    NVBIO_FORCEINLINE NVBIO_DEVICE
    void set(const uint32 i) { __threadfence(); *(volatile uint32*)m_cond = i; __threadfence(); }

    /// return the current value of the variable
    ///
    NVBIO_FORCEINLINE NVBIO_DEVICE
    uint32 value(const uint32 i = 0) { return *(volatile uint32*)m_cond; }

private:
    uint32* m_cond;
};

/// a class to view a set of conditions from the device
///
struct condition_set_view
{
    /// internal constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    condition_set_view(uint32* cond = NULL) : m_cond( cond ) {}

    /// get a condition object
    ///
    /// \param i        i-th condition
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    condition operator[] (const uint32 i) { return condition( m_cond + i ); }

private:
    uint32* m_cond;
};

/// a class to build a set of condition variables from the host
///
struct condition_set_storage
{
    /// constructor
    ///
    condition_set_storage(const uint32 count = 0) : m_conds( count, 0u ) {}

    /// resize
    ///
    void resize(const uint32 count)
    {
        m_conds.resize( count );
        thrust::fill( m_conds.begin(), m_conds.end(), 0 );
    }

    /// return a condition variable object
    ///
    condition_set_view get()
    {
        uint32* conds = thrust::raw_pointer_cast( &m_conds.front() );
        return condition_set_view( conds );
    }

    /// set the initial state of the conditions
    ///
    void set(const uint32 value) { thrust::fill( m_conds.begin(), m_conds.end(), value ); }

private:
    thrust::device_vector<uint32> m_conds;
};

} // namespace cuda
} // namespace nvbio
