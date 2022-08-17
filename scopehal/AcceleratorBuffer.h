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

#ifndef _WIN32
#include <sys/mman.h>
#endif

extern size_t g_vkPinnedMemoryType;
extern std::unique_ptr<vk::raii::Device> g_vkComputeDevice;

/**
	@brief A buffer of memory which may be used by GPU acceleration

	At any given point in time the buffer may exist as a single copy on the CPU, a single copy on the GPU, or
	mirrored buffers on both sides.

	Hints can be provided to the buffer about future usage patterns to optimize storage location for best performance.

	This buffer generally provides std::vector semantics, but does *not* initialize memory or call constructors on
	elements when calling resize() or reserve(). All locations not explicitly written to have undefined values.
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

	///@brief CPU-side buffer
	T* m_cpuPtr;

	///@brief CPU-side pinned buffer
	std::unique_ptr<vk::raii::DeviceMemory> m_cpuPinnedBuffer;

	///@brief GPU-side buffer
	std::unique_ptr<vk::raii::DeviceMemory> m_gpuPtr;

	///@brief True if m_cpuPtr and m_gpuPtr point to the same physical memory location
	bool m_buffersAreSame;

	///@brief True if m_cpuPtr contains stale data (m_gpuPtr has been modified and they point to different memory)
	bool m_cpuPtrIsStale;

	///@brief True if m_gpuPtr contains stale data (m_cpuPtr has been modified and they point to different memory)
	bool m_gpuPtrIsStale;

	///@brief File handle used for MEM_TYPE_CPU_PAGED
#ifndef _WIN32
	int m_tempFileHandle;
#endif

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
	AcceleratorBuffer()
		: m_cpuMemoryType(MEM_TYPE_NULL)
		, m_gpuMemoryType(MEM_TYPE_NULL)
		, m_cpuPtr(nullptr)
		, m_gpuPtr(nullptr)
		, m_buffersAreSame(false)
		, m_cpuPtrIsStale(false)
		, m_gpuPtrIsStale(false)
		#ifndef _WIN32
		, m_tempFileHandle(0)
		#endif
		, m_capacity(0)
		, m_size(0)
		, m_cpuAccessHint(HINT_LIKELY)	//default access hint: CPU-only
		, m_gpuAccessHint(HINT_NEVER)
	{
	}

	~AcceleratorBuffer()
	{
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// General accessors
public:

	/**
		@brief Returns the actual size of the container (may be smaller than what was allocated)
	 */
	size_t size()
	{ return m_size; }

	/**
		@brief Returns the allocated size of the container
	 */
	size_t capacity()
	{ return m_capacity; }

	/**
		@brief Returns the total reserved CPU memory, in bytes
	 */
	size_t GetCpuMemoryBytes()
	{
		if(m_cpuMemoryType == MEM_TYPE_NULL)
			return 0;
		else
			return m_capacity * sizeof(T);
	}

	/**
		@brief Returns the total reserved GPU memory, in bytes
	 */
	size_t GetGpuMemoryBytes()
	{
		if(m_gpuMemoryType == MEM_TYPE_NULL)
			return 0;
		else
			return m_capacity * sizeof(T);
	}

	/**
		@brief Returns true if the container is empty
	 */
	bool empty()
	{ return (m_size == 0); }

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
			else
				reserve(m_capacity * 2);
		}

		//Update our size
		m_size = size;
	}

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

		This also ensures that the buffer is stored in the best location described by the current hint flags
	 */
	void shrink_to_fit()
	{
		Reallocate(m_size);
	}

protected:

	/**
		@brief Reallocates the buffer so that it contains exactly size elements
	 */
	__attribute__((noinline))
	void Reallocate(size_t size)
	{
		//If we do not anticipate using the data on the CPU, we shouldn't waste RAM
		if(m_cpuAccessHint == HINT_NEVER)
		{
			LogFatal("reserve for GPU-only buffers not implemented\n");

			FreeCpuBuffer();
		}

		else
		{
			if(m_gpuPtr != nullptr)
				LogFatal("reserve: need to handle existing GPU data\n");

			//Resize CPU memory
			//TODO: optimization, when expanding a MEM_TYPE_CPU_PAGED we can just enlarge the file
			//and not have to make a new temp file and copy the content
			if(m_cpuPtr != nullptr)
			{
				//Save the old pointer
				auto pOld = m_cpuPtr;
				auto pOldPin = std::move(m_cpuPinnedBuffer);
				auto type = m_cpuMemoryType;

				//Allocate the new buffer
				AllocateCpuBuffer(size);

				//If CPU-side data is valid, copy existing data over.
				//New pointer is still valid in this case.
				if(!m_cpuPtrIsStale)
					memcpy(m_cpuPtr, pOld, sizeof(T) * m_size);

				//Otherwise no copy here
				//TODO: copy from GPU if GPU side pointer is valid??

				//Now we're done with the old pointer so get rid of it
				FreeCpuPointer(pOld, pOldPin, type, m_capacity);
			}

			//Allocate new CPU memory, replacing our current (null) pointer
			else
				AllocateCpuBuffer(size);

			//Update flags
			m_buffersAreSame = false;
			m_cpuPtrIsStale = false;

		}

		//We're expecting to use data on the GPU, so prepare to do stuff with it
		if(m_gpuAccessHint != HINT_NEVER)
		{
			//If GPU access is unlikely, we probably want to just use pinned memory.
			//If available, mark buffers as the same, and free any existing GPU buffer we might have
			if( (m_gpuAccessHint == HINT_UNLIKELY) && (m_cpuMemoryType == MEM_TYPE_CPU_DMA_CAPABLE) )
			{
				m_buffersAreSame = true;
				m_gpuMemoryType = MEM_TYPE_CPU_DMA_CAPABLE;
				m_gpuPtr = nullptr;
			}

			//Nope, we need to allocate dedicated GPU memory
			else
				LogFatal("reserve for GPU buffers not implemented\n");
		}

		//Existing GPU buffer we never expect to use again - needs to be freed
		else if(m_gpuPtr != nullptr)
			FreeGpuBuffer();

		//We are never going to use the buffer on the GPU, but don't have any existing GPU memory
		//so no action required
		else
		{
		}

		//Update our capacity
		m_capacity = size;

		//TODO: are buffers always in sync after this call?
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
		LogDebug("push_back (size=%zu, capacity=%zu)\n", m_size, m_capacity);
		LogIndenter li;

		size_t cursize = m_size;
		resize(m_size + 1);
		m_cpuPtr[cursize] = value;
	}

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

		if(reallocateImmediately)
			Reallocate(m_size);
	}

	/**
		@brief Sets a hint to the buffer on how often we expect to use it on the GPU in the future

		If reallocateImmediately is set, the buffer is reallocated with the specified settings to fit the current
		buffer size (shrinking to fit if needed)
	 */
	void SetGpuAccessHint(UsageHint hint, bool reallocateImmediately = false)
	{
		m_gpuAccessHint = hint;

		if(reallocateImmediately)
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
			m_gpuPtrIsStale = true;
	}

	/**
		@brief Marks the GPU-side copy of the buffer as modified.

		If the CPU and GPU pointers point to different memory, this makes the CPU-side copy stale.
	 */
	void MarkModifiedFromGpu()
	{
		if(!m_buffersAreSame)
			m_cpuPtrIsStale = true;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Preparation for access

	/**
		@brief Prepares the buffer to be accessed from the CPU.

		This MUST be called prior to accessing the CPU-side buffer to ensure that m_cpuPtr is valid and up to date.
	 */
	void PrepareForCpuAccess()
	{
	}

	/**
		@brief Prepares the buffer to be accessed from the GPU

		This MUST be called prior to accessing the GPU-side buffer to ensure that m_gpuPtr is valid and up to date.
	 */
	void PrepareForGpuAccess()
	{
	}

protected:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Copying of buffer content

	/**
		@brief Copy the buffer contents from GPU to CPU
	 */
	void CopyToCpu()
	{
	}

	/**
		@brief Copy the buffer contents from CPU to GPU
	 */
	void CopyToGpu()
	{
	}

protected:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Cleanup

	/**
		@brief Free the CPU-side buffer
	 */
	void FreeCpuBuffer()
	{
		//Early out if buffer is already null
		if(m_cpuPtr == nullptr)
			return;

		//If we have shared CPU/GPU buffers, we need to allocate a GPU-only buffer and move our data there
		if(m_buffersAreSame)
			LogFatal("FreeCpuBuffer: same buffer not supported\n");

		//We have a buffer on the GPU.
		//If it's stale, need to push our updated content there before freeing the CPU-side copy
		else if( (m_gpuMemoryType != MEM_TYPE_NULL) && m_gpuPtrIsStale)
			CopyToGpu();

		//Free the buffer and unmap any memory
		FreeCpuPointer(m_cpuPtr, m_cpuPinnedBuffer, m_cpuMemoryType, m_capacity);

		//Mark CPU-side buffer as empty
		m_cpuPtr = nullptr;
		m_cpuPinnedBuffer = nullptr;
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
		@brief Free the GPU-side buffer
	 */
	void FreeGpuBuffer()
	{
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
		//If any GPU access is expected, use pinned memory so we don't have to move things around
		if(m_gpuAccessHint != HINT_NEVER)
		{
			LogVerbose("Allocating CPU buffer (%zu elements, pinned)\n", size);

			//Allocate the buffer
			vk::MemoryAllocateInfo info(size, g_vkPinnedMemoryType);
			m_cpuPinnedBuffer = std::make_unique<vk::raii::DeviceMemory>(*g_vkComputeDevice, info);

			//Map it
			m_cpuPtr = reinterpret_cast<T*>(m_cpuPinnedBuffer->mapMemory(0, size));

			//We now have pinned memory
			m_cpuMemoryType = MEM_TYPE_CPU_DMA_CAPABLE;
		}

		//If frequent CPU access is expected, use normal host memory
		else if(m_cpuAccessHint == HINT_LIKELY)
		{
			LogVerbose("Allocating CPU buffer (%zu elements, not pinned)\n", size);
			m_cpuMemoryType = MEM_TYPE_CPU_ONLY;
			m_cpuPtr = m_cpuAllocator.allocate(size);
		}

		//If infrequent CPU access is expected, use a memory mapped temporary file so it can be paged out to disk
		else
		{
			#ifdef _WIN32

				//On Windows, use normal memory for now
				//until we figure out how to do this there
				LogVerbose("Allocating CPU buffer (%zu elements, not pinned)\n", size);
				m_cpuMemoryType = MEM_TYPE_CPU_ONLY;
				m_cpuPtr = m_cpuAllocator.allocate(size);

			#else

				LogVerbose("Allocating CPU buffer (%zu elements, paged)\n", size);
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
					abort();
				}
				m_cpuMemoryType = MEM_TYPE_CPU_PAGED;

				//Delete it (file will be removed by the OS after our active handle is closed)
				if(0 != unlink(fname))
					LogWarning("Failed to unlink temporary file %s, file will remain after application terminates\n", fname);

			#endif
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
		switch(type)
		{
			case MEM_TYPE_NULL:
				//legal no-op
				break;

			case MEM_TYPE_CPU_DMA_CAPABLE:
				LogFatal("FreeCpuPointer for MEM_TYPE_CPU_DMA_CAPABLE requires the vk::raii::DeviceMemory\n");
				break;

			case MEM_TYPE_CPU_PAGED:
				munmap(ptr, size * sizeof(T));
				close(m_tempFileHandle);
				m_tempFileHandle = -1;
				break;

			case MEM_TYPE_CPU_ONLY:
				m_cpuAllocator.deallocate(ptr, size);
				break;

			default:
				LogFatal("FreeCpuPointer: invalid type %x\n", type);
		}
	}

	/**
		@brief Frees a CPU-side buffer

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
};

#endif
