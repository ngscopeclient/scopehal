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
	@brief Declaration of QueueWrapper

	\ingroup core
 */
#ifndef QueueWrapper_h
#define QueueWrapper_h

class QueueLock;

/**
	@brief Mutexed wrapper around a Vulkan queue.

	There must be one and only one QueueWrapper for a given physical VkQueue but many QueueHandle's can point to it
 */
class QueueWrapper
{
public:
	QueueWrapper(std::shared_ptr<vk::raii::Device> device, size_t family, size_t index);
	~QueueWrapper();

	///@brief Append a name to the queue, used for debugging
	void AddName(std::string name)
	{
		const std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_names.emplace(name);
		UpdateName();
	}

	///@brief Remove a name to the queue, used for debugging
	void RemoveName(std::string name)
	{
		const std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_names.erase(name);
		UpdateName();
	}

	/**
		@brief Get the name of this queue

		(locks a mutex so can't be const even though it doesn't change anything)
	 */
	const std::string GetName()
	{
		std::string ret;
		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			ret = m_name;
		}
		return ret;
	}

	///@brief Get the mutex for the queue
	std::recursive_mutex& GetMutex()
	{ return m_mutex; }

	std::unique_ptr<vk::raii::Queue>& GetQueue()
	{ return m_queue; }

	std::shared_ptr<vk::raii::Device>& GetDevice()
	{ return m_device; }

public:
	//non-copyable
	QueueWrapper(QueueWrapper const&) = delete;
	QueueWrapper& operator=(QueueWrapper const&) = delete;

public:

	///@brief Family of the queue
	const size_t m_family;

	///@brief Index of this queue within the queue family
	const size_t m_index;

protected:

	void UpdateName();

	friend QueueLock;

	///@brief The mutex controlling access to the queue
	std::recursive_mutex m_mutex;

	///@brief Debug name of the queue
	std::string m_name;

	///@brief Set of names
	std::set<std::string> m_names;

	///@brief The Vulkan device this queue submits to
	std::shared_ptr<vk::raii::Device> m_device;

	///@brief The underlying Vulkan handle
	std::unique_ptr<vk::raii::Queue> m_queue;
};

#endif
