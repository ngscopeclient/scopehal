/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of ScratchBufferManager
	@ingroup core
 */
#ifndef ScratchBufferManager_h
#define ScratchBufferManager_h

/**
	@brief Memory pool for temporary working buffers
	@ingroup core
 */
class ScratchBufferManager
{
public:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Pool IDs

	enum PoolID_uint8
	{
		//Roughly one uint8_t per sample in the waveform, GPU resident
		U8_GPU_WAVEFORM
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// System stats

	static size_t GetTotalSize();
	static size_t GetPoolSize(PoolID_uint8 id);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Memory pressure and cleanup

	static void clear();
	static bool OnMemoryPressure(MemoryPressureLevel level, MemoryPressureType type, size_t requestedSize);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// The pools

	static std::shared_ptr< AcceleratorBuffer<uint8_t> > Allocate(PoolID_uint8 pool);

	static void Free(std::shared_ptr< AcceleratorBuffer<uint8_t> >& p, PoolID_uint8 pool);

protected:

	///@brief Mutex for all pools
	static std::mutex m_poolMutex;

	///@brief Pool for U8_GPU_WAVEFORM
	static std::list< std::shared_ptr<AcceleratorBuffer<uint8_t> > > m_pool_u8_gpu_waveform;
};

/**
	@brief RAII helper for allocations
 */
template<class ID, class T>
class ScratchBuffer
{
public:

	ScratchBuffer(ID id)
		: m_ptr(ScratchBufferManager::Allocate(id))
		, m_pool(id)
	{}

	~ScratchBuffer()
	{ ScratchBufferManager::Free(m_ptr, m_pool); }

	///@brief Get the underlying temporary
	AcceleratorBuffer<T>& operator*()
	{ return m_ptr; }

	///@brief Get the underlying temporary
	AcceleratorBuffer<T>* operator->()
	{ return m_ptr.get(); }

protected:

	///@brief The underlying buffer pointer
	std::shared_ptr< AcceleratorBuffer<T> > m_ptr;

	///@brief The pool we were allocated from
	ID m_pool;
};

#endif
