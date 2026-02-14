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

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static variables

list< shared_ptr<AcceleratorBuffer<uint8_t> > > ScratchBufferManager::m_pool_u8_gpu_waveform;
list< shared_ptr<AcceleratorBuffer<float> > > ScratchBufferManager::m_pool_f32_gpu_waveform;
list< shared_ptr<AcceleratorBuffer<int64_t> > > ScratchBufferManager::m_pool_i64_gpu_waveform;
list< shared_ptr<AcceleratorBuffer<int64_t> > > ScratchBufferManager::m_pool_i64_gpu_small;

mutex ScratchBufferManager::m_poolMutex;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System stats

/**
	@brief Get the total number of bytes in scratch buffers
 */
size_t ScratchBufferManager::GetTotalSize()
{
	size_t tmp = 0;
	tmp += GetPoolSize(U8_GPU_WAVEFORM);
	tmp += GetPoolSize(F32_GPU_WAVEFORM);
	tmp += GetPoolSize(I64_GPU_WAVEFORM);
	tmp += GetPoolSize(I64_GPU_SMALL);
	return tmp;
}

/**
	@brief Get the total number of bytes in a specific scratch buffer pool
 */
size_t ScratchBufferManager::GetPoolSize(PoolID_uint8 id)
{
	lock_guard<mutex> lock(m_poolMutex);

	size_t tmp = 0;
	switch(id)
	{
		case U8_GPU_WAVEFORM:
			for(auto& p : m_pool_u8_gpu_waveform)
				tmp += p->capacity();
			break;

		default:
			break;
	}

	return tmp * sizeof(uint8_t);
}

/**
	@brief Get the total number of bytes in a specific scratch buffer pool
 */
size_t ScratchBufferManager::GetPoolSize(PoolID_float32 id)
{
	lock_guard<mutex> lock(m_poolMutex);

	size_t tmp = 0;
	switch(id)
	{
		case F32_GPU_WAVEFORM:
			for(auto& p : m_pool_f32_gpu_waveform)
				tmp += p->capacity();
			break;

		default:
			break;
	}

	return tmp * sizeof(float);
}

/**
	@brief Get the total number of bytes in a specific scratch buffer pool
 */
size_t ScratchBufferManager::GetPoolSize(PoolID_int64 id)
{
	lock_guard<mutex> lock(m_poolMutex);

	size_t tmp = 0;
	switch(id)
	{
		case I64_GPU_WAVEFORM:
			for(auto& p : m_pool_i64_gpu_waveform)
				tmp += p->capacity();
			break;

		case I64_GPU_SMALL:
			for(auto& p : m_pool_i64_gpu_small)
				tmp += p->capacity();
			break;

		default:
			break;
	}

	return tmp * sizeof(uint64_t);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory pressure and cleanup

/**
	@brief Flush all cached buffers
 */
void ScratchBufferManager::clear()
{
	lock_guard<mutex> lock(m_poolMutex);
	m_pool_u8_gpu_waveform.clear();
	m_pool_f32_gpu_waveform.clear();
	m_pool_i64_gpu_waveform.clear();
	m_pool_i64_gpu_small.clear();
}

/**
	@brief Called when we run out of (probably) VRAM
 */
bool ScratchBufferManager::OnMemoryPressure(
	[[maybe_unused]] MemoryPressureLevel level,
	[[maybe_unused]] MemoryPressureType type,
	[[maybe_unused]] size_t requestedSize)
{
	if(GetTotalSize() > 0)
	{
		LogDebug("[ScratchBufferManager::OnMemoryPressure] dropping all scratch buffers\n");
		clear();
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pool management: uint8

shared_ptr< AcceleratorBuffer<uint8_t> > ScratchBufferManager::Allocate(PoolID_uint8 pool)
{
	lock_guard<mutex> lock(m_poolMutex);

	switch(pool)
	{
		case U8_GPU_WAVEFORM:
			{
				//If we have a suitable buffer in the pool now, allocate it
				if(!m_pool_u8_gpu_waveform.empty())
				{
					auto p = *m_pool_u8_gpu_waveform.begin();
					m_pool_u8_gpu_waveform.pop_front();
					return p;
				}

				//No buffer available, allocate and hand out a new one
				//TODO: keep track of a "in use" list somewhere?
				else
				{
					auto p = make_shared<AcceleratorBuffer<uint8_t>>("ScratchBufferManager.U8_GPU_WAVEFORM");
					p->SetGpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_LIKELY);
					return p;
				}
			}
			break;

		default:
			LogFatal("Tried to allocate from nonexistent or unimplemented scratch buffer pool\n");
			return nullptr;
	}
}

void ScratchBufferManager::Free(shared_ptr< AcceleratorBuffer<uint8_t> >& p, PoolID_uint8 pool)
{
	lock_guard<mutex> lock(m_poolMutex);
	switch(pool)
	{
		case U8_GPU_WAVEFORM:
			m_pool_u8_gpu_waveform.push_back(p);
			break;

		default:
			LogFatal("Tried to free to nonexistent or unimplemented scratch buffer pool\n");
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pool management: float32

shared_ptr< AcceleratorBuffer<float> > ScratchBufferManager::Allocate(PoolID_float32 pool)
{
	lock_guard<mutex> lock(m_poolMutex);

	switch(pool)
	{
		case F32_GPU_WAVEFORM:
			{
				//If we have a suitable buffer in the pool now, allocate it
				if(!m_pool_f32_gpu_waveform.empty())
				{
					auto p = *m_pool_f32_gpu_waveform.begin();
					m_pool_f32_gpu_waveform.pop_front();
					return p;
				}

				//No buffer available, allocate and hand out a new one
				//TODO: keep track of a "in use" list somewhere?
				else
				{
					auto p = make_shared<AcceleratorBuffer<float>>("ScratchBufferManager.F32_GPU_WAVEFORM");
					p->SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
					return p;
				}
			}
			break;

		default:
			LogFatal("Tried to allocate from nonexistent or unimplemented scratch buffer pool\n");
			return nullptr;
	}
}

void ScratchBufferManager::Free(shared_ptr< AcceleratorBuffer<float> >& p, PoolID_float32 pool)
{
	lock_guard<mutex> lock(m_poolMutex);
	switch(pool)
	{
		case F32_GPU_WAVEFORM:
			m_pool_f32_gpu_waveform.push_back(p);
			break;

		default:
			LogFatal("Tried to free to nonexistent or unimplemented scratch buffer pool\n");
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pool management: int64

shared_ptr< AcceleratorBuffer<int64_t> > ScratchBufferManager::Allocate(PoolID_int64 pool)
{
	lock_guard<mutex> lock(m_poolMutex);

	switch(pool)
	{
		case I64_GPU_WAVEFORM:
			{
				//If we have a suitable buffer in the pool now, allocate it
				if(!m_pool_i64_gpu_waveform.empty())
				{
					auto p = *m_pool_i64_gpu_waveform.begin();
					m_pool_i64_gpu_waveform.pop_front();
					return p;
				}

				//No buffer available, allocate and hand out a new one
				//TODO: keep track of a "in use" list somewhere?
				else
				{
					auto p = make_shared<AcceleratorBuffer<int64_t>>("ScratchBufferManager.I64_GPU_WAVEFORM");
					p->SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
					return p;
				}
			}
			break;

		case I64_GPU_SMALL:
			{
				//If we have a suitable buffer in the pool now, allocate it
				if(!m_pool_i64_gpu_small.empty())
				{
					auto p = *m_pool_i64_gpu_small.begin();
					m_pool_i64_gpu_small.pop_front();
					return p;
				}

				//No buffer available, allocate and hand out a new one
				//TODO: keep track of a "in use" list somewhere?
				else
				{
					auto p = make_shared<AcceleratorBuffer<int64_t>>("ScratchBufferManager.I64_GPU_SMALL");
					p->SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
					return p;
				}
			}
			break;

		default:
			LogFatal("Tried to allocate from nonexistent or unimplemented scratch buffer pool\n");
			return nullptr;
	}
}

void ScratchBufferManager::Free(shared_ptr< AcceleratorBuffer<int64_t> >& p, PoolID_int64 pool)
{
	lock_guard<mutex> lock(m_poolMutex);
	switch(pool)
	{
		case I64_GPU_WAVEFORM:
			m_pool_i64_gpu_waveform.push_back(p);
			break;

		case I64_GPU_SMALL:
			m_pool_i64_gpu_small.push_back(p);
			break;

		default:
			LogFatal("Tried to free to nonexistent or unimplemented scratch buffer pool\n");
			break;
	}
}
