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

#ifndef QueueHandle_h
#define QueueHandle_h

#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include "QueueWrapper.h"

/**
	@brief Wrapper around a Vulkan Queue object

	Many QueueHandle's can point to a single QueueWrapper and are thread safe, but a single QueueHandle cannot be
	used from more than one thread simultaneously.

	TODO: how does this play with g_vkTransferQueue? for now keep a lock on the fence for that use case
 */
class QueueHandle
{
public:

	QueueHandle(std::shared_ptr<QueueWrapper>& queue, const std::string& name);
	~QueueHandle();

	/// Submit the given command buffer on the queue
	void Submit(vk::raii::CommandBuffer const& cmdBuf);

	/// Submit the given command buffer on the queue and wait until completion
	void SubmitAndBlock(vk::raii::CommandBuffer const& cmdBuf);

	const std::string GetName() const
	{ return m_queue->GetName(); }

	/**
		@brief Wait for all previous submits to complete
	 */
	void WaitIdle()
	{
		const std::lock_guard<std::recursive_mutex> lock(m_queue->GetMutex());
		_waitFence();
	}

	bool WaitIdleWithTimeout(uint64_t nanoseconds);

	std::shared_ptr<QueueWrapper> GetQueue()
	{ return m_queue; }

public:
	//non-copyable
	QueueHandle(QueueHandle const&) = delete;
	QueueHandle& operator=(QueueHandle const&) = delete;

protected:
	/// Waits for previous submit's fence, if any, then resets the fence for reuse.
	/// Must obtain the lock before calling!
	void _waitFence();

protected:
	friend QueueLock;
	std::shared_ptr<QueueWrapper> m_queue;
	std::unique_ptr<vk::raii::Fence> m_fence;
	bool m_fenceBusy;

	std::string m_fenceName;

	///@brief The mutex controlling access to the fence
	std::recursive_mutex m_fenceMutex;
};


#endif
