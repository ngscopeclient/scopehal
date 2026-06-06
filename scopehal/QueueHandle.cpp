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
	@author Lain Agan
	@brief Vulkan queue management
 */

#include "log.h"
#include "QueueManager.h"

using namespace std;
extern bool g_hasDebugUtils;

QueueHandle::QueueHandle(std::shared_ptr<QueueWrapper>& queue, const string& name)
	: m_queue(queue)
	, m_fence(make_unique<vk::raii::Fence>(*queue->GetDevice(), vk::FenceCreateInfo()))
	, m_fenceBusy(false)
	, m_fenceName(name)
{
	//Add us as a named user of the physical queue
	m_queue->AddName(name);

	//Name our fence (this is private to the QueueHandle)
	if(g_hasDebugUtils)
	{
		m_queue->GetDevice()->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eFence,
				reinterpret_cast<uint64_t>(static_cast<VkFence>(**m_fence)),
				name.c_str()));
	}
}

QueueHandle::~QueueHandle()
{
	m_queue->RemoveName(m_fenceName);

	{
		const lock_guard<recursive_mutex> lock(m_queue->GetMutex());
		m_fence = nullptr;
	}
	m_queue = nullptr;
}

void QueueHandle::Submit(vk::raii::CommandBuffer const& cmdBuf)
{
	const scoped_lock lock(m_queue->GetMutex(), m_fenceMutex);

	_waitFence();

	m_fenceBusy = true;

	vk::SubmitInfo info({}, {}, *cmdBuf);
	m_queue->GetQueue()->submit(info, **m_fence);
}

void QueueHandle::SubmitAndBlock(vk::raii::CommandBuffer const& cmdBuf)
{
	//Submit while holding both mutexes
	{
		const scoped_lock lock(m_queue->GetMutex(), m_fenceMutex);
		_waitFence();

		m_fenceBusy = true;

		vk::SubmitInfo info({}, {}, *cmdBuf);
		m_queue->GetQueue()->submit(info, **m_fence);
	}

	//Then wait while only holding the fence mutex
	const lock_guard<recursive_mutex> lock(m_fenceMutex);
	_waitFence();
}

/**
	@brief Wait for previous submits to complete, but only up to a timeout

	@return true if now idle, false if timeout
 */

bool QueueHandle::WaitIdleWithTimeout(uint64_t nanoseconds)
{
	const lock_guard<recursive_mutex> lock(m_fenceMutex);

	//Not busy? Return immediately
	if(!m_fenceBusy)
		return true;

	//Wait exactly once for the submit, time out if not finished
	if(vk::Result::eTimeout == m_queue->GetDevice()->waitForFences({**m_fence}, VK_TRUE, nanoseconds))
		return false;

	//If we get here, the most recent wait was the one that finished
	m_fenceBusy = false;
	m_queue->GetDevice()->resetFences(**m_fence);
	return true;
}

void QueueHandle::_waitFence()
{
	//Not busy? Return immediately
	if(!m_fenceBusy)
		return;

	//Wait for any previous submit to finish
	while(vk::Result::eTimeout == m_queue->GetDevice()->waitForFences({**m_fence}, VK_TRUE, 1000 * 1000))
	{}

	m_fenceBusy = false;
	m_queue->GetDevice()->resetFences(**m_fence);
}
