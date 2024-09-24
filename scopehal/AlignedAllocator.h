/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@ingroup core
 */

#ifndef AlignedAllocator_h
#define AlignedAllocator_h

#ifdef _WIN32
#include <windows.h>
#endif

/**
	@brief Aligned memory allocator for STL containers

	Based on https://devblogs.microsoft.com/cppblog/the-mallocator/

	@ingroup core
 */
template <class T, size_t alignment>
class AlignedAllocator
{
public:

	///@brief Pointer to the allocated type
	typedef T* pointer;

	///@brief Const pointer to the allocated type
	typedef const T* const_pointer;

	///@brief Reference to the allocated type
	typedef T& reference;

	///@brief Const reference to the allocated type
	typedef const T& const_reference;

	///@brief The allocated type
	typedef T value_type;

	///@brief Type of the size of an allocated object
	typedef size_t size_type;

	///@brief Type of the difference between two allocated pointers
	typedef ptrdiff_t difference_type;

	/**
		@brief Get the address of an object

		Overloaded in case somebody overloaded the unary operator&()
		(which is pretty weird but the spec allows it)

		@param rhs	The object to get the address of
	 */
	T* address(T& rhs)
	{ return &rhs; }

	/**
		@brief Get the address of an object

		Overloaded in case somebody overloaded the unary operator&()
		(which is pretty weird but the spec allows it)

		@param rhs	The object to get the address of
	 */
	const T* address(T& rhs) const
	{ return &rhs; }

	/**
		@brief Get the max possible allocation size the allocator supports

		(Does not necessarily mean that we have enough RAM to do so, only enough address space)
	 */
	size_t max_size() const
	{ return (static_cast<size_t>(0) - static_cast<size_t>(1)) / sizeof(T); }

	///@brief Rebind to a different type of allocator
	template<typename U>
	struct rebind
	{
		typedef AlignedAllocator<U, alignment> other;
	};

	/**
		@brief Check if two allocators are the same

		@param other	The other object
	 */
	bool operator!=(const AlignedAllocator& other) const
	{ return !(*this == other); }

	//Look at that, a placement new! First time I've ever used one.
	/**
		@brief Construct an object in-place given a reference one

		@param p	Destination object
		@param t	Source object
	 */
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

	/**
		@brief Allocate a block of memory

		@param n	Size in bytes (internally rounded up to our alignment)
	 */
	T* allocate(size_t n) const
	{
		//Fail if we got an invalid size
		if(n == 0)
			return NULL;
		if(n > max_size())
			throw std::length_error("AlignedAllocator<T>::allocate(): requested size is too large, integer overflow?");

		//Round size up to multiple of alignment
		if( (n % alignment) != 0)
		{
			n |= (alignment - 1);
			n ++;
		}

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

	/**
		@brief	Free a block of memory

		@param p		Block to free
		@param unused	Size of block (ignored)
	 */
	void deallocate(T* const p, [[maybe_unused]] const size_t unused) const
	{
#ifdef _WIN32
		_aligned_free(p);
#else
		free(p);
#endif
	}

	/**
		@brief Free a single object

		@param p	Object to free
	 */
	void deallocate(T* const p) const
	{ deallocate(p, 1); }

	//Not quite sure what this is for but apparently we need it?
	/**
		@brief Allocate an object

		@param n	Size in bytes
		@param hint	Ignored
	 */
	template<typename U>
	T* allocate(const size_t n, [[maybe_unused]] const U* const hint)
	{ return allocate(n); }

	//Disallow assignment
	AlignedAllocator& operator=(const AlignedAllocator&) = delete;
};

#endif
