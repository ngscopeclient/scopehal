/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of AlignedAllocator
 */

#ifndef AlignedAllocator_h
#define AlignedAllocator_h

#ifdef _WIN32
#include <windows.h>
#endif

/**
	@brief Aligned memory allocator for STL containers

	Based on https://devblogs.microsoft.com/cppblog/the-mallocator/
 */
template <class T, size_t alignment>
class AlignedAllocator
{
public:

	//Standard typedefs
	typedef T* pointer;
	typedef const T* const_pointer;
	typedef T& reference;
	typedef const T& const_reference;
	typedef T value_type;
	typedef size_t size_type;
	typedef ptrdiff_t difference_type;

	//Overloads in case somebody overloaded the unary operator&()
	//(which is pretty weird but the spec allows it)
	T* address(T& rhs)
	{ return &rhs; }

	const T* address(T& rhs) const
	{ return &rhs; }

	size_t max_size() const
	{ return (static_cast<size_t>(0) - static_cast<size_t>(1)) / sizeof(T); }

	//RTTI and construction helpers
	template<typename U>
	struct rebind
	{
		typedef AlignedAllocator<U, alignment> other;
	};

	bool operator!=(const AlignedAllocator& other) const
	{ return !(*this == other); }

	//Look at that, a placement new! First time I've ever used one.
	void construct(T* const p, const T& t) const
	{ new( static_cast<void*>(p) ) T(t); }

	void destroy(T* const p) const
	{ p->~T(); }

	//Check if this allocator is functionally equivalent to another
	//We have no member variables, so all objects of the same type are equivalent
	bool operator== (const AlignedAllocator& /* unused */) const
	{ return true; }

	//Default ctors, do nothing
	AlignedAllocator()
	{}

	AlignedAllocator(const AlignedAllocator& /* unused */)
	{}

	template<typename U>
	AlignedAllocator(const AlignedAllocator<U, alignment>&)
	{}

	~AlignedAllocator()
	{}

	//Now for the fun part
	T* allocate(const size_t n) const
	{
		//Fail if we got an invalid size
		if(n == 0)
			return NULL;
		if(n > max_size())
			throw std::length_error("AlignedAllocator<T>::allocate(): requested size is too large, integer overflow?");

		//Do the actual allocation
		#ifdef _WIN32
			T* ret = static_cast<T*>(_aligned_malloc(n*sizeof(T), alignment));
		#else
			T* ret = static_cast<T*>(aligned_alloc(alignment, n*sizeof(T)));
		#endif

		//Error check
		if(ret == NULL)
			throw std::bad_alloc();

		return ret;
	}

	void deallocate(T* const p, const size_t /*unused*/) const
	{
		#ifdef _WIN32
			_aligned_free(p);
		#else
			free(p);
		#endif
	}

	//convenience wrapper
	void deallocate(T* const p) const
	{ deallocate(p, 1); }

	//Not quite sure what this is for but apparently we need it?
	template<typename U>
	T* allocate(const size_t n, const U* /* const hint */ const)
	{ return allocate(n); }

	//Disallow assignment
	AlignedAllocator& operator=(const AlignedAllocator&) = delete;
};

//Global allocator for AVX helpers
extern AlignedAllocator<float, 32> g_floatVectorAllocator;

#endif
