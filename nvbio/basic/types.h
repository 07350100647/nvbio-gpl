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

#include <assert.h>
#include <vector_types.h>

#ifdef __CUDACC__
    #define NVBIO_HOST_DEVICE __host__ __device__
    #define NVBIO_HOST   __host__
    #define NVBIO_DEVICE __device__
#else
    #define NVBIO_HOST_DEVICE 
    #define NVBIO_HOST
    #define NVBIO_DEVICE
#endif

#ifdef __CUDA_ARCH__
#define NVBIO_RESTRICT __restrict__
#define NVBIO_SHARED   __shared__
#else
#define NVBIO_RESTRICT
#define NVBIO_SHARED
#endif

#if defined(__CUDA_ARCH__) && __CUDA_ARCH__ > 0
#define NVBIO_DEVICE_COMPILATION
#endif

#define NVBIO_API_CS
#define NVBIO_API_SS

#ifdef WIN32
#define NVBIO_FORCEINLINE __forceinline
#else
#define NVBIO_FORCEINLINE __inline__
#endif

#if defined(NVBIO_CUDA_DEBUG)
#define NVBIO_CUDA_DEBUG_STATEMENT(x) x
#else
#define NVBIO_CUDA_DEBUG_STATEMENT(x)
#endif

#if defined(NVBIO_CUDA_ASSERTS)
  // the following trickery eliminates the "controlling expression is constant" warning from nvcc when doing assert(!"some string")
  #define NVBIO_CUDA_ASSERT(x) { const bool __yes = true; assert(x && __yes); }
  #define NVBIO_CUDA_ASSERT_IF(cond, x, ...) if ((cond) && !(x)) {printf(__VA_ARGS__); NVBIO_CUDA_ASSERT(x); }
  #define NVBIO_CUDA_DEBUG_ASSERT(x,...) if (!(x)) { printf(__VA_ARGS__); NVBIO_CUDA_ASSERT(x); }
#elif defined(NVBIO_CUDA_NON_BLOCKING_ASSERTS) // !defined(NVBIO_CUDA_ASSERTS)
  #define NVBIO_CUDA_ASSERT(x)
  #define NVBIO_CUDA_ASSERT_IF(cond, x, ...) if ((cond) && !(x)) { printf(__VA_ARGS__); }
  #define NVBIO_CUDA_DEBUG_ASSERT(x,...) if (!(x)) { printf(__VA_ARGS__); }
#else // !defined(NVBIO_NON_BLOCKING_ASSERTS) && !defined(NVBIO_CUDA_ASSERTS)
  #define NVBIO_CUDA_ASSERT(x)
  #define NVBIO_CUDA_ASSERT_IF(cond, x, ...)
  #define NVBIO_CUDA_DEBUG_ASSERT(x,...)
#endif

#if defined(NVBIO_CUDA_DEBUG)
  #define NVBIO_CUDA_DEBUG_PRINT(...) printf(__VA_ARGS__)
  #define NVBIO_CUDA_DEBUG_PRINT_IF(cond,...) if (cond) printf(__VA_ARGS__)
  #define NVBIO_CUDA_DEBUG_SELECT(debug_val,normal_val) (debug_val)
#else // !defined(NVBIO_CUDA_DEBUG)
  #define NVBIO_CUDA_DEBUG_PRINT(...)
  #define NVBIO_CUDA_DEBUG_PRINT_IF(cond,...)
  #define NVBIO_CUDA_DEBUG_SELECT(debug_val,normal_val) (normal_val)
#endif

#if defined(NVBIO_CUDA_DEBUG)
  #if defined(NVBIO_CUDA_ASSERTS)
    #define NVBIO_CUDA_DEBUG_CHECK_IF(cond, check,...) if ((cond) && (!(check))) { printf(__VA_ARGS__); assert(check); }
  #else // !defined(NVBIO_CUDA_ASSERTS)
    #define NVBIO_CUDA_DEBUG_CHECK_IF(cond, check,...) if ((cond) && (!(check))) printf(__VA_ARGS__)
  #endif
#else // !defined(NVBIO_CUDA_DEBUG)
#define NVBIO_CUDA_DEBUG_CHECK_IF(cond, check,...)
#endif

#if defined(__CUDACC__)
#define NVBIO_HOST_DEVICE_TEMPLATE \
#pragma hd_warning_disable
#else
#define NVBIO_HOST_DEVICE_TEMPLATE
#endif

#ifdef WIN32
#define WINONLY(x) x
#else
#define WINONLY(x)
#endif

// NVBIO_VAR_UNUSED can be prepended to a variable to turn off unused variable warnings
// this should only be used when the variable actually is used and the warning is wrong
// (e.g., variables which are used only as template parameters for kernel launches)
#if defined(__GNUC__)
#define NVBIO_VAR_UNUSED __attribute__((unused))
#else
#define NVBIO_VAR_UNUSED
#endif

namespace nvbio {

typedef unsigned long long  uint64;
typedef unsigned int        uint32;
typedef unsigned short      uint16;
typedef unsigned char       uint8;
typedef long long           int64;
typedef int                 int32;
typedef short               int16;
typedef char                int8;

/// a null type, useful to represent unbound template arguments
///
struct null_type {};

/// a meta-function to convert a type to const
///
template <typename T> struct to_const           { typedef T type; };
template <typename T> struct to_const<T&>       { typedef const T& type; };
template <typename T> struct to_const<T*>       { typedef const T* type; };
template <typename T> struct to_const<const T&> { typedef const T& type; };
template <typename T> struct to_const<const T*> { typedef const T* type; };

/// a meta-function to return the reference subtype of a given container
///
template <typename T> struct reference_subtype                    { typedef typename T::reference type; };
template <typename T> struct reference_subtype<T*>                { typedef T&                    type; };
template <typename T> struct reference_subtype<const T*>          { typedef const T&              type; };
template <>           struct reference_subtype<null_type>         { typedef null_type             type; };

/// a meta-function to return the view subtype of a given container
///
template <typename T> struct device_view_subtype                   { typedef typename T::device_view_type type; };
template <>           struct device_view_subtype<null_type>        { typedef null_type                    type; };
template <typename T> struct device_view_subtype<const T*>         { typedef const T*                     type; };
template <typename T> struct device_view_subtype<T*>               { typedef T*                           type; };

/// a meta-function to return the view subtype of a given container
///
template <typename T> struct plain_view_subtype                   { typedef typename T::plain_view_type  type; };
template <>           struct plain_view_subtype<null_type>        { typedef null_type                    type; };
template <typename T> struct plain_view_subtype<const T*>         { typedef const T*                     type; };
template <typename T> struct plain_view_subtype<T*>               { typedef T*                           type; };

/// a meta-function to convert potentially unsigned integrals to their signed counter-part
///
template <typename T> struct signed_type {};
template <> struct signed_type<uint32> { typedef int32 type; };
template <> struct signed_type<uint64> { typedef int64 type; };
template <> struct signed_type<int32>  { typedef int32 type; };
template <> struct signed_type<int64>  { typedef int64 type; };

/// a meta-function to convert potentially signed integrals to their unsigned counter-part
///
template <typename T> struct unsigned_type {};
template <> struct unsigned_type<uint32> { typedef uint32 type; };
template <> struct unsigned_type<uint64> { typedef uint64 type; };
template <> struct unsigned_type<int32>  { typedef uint32 type; };
template <> struct unsigned_type<int64>  { typedef uint64 type; };

/// a utility to perform binary casts between different types
///
template <typename Out, typename In>
union BinaryCast
{
    In  in;
    Out out;
};

/// a utility to perform binary casts between different types
///
template <typename Out, typename In>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Out binary_cast(const In in)
{
#if defined(__CUDA_ARCH__)
    return reinterpret_cast<const Out&>(in);
#else
    BinaryCast<Out,In> inout;
    inout.in = in;
    return inout.out;
#endif
}

template <uint32 C>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool is_pow2() { return (C & (C-1)) == 0u; }

template <typename T1, typename T2>
struct same_type { static const bool pred = false; };

template <typename T>
struct same_type<T,T> { static const bool pred = true; };

template <typename A, typename B>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool equal() { return same_type<A,B>::pred; }

// round up to next multiple of N, where N is a power of 2.
template <uint32 N, typename I> NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
I align(const I a) { return (N > 1) ? I(a + N-1) & I(~(N-1)) : a; }

// round down to previous multiple of N, where N is a power of 2.
template <uint32 N, typename I> NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
I align_down(const I a) { return (N > 1) ? I((a / N) * N) : a; }

/// a helper struct to switch at compile-time between two types
///
template <typename A, typename B, uint32 N> struct binary_switch { typedef B type; };

/// a helper struct to switch at compile-time between two types
///
template <typename A, typename B> struct binary_switch<A,B,0> { typedef A type; };

} // namespace nvbio
