/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of AcceleratorBuffer
 */
#ifndef AcceleratorBuffer_h
#define AcceleratorBuffer_h

#include "AlignedAllocator.h"
#include "QueueManager.h"

#ifdef _WIN32
#undef MemoryBarrier
#endif

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <type_traits>

extern uint32_t g_vkPinnedMemoryType;
extern uint32_t g_vkLocalMemoryType;
extern std::shared_ptr<vk::raii::Device> g_vkComputeDevice;
extern std::unique_ptr<vk::raii::CommandBuffer> g_vkTransferCommandBuffer;
extern std::shared_ptr<QueueHandle> g_vkTransferQueue;
extern std::mutex g_vkTransferMutex;

extern bool g_hasDebugUtils;

template<class T>
class AcceleratorBuffer;

template<class T>
class AcceleratorBufferIterator
{
public:
	using value_type = T;
	using iterator_category = std::forward_iterator_tag;
	using difference_type = std::ptrdiff_t;
	using pointer = T*;
	using reference = T&;

	AcceleratorBufferIterator(AcceleratorBuffer<T>& buf, size_t i)
	: m_index(i)
	, m_buf(buf)
	{}

	T& operator*()
	{ return m_buf[m_index]; }

	size_t GetIndex() const
	{ return m_index; }

	bool operator!=(AcceleratorBufferIterator<T>& it)
	{
		//TODO: should we check m_buf equality too?
		//Will slow things down, but be more semantically correct. Does anything care?
		return (m_index != it.m_index);
	}

	AcceleratorBufferIterator<T>& operator++()
	{
		m_index ++;
		return *this;
	}

protected:
	size_t m_index;
	AcceleratorBuffer<T>& m_buf;
};

template<class T>
std::ptrdiff_t operator-(const AcceleratorBufferIterator<T>& a, const AcceleratorBufferIterator<T>& b)
{ return a.GetIndex() - b.GetIndex(); }

/**
	@brief A buffer of memory which may be used by GPU acceleration

	At any given point in time the buffer may exist as a single copy on the CPU, a single copy on the GPU, or
	mirrored buffers on both sides.

	Hints can be provided to the buffer about future usage patterns to optimize storage location for best performance.

	This buffer generally provides std::vector semantics, but does *not* initialize memory or call constructors on
	elements when calling resize() or reserve() unless the element type is not trivially copyable.
	All locations not explicitly written to have undefined values. Most notably, allocated buffer space between size()
	and capacity() is undefined and its value may not be coherent between CPU and GPU view of the buffer.

	If the element type is not trivially copyable, the data cannot be shared with the GPU. This class still supports
	non-trivially-copyable types as a convenience for working with waveforms on the CPU.
 */
template<class T>
class AcceleratorBuffer
{
protected:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Allocator for CPU-only memory

	AlignedAllocator<T, 32> m_cpuAllocator;

public:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Buffer types

	/**
		@brief Attributes that a memory buffer can have
	 */
	enum MemoryAttributes
	{
		//Location of the memory
		MEM_ATTRIB_CPU_SIDE			= 0x1,
		MEM_ATTRIB_GPU_SIDE			= 0x2,

		//Reachability
		MEM_ATTRIB_CPU_REACHABLE	= 0x4,
		MEM_ATTRIB_GPU_REACHABLE	= 0x8,

		//Speed
		MEM_ATTRIB_CPU_FAST			= 0x10,
		MEM_ATTRIB_GPU_FAST			= 0x20
	};

	/**
		@brief Types of memory buffer
	 */
	enum MemoryType
	{
		//Pointer is invalid
		MEM_TYPE_NULL = 0,

		//Memory is located on the CPU but backed by a file and may get paged out
		MEM_TYPE_CPU_PAGED =
			MEM_ATTRIB_CPU_SIDE | MEM_ATTRIB_CPU_REACHABLE,

		//Memory is located on the CPU but not pinned, or otherwise accessible to the GPU
		MEM_TYPE_CPU_ONLY =
			MEM_ATTRIB_CPU_SIDE | MEM_ATTRIB_CPU_REACHABLE | MEM_ATTRIB_CPU_FAST,

		//Memory is located on the CPU, but can be accessed by the GPU.
		//Fast to access from the CPU, but accesses from the GPU require PCIe DMA and are slow.
		MEM_TYPE_CPU_DMA_CAPABLE =
			MEM_ATTRIB_CPU_SIDE | MEM_ATTRIB_CPU_REACHABLE | MEM_ATTRIB_CPU_FAST | MEM_ATTRIB_GPU_REACHABLE,

		//Memory is located on the GPU and cannot be directly accessed by the CPU
		MEM_TYPE_GPU_ONLY =
			MEM_ATTRIB_GPU_SIDE | MEM_ATTRIB_GPU_REACHABLE | MEM_ATTRIB_GPU_FAST,

		//Memory is located on the GPU, but can be accessed by the CPU.
		//Fast to access from the GPU, but accesses from the CPU require PCIe DMA and are slow.
		MEM_TYPE_GPU_DMA_CAPABLE =
			MEM_ATTRIB_GPU_SIDE | MEM_ATTRIB_GPU_REACHABLE | MEM_ATTRIB_GPU_FAST | MEM_ATTRIB_CPU_REACHABLE
	};

protected:

	/**
		@brief Returns true if the given buffer type can be reached from the CPU
	 */
	bool IsReachableFromCpu(MemoryType mt)
	{ return (mt & MEM_ATTRIB_CPU_REACHABLE) != 0; }

	/**
		@brief Returns true if the given buffer type can be reached from the GPU
	 */
	bool IsReachableFromGpu(MemoryType mt)
	{ return (mt & MEM_ATTRIB_GPU_REACHABLE) != 0; }

	/**
		@brief Returns true if the given buffer type is fast to access from the CPU
	 */
	bool IsFastFromCpu(MemoryType mt)
	{ return (mt & MEM_ATTRIB_CPU_FAST) != 0; }

	/**
		@brief Returns true if the given buffer type is fast to access from the GPU
	 */
	bool IsFastFromGpu(MemoryType mt)
	{ return (mt & MEM_ATTRIB_GPU_FAST) != 0; }

	///@brief Type of the CPU-side buffer
	MemoryType m_cpuMemoryType;

	///@brief Type of the GPU-side buffer
	MemoryType m_gpuMemoryType;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// The actual memory buffers

	///@brief CPU-side mapped pointer
	T* m_cpuPtr;

	///@brief CPU-side physical memory
	std::unique_ptr<vk::raii::DeviceMemory> m_cpuPhysMem;

	///@brief GPU-side physical memory
	std::unique_ptr<vk::raii::DeviceMemory> m_gpuPhysMem;

	///@brief Buffer object for CPU-side memory
	std::unique_ptr<vk::raii::Buffer> m_cpuBuffer;

	///@brief Buffer object for GPU-side memory
	std::unique_ptr<vk::raii::Buffer> m_gpuBuffer;

	///@brief True if we have only one piece of physical memory accessible from both sides
	bool m_buffersAreSame;

	///@brief True if m_cpuPtr contains stale data (m_gpuPhysMem has been modified and they point to different memory)
	bool m_cpuPhysMemIsStale;

	///@brief True if m_gpuPhysMem contains stale data (m_cpuPtr has been modified and they point to different memory)
	bool m_gpuPhysMemIsStale;

	///@brief File handle used for MEM_TYPE_CPU_PAGED
#ifndef _WIN32
	int m_tempFileHandle;
#endif

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Iteration

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Sizes of buffers

	///@brief Size of the allocated memory space (may be larger than m_size)
	size_t m_capacity;

	///@brief Size of the memory actually being used
	size_t m_size;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Hint configuration
public:
	enum UsageHint
	{
		HINT_NEVER,
		HINT_UNLIKELY,
		HINT_LIKELY
	};

protected:
	///@brief Hint about how likely future CPU access is
	UsageHint m_cpuAccessHint;

	///@brief Hint about how likely future GPU access is
	UsageHint m_gpuAccessHint;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Construction / destruction
public:

	/**
		@brief Creates a new AcceleratorBuffer with no content
	 */
	AcceleratorBuffer(const std::string& name = "")
		: m_cpuMemoryType(MEM_TYPE_NULL)
		, m_gpuMemoryType(MEM_TYPE_NULL)
		, m_cpuPtr(nullptr)
		, m_gpuPhysMem(nullptr)
		, m_buffersAreSame(false)
		, m_cpuPhysMemIsStale(false)
		, m_gpuPhysMemIsStale(false)
		#ifndef _WIN32
		, m_tempFileHandle(0)
		#endif
		, m_capacity(0)
		, m_size(0)
		, m_cpuAccessHint(HINT_LIKELY)	//default access hint: CPU-side pinned memory
		, m_gpuAccessHint(HINT_UNLIKELY)
		, m_name(name)
	{
		//non-trivially-copyable types can't be copied to GPU
		if(!std::is_trivially_copyable<T>::value)
			m_gpuAccessHint = HINT_NEVER;
	}

	~AcceleratorBuffer()
	{
		FreeCpuBuffer();
		FreeGpuBuffer();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// General accessors
public:

	/**
		@brief Returns the actual size of the container (may be smaller than what was allocated)
	 */
	size_t size() const
	{ return m_size; }

	/**
		@brief Returns the allocated size of the container
	 */
	size_t capacity() const
	{ return m_capacity; }

	/**
		@brief Returns the total reserved CPU memory, in bytes
	 */
	size_t GetCpuMemoryBytes() const
	{
		if(m_cpuMemoryType == MEM_TYPE_NULL)
			return 0;
		else
			return m_capacity * sizeof(T);
	}

	/**
		@brief Returns the total reserved GPU memory, in bytes
	 */
	size_t GetGpuMemoryBytes() const
	{
		if(m_gpuMemoryType == MEM_TYPE_NULL)
			return 0;
		else
			return m_capacity * sizeof(T);
	}

	/**
		@brief Returns true if the container is empty
	 */
	bool empty() const
	{ return (m_size == 0); }

	/**
		@brief Returns true if the CPU-side buffer is stale
	 */
	bool IsCpuBufferStale() const
	{ return m_cpuPhysMemIsStale; }

	/**
		@brief Returns true if the GPU-side buffer is stale
	 */
	bool IsGpuBufferStale() const
	{ return m_gpuPhysMemIsStale; }

	/**
		@brief Returns true if there is currently a CPU-side buffer
	 */
	bool HasCpuBuffer() const
	{ return (m_cpuPtr != nullptr); }

	/**
		@brief Returns true if there is currently a GPU-side buffer
	 */
	bool HasGpuBuffer() const
	{ return (m_gpuPhysMem != nullptr); }

	/**
		@brief Returns true if the object contains only a single buffer
	 */
	bool IsSingleSharedBuffer() const
	{ return m_buffersAreSame; }

	/**
		@brief Returns the preferred buffer for GPU-side access.

		This is the GPU buffer if we have one, otherwise the CPU buffer.
	 */
	vk::Buffer GetBuffer()
	{
		if(m_gpuBuffer != nullptr)
			return **m_gpuBuffer;
		else
			return **m_cpuBuffer;
	}

	/**
		@brief Gets a pointer to the CPU-side buffer
	 */
	T* GetCpuPointer()
	{ return m_cpuPtr; }

	/**
		@brief Returns a vk::DescriptorBufferInfo suitable for binding this object to
	 */
	vk::DescriptorBufferInfo GetBufferInfo()
	{
		return vk::DescriptorBufferInfo(
			GetBuffer(),
			0,
			m_size * sizeof(T));
	}

	/**
		@brief Change the usable size of the container
	 */
	void resize(size_t size)
	{
		//Need to grow?
		if(size > m_capacity)
		{
			//Default to doubling in size each time to avoid excessive copying.
			if(m_capacity == 0)
				reserve(size);
			else if(size > m_capacity*2)
				reserve(size);
			else
				reserve(m_capacity * 2);
		}

		//Update our size
		m_size = size;
	}

	/**
		@brief Resize the container to be empty (but don't free memory)
	 */
	void clear()
	{ resize(0); }

	/**
		@brief Reallocates buffers so that at least size elements of storage are available
	 */
	void reserve(size_t size)
	{
		if(size >= m_capacity)
			Reallocate(size);
	}

	/**
		@brief Frees unused memory so that m_size == m_capacity
	 */
	void shrink_to_fit()
	{
		if(m_size != m_capacity)
			Reallocate(m_size);
	}

	/**
		@brief Copies our content from another AcceleratorBuffer
	 */
	 __attribute__((noinline))
	void CopyFrom(const AcceleratorBuffer<T>& rhs)
	{
		//Copy placement hints from the other instance, then resize to match
		SetCpuAccessHint(rhs.m_cpuAccessHint);
		SetGpuAccessHint(rhs.m_gpuAccessHint, true);
		resize(rhs.m_size);

		//Valid data CPU side? Copy it to here
		if(rhs.HasCpuBuffer() && !rhs.m_cpuPhysMemIsStale)
		{
			//non-trivially-copyable types have to be copied one at a time
			if(!std::is_trivially_copyable<T>::value)
			{
				for(size_t i=0; i<m_size; i++)
					m_cpuPtr[i] = rhs.m_cpuPtr[i];
			}

			//Trivially copyable types can be done more efficiently in a block
			else
				memcpy(m_cpuPtr, rhs.m_cpuPtr, m_size * sizeof(T));
		}
		m_cpuPhysMemIsStale = rhs.m_cpuPhysMemIsStale;

		//Valid data GPU side? Copy it to here
		if(rhs.HasGpuBuffer() && !rhs.m_gpuPhysMemIsStale)
		{
			std::lock_guard<std::mutex> lock(g_vkTransferMutex);

			//Make the transfer request
			g_vkTransferCommandBuffer->begin({});
			vk::BufferCopy region(0, 0, m_size * sizeof(T));
			g_vkTransferCommandBuffer->copyBuffer(**rhs.m_gpuBuffer, **m_gpuBuffer, {region});
			g_vkTransferCommandBuffer->end();

			//Submit the request and block until it completes
			g_vkTransferQueue->SubmitAndBlock(*g_vkTransferCommandBuffer);
		}
		m_gpuPhysMemIsStale = rhs.m_gpuPhysMemIsStale;
	}

protected:

	/**
		@brief Reallocates the buffer so that it contains exactly size elements
	 */
	__attribute__((noinline))
	void Reallocate(size_t size)
	{
		if(size == 0)
			return;

		/*
			If we are a bool[] or similar one-byte type, we are likely going to be accessed from the GPU via a uint32
			descriptor for at least some shaders (such as rendering).

			Round our actual allocated size to the next multiple of 4 bytes. The padding values are unimportant as the
			bytes are never written, and the data read from the high bytes in the uint32 is discarded by the GPU.
			We just need to ensure the memory is allocated so the 32-bit read is legal to perform.
		 */
		if( (sizeof(T) == 1) && (m_gpuAccessHint != HINT_NEVER) )
		{
			if(size & 3)
				size = (size | 3) + 1;
		}

		//If we do not anticipate using the data on the CPU, we shouldn't waste RAM.
		//Allocate a GPU-local buffer, copy data to it, then free the CPU-side buffer
		if(m_cpuAccessHint == HINT_NEVER)
		{
			PrepareForGpuAccess();
			FreeCpuBuffer();
		}

		else
		{
			//Resize CPU memory
			//TODO: optimization, when expanding a MEM_TYPE_CPU_PAGED we can just enlarge the file
			//and not have to make a new temp file and copy the content
			if(m_cpuPtr != nullptr)
			{
				//Save the old pointer
				auto pOld = m_cpuPtr;
				auto pOldPin = std::move(m_cpuPhysMem);
				auto type = m_cpuMemoryType;

				//Allocate the new buffer
				AllocateCpuBuffer(size);

				//If CPU-side data is valid, copy existing data over.
				//New pointer is still valid in this case.
				if(!m_cpuPhysMemIsStale)
				{
					//non-trivially-copyable types have to be copied one at a time
					if(!std::is_trivially_copyable<T>::value)
					{
						for(size_t i=0; i<m_size; i++)
							m_cpuPtr[i] = std::move(pOld[i]);
					}

					//Trivially copyable types can be done more efficiently in a block
					//gcc warns about this even though we only call this code if the type is trivially copyable,
					//so disable the warning.
					else
					{
						#pragma GCC diagnostic push
						#pragma GCC diagnostic ignored "-Wclass-memaccess"

						memcpy(m_cpuPtr, pOld, m_size * sizeof(T));

						#pragma GCC diagnostic pop
					}
				}

				//If CPU-side data is stale, just allocate the new buffer but leave it as stale
				//(don't do a potentially unnecessary copy from the GPU)

				//Now we're done with the old pointer so get rid of it
				FreeCpuPointer(pOld, pOldPin, type, m_capacity);
			}

			//Allocate new CPU memory, replacing our current (null) pointer
			else
			{
				AllocateCpuBuffer(size);

				//If we already had GPU-side memory containing data, then the new CPU-side buffer is stale
				//until we copy stuff over to it
				if(m_gpuPhysMem != nullptr)
					m_cpuPhysMemIsStale = true;
			}
		}

		//We're expecting to use data on the GPU, so prepare to do stuff with it
		if(m_gpuAccessHint != HINT_NEVER)
		{
			//If GPU access is unlikely, we probably want to just use pinned memory.
			//If available, mark buffers as the same, and free any existing GPU buffer we might have
			if( (m_gpuAccessHint == HINT_UNLIKELY) && (m_cpuMemoryType == MEM_TYPE_CPU_DMA_CAPABLE) )
				FreeGpuBuffer();

			//Nope, we need to allocate dedicated GPU memory
			else
			{
				//If we have an existing buffer with valid content, save it and copy content over
				if( (m_gpuPhysMem != nullptr) && !m_gpuPhysMemIsStale && (m_size != 0))
				{
					auto pOld = std::move(m_gpuPhysMem);
					//auto type = m_gpuMemoryType;
					auto bOld = std::move(m_gpuBuffer);

					AllocateGpuBuffer(size);

					std::lock_guard<std::mutex> lock(g_vkTransferMutex);

					//Make the transfer request
					g_vkTransferCommandBuffer->begin({});
					vk::BufferCopy region(0, 0, m_size * sizeof(T));
					g_vkTransferCommandBuffer->copyBuffer(**bOld, **m_gpuBuffer, {region});
					g_vkTransferCommandBuffer->end();

					//Submit the request and block until it completes
					g_vkTransferQueue->SubmitAndBlock(*g_vkTransferCommandBuffer);

					//make sure buffer is freed before underlying physical memory (pOld) goes out of scope
					bOld = nullptr;
				}

				//Nope, just allocate a new buffer
				else
				{
					AllocateGpuBuffer(size);

					//If we already had CPU-side memory containing data, then the new GPU-side buffer is stale
					//until we copy stuff over to it.
					//Special case: if m_size is 0 (newly allocated buffer) we're not stale yet
					if( (m_cpuPhysMem != nullptr) && (m_size != 0) )
						m_gpuPhysMemIsStale = true;
				}
			}
		}

		//Existing GPU buffer we never expect to use again - needs to be freed
		else if(m_gpuPhysMem != nullptr)
			FreeGpuBuffer();

		//We are never going to use the buffer on the GPU, but don't have any existing GPU memory
		//so no action required
		else
		{
		}

		//Update our capacity
		m_capacity = size;

		//If we have a pinned buffer and nothing on the other side, there's a single shared physical memory region
		m_buffersAreSame =
			( (m_cpuMemoryType == MEM_TYPE_CPU_DMA_CAPABLE) && (m_gpuMemoryType == MEM_TYPE_NULL) ) ||
			( (m_cpuMemoryType == MEM_TYPE_NULL) && (m_gpuMemoryType == MEM_TYPE_GPU_DMA_CAPABLE) );
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// CPU-side STL-esque container API

	//PrepareForCpuAccess() *must* be called prior to calling any of these methods.
public:

	const T& operator[](size_t i) const
	{ return m_cpuPtr[i]; }

	T& operator[](size_t i)
	{ return m_cpuPtr[i]; }

	/**
		@brief Adds a new element to the end of the container, allocating space if needed
	 */
	void push_back(const T& value)
	{
		size_t cursize = m_size;
		resize(m_size + 1);
		m_cpuPtr[cursize] = value;

		MarkModifiedFromCpu();
	}

	/**
		@brief Removes the last item in the container
	 */
	void pop_back()
	{
		if(!empty())
			resize(m_size - 1);
	}

	/**
		@brief Inserts a new item at the beginning of the container. This is inefficient due to copying.

		TODO: GPU implementation of this?
	 */
	void push_front(const T& value)
	{
		size_t cursize = m_size;
		resize(m_size + 1);

		PrepareForCpuAccess();

		//non-trivially-copyable types have to be copied one at a time
		if(!std::is_trivially_copyable<T>::value)
		{
			for(size_t i=0; i<cursize; i++)
				m_cpuPtr[i+1] = std::move(m_cpuPtr[i]);
		}

		//Trivially copyable types can be done more efficiently in a block
		else
			memmove(m_cpuPtr+1, m_cpuPtr, sizeof(T) * (cursize));

		//Insert the new first element
		m_cpuPtr[0] = value;

		MarkModifiedFromCpu();
	}

	/**
		@brief Removes the first item in the container

		TODO: GPU implementation of this?
	 */
	void pop_front()
	{
		//No need to move data if popping last element
		if(m_size == 1)
		{
			clear();
			return;
		}

		//Don't touch GPU side buffer

		PrepareForCpuAccess();

		//non-trivially-copyable types have to be copied one at a time
		if(!std::is_trivially_copyable<T>::value)
		{
			for(size_t i=0; i<m_size-1; i++)
				m_cpuPtr[i] = std::move(m_cpuPtr[i+1]);
		}

		//Trivially copyable types can be done more efficiently in a block
		else
			memmove(m_cpuPtr, m_cpuPtr+1, sizeof(T) * (m_size-1));

		resize(m_size - 1);

		MarkModifiedFromCpu();
	}

	AcceleratorBufferIterator<T> begin()
	{ return AcceleratorBufferIterator<T>(*this, 0); }

	AcceleratorBufferIterator<T> end()
	{ return AcceleratorBufferIterator<T>(*this, m_size); }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Hints about near-future usage patterns

public:

	/**
		@brief Sets a hint to the buffer on how often we expect to use it on the CPU in the future

		If reallocateImmediately is set, the buffer is reallocated with the specified settings to fit the current
		buffer size (shrinking to fit if needed)
	 */
	void SetCpuAccessHint(UsageHint hint, bool reallocateImmediately = false)
	{
		m_cpuAccessHint = hint;

		if(reallocateImmediately && (m_size != 0))
			Reallocate(m_size);
	}

	/**
		@brief Sets a hint to the buffer on how often we expect to use it on the GPU in the future

		If reallocateImmediately is set, the buffer is reallocated with the specified settings to fit the current
		buffer size (shrinking to fit if needed)
	 */
	void SetGpuAccessHint(UsageHint hint, bool reallocateImmediately = false)
	{
		//Only trivially copyable datatypes are allowed on the GPU
		if(!std::is_trivially_copyable<T>::value)
			hint = HINT_NEVER;

		m_gpuAccessHint = hint;

		if(reallocateImmediately && (m_size != 0))
			Reallocate(m_size);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Cache invalidation

	/**
		@brief Marks the CPU-side copy of the buffer as modified.

		If the CPU and GPU pointers point to different memory, this makes the GPU-side copy stale.
	 */
	void MarkModifiedFromCpu()
	{
		if(!m_buffersAreSame)
			m_gpuPhysMemIsStale = true;
	}

	/**
		@brief Marks the GPU-side copy of the buffer as modified.

		If the CPU and GPU pointers point to different memory, this makes the CPU-side copy stale.
	 */
	void MarkModifiedFromGpu()
	{
		if(!m_buffersAreSame)
			m_cpuPhysMemIsStale = true;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Preparation for access

	/**
		@brief Prepares the buffer to be accessed from the CPU.

		This MUST be called prior to accessing the CPU-side buffer to ensure that m_cpuPtr is valid and up to date.
	 */
	void PrepareForCpuAccess()
	{
		//Early out if no content
		if(m_size == 0)
			return;

		//If there's no buffer at all on the CPU, allocate one
		if(!HasCpuBuffer() && (m_gpuMemoryType != MEM_TYPE_GPU_DMA_CAPABLE))
			AllocateCpuBuffer(m_capacity);

		if(m_cpuPhysMemIsStale)
			CopyToCpu();
	}

	/**
		@brief Prepares the buffer to be accessed from the GPU

		This MUST be called prior to accessing the GPU-side buffer to ensure that m_gpuPhysMem is valid and up to date.

		@param outputOnly	True if the buffer is output-only for the shader, so there's no need to copy anything
							to the GPU even if data is stale.
	 */
	void PrepareForGpuAccess(bool outputOnly = false)
	{
		//Early out if no content
		if(m_size == 0)
			return;

		//If our current hint has no GPU access at all, update to say "unlikely" and reallocate
		if(m_gpuAccessHint == HINT_NEVER)
			SetGpuAccessHint(HINT_UNLIKELY, true);

		//If we don't have a buffer, allocate one unless our CPU buffer is pinned and GPU-readable
		if(!HasGpuBuffer() && (m_cpuMemoryType != MEM_TYPE_CPU_DMA_CAPABLE) )
			AllocateGpuBuffer(m_capacity);

		//Make sure the GPU-side buffer is up to date
		if(m_gpuPhysMemIsStale && !outputOnly)
			CopyToGpu();
	}

	/**
		@brief Prepares the buffer to be accessed from the GPU

		This MUST be called prior to accessing the GPU-side buffer to ensure that m_gpuPhysMem is valid and up to date.

		@param outputOnly	True if the buffer is output-only for the shader, so there's no need to copy anything
							to the GPU even if data is stale.
	 */
	void PrepareForGpuAccessNonblocking(bool outputOnly, vk::raii::CommandBuffer& cmdBuf)
	{
		//Early out if no content
		if(m_size == 0)
			return;

		//If our current hint has no GPU access at all, update to say "unlikely" and reallocate
		if(m_gpuAccessHint == HINT_NEVER)
			SetGpuAccessHint(HINT_UNLIKELY, true);

		//If we don't have a buffer, allocate one unless our CPU buffer is pinned and GPU-readable
		if(!HasGpuBuffer() && (m_cpuMemoryType != MEM_TYPE_CPU_DMA_CAPABLE) )
			AllocateGpuBuffer(m_capacity);

		//Make sure the GPU-side buffer is up to date
		if(m_gpuPhysMemIsStale && !outputOnly)
			CopyToGpuNonblocking(cmdBuf);
	}

protected:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Copying of buffer content

	/**
		@brief Copy the buffer contents from GPU to CPU and blocks until the transfer completes.
	 */
	void CopyToCpu()
	{
		assert(std::is_trivially_copyable<T>::value);

		std::lock_guard<std::mutex> lock(g_vkTransferMutex);

		//Make the transfer request
		g_vkTransferCommandBuffer->begin({});
		vk::BufferCopy region(0, 0, m_size * sizeof(T));
		g_vkTransferCommandBuffer->copyBuffer(**m_gpuBuffer, **m_cpuBuffer, {region});
		g_vkTransferCommandBuffer->end();

		//Submit the request and block until it completes
		g_vkTransferQueue->SubmitAndBlock(*g_vkTransferCommandBuffer);

		m_cpuPhysMemIsStale = false;
	}

	/**
		@brief Copy the buffer contents from CPU to GPU and blocks until the transfer completes.
	 */
	void CopyToGpu()
	{
		assert(std::is_trivially_copyable<T>::value);

		std::lock_guard<std::mutex> lock(g_vkTransferMutex);

		//Make the transfer request
		g_vkTransferCommandBuffer->begin({});
		vk::BufferCopy region(0, 0, m_size * sizeof(T));
		g_vkTransferCommandBuffer->copyBuffer(**m_cpuBuffer, **m_gpuBuffer, {region});
		g_vkTransferCommandBuffer->end();

		//Submit the request and block until it completes
		g_vkTransferQueue->SubmitAndBlock(*g_vkTransferCommandBuffer);

		m_gpuPhysMemIsStale = false;
	}


	/**
		@brief Copy the buffer contents from CPU to GPU without blocking on the CPU.

		Inserts a memory barrier to ensure that GPU-side access is synchronized.
	 */
	void CopyToGpuNonblocking(vk::raii::CommandBuffer& cmdBuf)
	{
		assert(std::is_trivially_copyable<T>::value);

		//Make the transfer request
		vk::BufferCopy region(0, 0, m_size * sizeof(T));
		cmdBuf.copyBuffer(**m_cpuBuffer, **m_gpuBuffer, {region});

		//Add the barrier
		cmdBuf.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eComputeShader,
			{},
			vk::MemoryBarrier(
				vk::AccessFlagBits::eTransferWrite,
				vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite),
			{},
			{});

		m_gpuPhysMemIsStale = false;
	}
public:
	/**
		@brief Adds a memory barrier for transferring data from host to device
	 */
	static void HostToDeviceTransferMemoryBarrier(vk::raii::CommandBuffer& cmdBuf)
	{
		cmdBuf.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eComputeShader,
			{},
			vk::MemoryBarrier(
				vk::AccessFlagBits::eTransferWrite,
				vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite),
			{},
			{});
	}


protected:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Cleanup

	/**
		@brief Free the CPU-side buffer and underlying physical memory
	 */
	void FreeCpuBuffer()
	{
		//Early out if buffer is already null
		if(m_cpuPtr == nullptr)
			return;

		//We have a buffer on the GPU.
		//If it's stale, need to push our updated content there before freeing the CPU-side copy
		if( (m_gpuMemoryType != MEM_TYPE_NULL) && m_gpuPhysMemIsStale && !empty())
			CopyToGpu();

		//Free the Vulkan buffer object
		m_cpuBuffer = nullptr;

		//Free the buffer and unmap any memory
		FreeCpuPointer(m_cpuPtr, m_cpuPhysMem, m_cpuMemoryType, m_capacity);

		//Mark CPU-side buffer as empty
		m_cpuPtr = nullptr;
		m_cpuPhysMem = nullptr;
		m_cpuMemoryType = MEM_TYPE_NULL;
		m_buffersAreSame = false;

		//If we have no GPU-side buffer either, we're empty
		if(m_gpuMemoryType == MEM_TYPE_NULL)
		{
			m_size = 0;
			m_capacity = 0;
		}
	}

	/**
		@brief Free the GPU-side buffer and underlying physical memory
	 */
	void FreeGpuBuffer()
	{
		//Early out if buffer is already null
		if(m_gpuPhysMem == nullptr)
			return;

		//If we have a CPU-side buffer, and it's stale, move our about-to-be-deleted content over before we free it
		if( (m_cpuMemoryType != MEM_TYPE_NULL) && m_cpuPhysMemIsStale && !empty() )
			CopyToCpu();

		m_gpuBuffer = nullptr;
		m_gpuPhysMem = nullptr;
		m_gpuMemoryType = MEM_TYPE_NULL;
	}

protected:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Allocation

	/**
		@brief Allocates a buffer for CPU access
	 */
	__attribute__((noinline))
	void AllocateCpuBuffer(size_t size)
	{
		if(size == 0)
			LogFatal("AllocateCpuBuffer with size zero (invalid)\n");

		//If any GPU access is expected, use pinned memory so we don't have to move things around
		if(m_gpuAccessHint != HINT_NEVER)
		{
			//Make a Vulkan buffer first
			vk::BufferCreateInfo bufinfo(
				{},
				size * sizeof(T),
				vk::BufferUsageFlagBits::eTransferSrc |
					vk::BufferUsageFlagBits::eTransferDst |
					vk::BufferUsageFlagBits::eStorageBuffer);
			m_cpuBuffer = std::make_unique<vk::raii::Buffer>(*g_vkComputeDevice, bufinfo);

			//Figure out actual memory requirements of the buffer
			//(may be rounded up from what we asked for)
			auto req = m_cpuBuffer->getMemoryRequirements();

			//Allocate the physical memory to back the buffer
			vk::MemoryAllocateInfo info(req.size, g_vkPinnedMemoryType);
			m_cpuPhysMem = std::make_unique<vk::raii::DeviceMemory>(*g_vkComputeDevice, info);

			//Map it and bind to the buffer
			m_cpuPtr = reinterpret_cast<T*>(m_cpuPhysMem->mapMemory(0, req.size));
			m_cpuBuffer->bindMemory(**m_cpuPhysMem, 0);

			//We now have pinned memory
			m_cpuMemoryType = MEM_TYPE_CPU_DMA_CAPABLE;

			if(g_hasDebugUtils)
				UpdateCpuNames();
		}

		//If frequent CPU access is expected, use normal host memory
		else if(m_cpuAccessHint == HINT_LIKELY)
		{
			m_cpuBuffer = nullptr;
			m_cpuMemoryType = MEM_TYPE_CPU_ONLY;
			m_cpuPtr = m_cpuAllocator.allocate(size);
		}

		//If infrequent CPU access is expected, use a memory mapped temporary file so it can be paged out to disk
		else
		{
			#ifdef _WIN32

				//On Windows, use normal memory for now
				//until we figure out how to do this there
				m_cpuBuffer = nullptr;
				m_cpuMemoryType = MEM_TYPE_CPU_ONLY;
				m_cpuPtr = m_cpuAllocator.allocate(size);

			#else

				m_cpuBuffer = nullptr;
				m_cpuMemoryType = MEM_TYPE_CPU_PAGED;

				//Make the temp file
				char fname[] = "/tmp/glscopeclient-tmpXXXXXX";
				m_tempFileHandle = mkstemp(fname);
				if(m_tempFileHandle < 0)
				{
					LogError("Failed to create temporary file %s\n", fname);
					abort();
				}

				//Resize it to our desired file size
				size_t bytesize = size * sizeof(T);
				if(0 != ftruncate(m_tempFileHandle, bytesize))
				{
					LogError("Failed to resize temporary file %s\n", fname);
					abort();
				}

				//Map it
				m_cpuPtr = reinterpret_cast<T*>(mmap(
					nullptr,
					bytesize,
					PROT_READ | PROT_WRITE,
					MAP_SHARED/* | MAP_UNINITIALIZED*/,
					m_tempFileHandle,
					0));
				if(m_cpuPtr == MAP_FAILED)
				{
					LogError("Failed to map temporary file %s\n", fname);
					perror("mmap failed: ");
					abort();
				}
				m_cpuMemoryType = MEM_TYPE_CPU_PAGED;

				//Delete it (file will be removed by the OS after our active handle is closed)
				if(0 != unlink(fname))
					LogWarning("Failed to unlink temporary file %s, file will remain after application terminates\n", fname);

			#endif
		}

		//Memory has been allocated. Call constructors iff type is not trivially copyable
		//(This is not exactly 1:1 with having a constructor, but hopefully good enough?)
		if(!std::is_trivially_copyable<T>::value)
		{
			for(size_t i=0; i<size; i++)
				new(m_cpuPtr +i) T;
		}
	}

	/**
		@brief Frees a CPU-side buffer

		An explicit type is passed here because if we're reallocating we might change memory type.
		By this point AllocateCpuBuffer() has been called so m_cpuMemoryType points to the type of the new buffer,
		not the one we're getting rid of.
	 */
	__attribute__((noinline))
	void FreeCpuPointer(T* ptr, MemoryType type, size_t size)
	{
		//Call destructors iff type is not trivially copyable
		if(!std::is_trivially_copyable<T>::value)
		{
			for(size_t i=0; i<size; i++)
				ptr[i].~T();
		}

		switch(type)
		{
			case MEM_TYPE_NULL:
				//legal no-op
				break;

			case MEM_TYPE_CPU_DMA_CAPABLE:
				LogFatal("FreeCpuPointer for MEM_TYPE_CPU_DMA_CAPABLE requires the vk::raii::DeviceMemory\n");
				break;

			case MEM_TYPE_CPU_PAGED:
				#ifndef _WIN32
					munmap(ptr, size * sizeof(T));
					close(m_tempFileHandle);
					m_tempFileHandle = -1;
				#endif
				break;

			case MEM_TYPE_CPU_ONLY:
				m_cpuAllocator.deallocate(ptr, size);
				break;

			default:
				LogFatal("FreeCpuPointer: invalid type %x\n", type);
		}
	}

	/**
		@brief Frees a CPU-side physical memory block

		An explicit type is passed here because if we're reallocating we might change memory type.
		By this point AllocateCpuBuffer() has been called so m_cpuMemoryType points to the type of the new buffer,
		not the one we're getting rid of.
	 */
	__attribute__((noinline))
	void FreeCpuPointer(T* ptr, std::unique_ptr<vk::raii::DeviceMemory>& buf, MemoryType type, size_t size)
	{
		switch(type)
		{
			case MEM_TYPE_CPU_DMA_CAPABLE:
				buf->unmapMemory();
				break;

			default:
				FreeCpuPointer(ptr, type, size);
		}
	}

	/**
		@brief Allocates physical memory for GPU access
	 */
	__attribute__((noinline))
	void AllocateGpuBuffer(size_t size)
	{
		assert(std::is_trivially_copyable<T>::value);

		//Make a Vulkan buffer first
		vk::BufferCreateInfo bufinfo(
			{},
			size * sizeof(T),
			vk::BufferUsageFlagBits::eTransferSrc |
				vk::BufferUsageFlagBits::eTransferDst |
				vk::BufferUsageFlagBits::eStorageBuffer);
		m_gpuBuffer = std::make_unique<vk::raii::Buffer>(*g_vkComputeDevice, bufinfo);

		//Figure out actual memory requirements of the buffer
		//(may be rounded up from what we asked for)
		auto req = m_gpuBuffer->getMemoryRequirements();

		//For now, always use local memory
		vk::MemoryAllocateInfo info(req.size, g_vkLocalMemoryType);
		m_gpuPhysMem = std::make_unique<vk::raii::DeviceMemory>(*g_vkComputeDevice, info);
		m_gpuMemoryType = MEM_TYPE_GPU_ONLY;

		m_gpuBuffer->bindMemory(**m_gpuPhysMem, 0);

		if(g_hasDebugUtils)
			UpdateGpuNames();
	}

protected:

	///@brief Friendly name of the buffer (for debug tools)
	std::string m_name;

	/**
		@brief Pushes our friendly name to the underlying Vulkan objects
	 */
	__attribute__((noinline))
	void UpdateGpuNames()
	{
		std::string sname = m_name;
		if(sname.empty())
			sname = "unnamed";
		std::string prefix = std::string("AcceleratorBuffer.") + sname + ".";

		std::string gpuBufName = prefix + "m_gpuBuffer";
		std::string gpuPhysName = prefix + "m_gpuPhysMem";

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eBuffer,
				reinterpret_cast<uint64_t>(static_cast<VkBuffer>(**m_gpuBuffer)),
				gpuBufName.c_str()));

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eDeviceMemory,
				reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(**m_gpuPhysMem)),
				gpuPhysName.c_str()));
	}

	/**
		@brief Pushes our friendly name to the underlying Vulkan objects
	 */
	__attribute__((noinline))
	void UpdateCpuNames()
	{
		std::string sname = m_name;
		if(sname.empty())
			sname = "unnamed";
		std::string prefix = std::string("AcceleratorBuffer.") + sname + ".";

		std::string cpuBufName = prefix + "m_cpuBuffer";
		std::string cpuPhysName = prefix + "m_cpuPhysMem";

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eBuffer,
				reinterpret_cast<uint64_t>(static_cast<VkBuffer>(**m_cpuBuffer)),
				cpuBufName.c_str()));

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eDeviceMemory,
				reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(**m_cpuPhysMem)),
				cpuPhysName.c_str()));
	}

public:
	void SetName(std::string name)
	{
		m_name = name;
		if(g_hasDebugUtils)
		{
			if(m_gpuBuffer != nullptr)
				UpdateGpuNames();
			if(m_cpuBuffer != nullptr)
				UpdateCpuNames();
		}
	}

};

#endif
