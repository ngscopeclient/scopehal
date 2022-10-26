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
	@author Lain Agan
	@brief Declaration of QueueManager and QueueHandle
 */

#ifndef QueueManager_h
#define QueueManager_h

#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class QueueLock;

/**
 * @brief Wrapper around a Vulkan Queue, protected by mutex for thread safety.
 *
 */
class QueueHandle
{
public:
	QueueHandle(std::shared_ptr<vk::raii::Device> device, size_t family, size_t index, std::string name);
	~QueueHandle();

	/// Append a name to the queue, used for debugging
	void AddName(std::string name);

	/// Submit the given command buffer on the queue
	void Submit(vk::raii::CommandBuffer const& cmdBuf);
	/// Submit the given command buffer on the queue and wait until completion
	void SubmitAndBlock(vk::raii::CommandBuffer const& cmdBuf);

	const std::string& GetName() const
	{ return m_name; }

public:
	//non-copyable
	QueueHandle(QueueHandle const&) = delete;
	QueueHandle& operator=(QueueHandle const&) = delete;

protected:
	/// Waits for previous submit's fence, if any, then resets the fence for reuse.
	/// Must obtain the lock before calling!
	void _waitFence();

public:
	const size_t m_family;
	const size_t m_index;

protected:
	friend QueueLock;
	std::mutex m_mutex;
	std::string m_name;
	std::shared_ptr<vk::raii::Device> m_device;
	std::unique_ptr<vk::raii::Queue> m_queue;
	std::unique_ptr<vk::raii::Fence> m_fence;
};


/**
 * @brief Obtains exclusive access to a Vulkan Queue for the duration of its existence, similar to a std::lock_guard.
 *
 * Use this when you need access to the vk::raii::Queue& directly.
 * Lock is released upon destruction.
 */
class QueueLock
{
public:
	QueueLock(std::shared_ptr<QueueHandle> handle)
	: m_lock(handle->m_mutex)
	, m_handle(handle)
	{ handle->_waitFence(); }

	vk::raii::Queue& operator*()
	{ return *(m_handle->m_queue); }

public:
	//non-copyable
	QueueLock(QueueLock const&) = delete;
	QueueLock& operator=(QueueLock const&) = delete;

protected:
	const std::lock_guard<std::mutex> m_lock;
	std::shared_ptr<QueueHandle> m_handle;
};


/**
 * @brief Allocates and hands out std::shared_ptr<QueueHandle> instances for thread-safe access to Vulkan Queues.
 *
 * Each QueueHandle represents a single Vulkan Queue. Many shared pointers to a single
 * QueueHandle may exist at a given time, e.g. if the GPU only provides a single queue
 * of the required type.
 */
class QueueManager
{
public:
	QueueManager(vk::raii::PhysicalDevice* phys, std::shared_ptr<vk::raii::Device> device);

	/// Get a handle to a compute queue
	std::shared_ptr<QueueHandle> GetComputeQueue(std::string name)
	{ return GetQueueWithFlags(vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer, name); }
	/// Get a handle to a render queue
	/// @note Currently this requires Graphics and Transfer capabilities to simplify texture transfer code in WaveformArea.
	std::shared_ptr<QueueHandle> GetRenderQueue(std::string name)
	{ return GetQueueWithFlags(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer, name); }
	/// Get a handle to a transfer queue
	std::shared_ptr<QueueHandle> GetTransferQueue(std::string name)
	{ return GetQueueWithFlags(vk::QueueFlagBits::eTransfer, name); }

	/// Get a handle to a queue that has the given flag bits set, allocating the queue if necessary,
	/// and set or append name to the queue name for debug
	std::shared_ptr<QueueHandle> GetQueueWithFlags(vk::QueueFlags flags, std::string name);

public:
	//non-copyable
	QueueManager(QueueManager const&) = delete;
	QueueManager& operator=(QueueManager const&) = delete;

protected:
	vk::raii::PhysicalDevice* m_phys;
	std::shared_ptr<vk::raii::Device> m_device;

	/// Mutex to guard allocations
	std::mutex m_mutex;

	struct QueueInfo
	{
		size_t Family;
		size_t Index;
		vk::QueueFlags Flags;
		std::shared_ptr<QueueHandle> Handle;
	};

	/// All queues available on the device
	std::vector<QueueInfo> m_queues;
};

#endif
