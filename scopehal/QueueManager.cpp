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
	@brief Vulkan queue management
 */

#include <vulkan/vulkan_raii.hpp>

#include "log.h"
#include "QueueManager.h"

using namespace std;


extern bool g_hasDebugUtils;


QueueHandle::QueueHandle(std::shared_ptr<vk::raii::Device> device, size_t family, size_t index, string name)
: m_family(family)
, m_index(index)
, m_mutex()
, m_name()
, m_device(device)
, m_queue(make_unique<vk::raii::Queue>(*device, family, index))
, m_fence(nullptr)
{
	AddName(name);
}

QueueHandle::~QueueHandle()
{
	const lock_guard<recursive_mutex> lock(m_mutex);
	m_fence = nullptr;
	m_queue = nullptr;
	m_device = nullptr;
}

void QueueHandle::AddName(string name)
{
	const lock_guard<recursive_mutex> lock(m_mutex);

	if(m_name.size() != 0)
		m_name += ";";
	m_name += name;

	if(g_hasDebugUtils)
	{
		m_device->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eQueue,
				reinterpret_cast<uint64_t>(static_cast<VkQueue>(**m_queue)),
				m_name.c_str()));
	}
}

void QueueHandle::Submit(vk::raii::CommandBuffer const& cmdBuf)
{
	const lock_guard<recursive_mutex> lock(m_mutex);

	_waitFence();

	vk::SubmitInfo info({}, {}, *cmdBuf);
	m_fence = make_unique<vk::raii::Fence>(*m_device, vk::FenceCreateInfo());
	if(g_hasDebugUtils)
	{
		m_device->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eFence,
				reinterpret_cast<uint64_t>(static_cast<VkFence>(**m_fence)),
				m_name.c_str()));
	}
	m_queue->submit(info, **m_fence);
}

void QueueHandle::SubmitAndBlock(vk::raii::CommandBuffer const& cmdBuf)
{
	const lock_guard<recursive_mutex> lock(m_mutex);

	_waitFence();

	vk::SubmitInfo info({}, {}, *cmdBuf);
	m_fence = make_unique<vk::raii::Fence>(*m_device, vk::FenceCreateInfo());
	if(g_hasDebugUtils)
	{
		m_device->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eFence,
				reinterpret_cast<uint64_t>(static_cast<VkFence>(**m_fence)),
				m_name.c_str()));
	}
	m_queue->submit(info, **m_fence);
	_waitFence();
}

void QueueHandle::_waitFence()
{
	if(!m_fence)
		return;

	//Wait for any previous submit to finish
	while(vk::Result::eTimeout == m_device->waitForFences({**m_fence}, VK_TRUE, 1000 * 1000))
	{}

	m_fence = nullptr;
}


QueueManager::QueueManager(vk::raii::PhysicalDevice* phys, std::shared_ptr<vk::raii::Device> device)
: m_phys(phys)
, m_device(device)
, m_mutex()
, m_queues()
{
	auto families = phys->getQueueFamilyProperties();
	for(size_t family=0; family<families.size(); family++)
		for(size_t idx=0; idx<families[family].queueCount; idx++)
			m_queues.push_back(QueueInfo{family, idx, families[family].queueFlags, nullptr});
	//Sort the queues in ascending order of feature flag count
	//FIXME-CXX20 Use std::popcount() for sorting when we move to C++20
	static_assert(sizeof(vk::QueueFlags::MaskType) == sizeof(uint32_t));
	sort(m_queues.begin(), m_queues.end(),
		[](QueueInfo const& a, QueueInfo const& b) -> bool
		{
			size_t flag_count_a = 0;
			size_t flag_count_b = 0;
			for(size_t i=0; i<sizeof(vk::QueueFlags::MaskType)*8; i++)
			{
				if(static_cast<uint32_t>(a.Flags) & (1<<i))
					flag_count_a++;
				if(static_cast<uint32_t>(b.Flags) & (1<<i))
					flag_count_b++;
			}
			return flag_count_a < flag_count_b;
		});
	LogDebug("Sorted queues:\n");
	LogIndenter li;
	for(QueueInfo const& qi : m_queues)
		LogDebug("Family=%zu Index=%zu Flags=%08x\n", qi.Family, qi.Index, (uint32_t)qi.Flags);
}

shared_ptr<QueueHandle> QueueManager::GetQueueWithFlags(vk::QueueFlags flags, std::string name)
{
	const lock_guard<mutex> lock(m_mutex);

	//This will choose the first queue with matching flags that is not yet used.
	//If all queues with matching flags are used, the queue with the fewest
	//existing handles is chosen.
	//Because we sort m_queues by flag count in the constructor, the first match
	//should be the one with the least feature flags that satisfies the request.
	ssize_t chosenIdx = -1;
	for(size_t i=0; i<m_queues.size(); i++)
	{
		//Skip if flags don't match
		if((m_queues[i].Flags & flags) != flags)
			continue;

		//If handle is unallocated, use it right away
		if(m_queues[i].Handle.use_count() == 0)
		{
			LogDebug("QueueManager creating family=%zu index=%zu name=%s\n", m_queues[i].Family, m_queues[i].Index, name.c_str());
			m_queues[i].Handle = make_shared<QueueHandle>(
				m_device, m_queues[i].Family, m_queues[i].Index, name);
			return m_queues[i].Handle;
		}

		//Otherwise find the queue with the fewest existing handles
		if(chosenIdx == -1)
			chosenIdx = i;
		else if(m_queues[i].Handle.use_count() < m_queues[chosenIdx].Handle.use_count())
			chosenIdx = i;
	}

	if(chosenIdx < 0)
		LogFatal("Failed to locate a vulkan queue satisfying the flags 0x%x", (unsigned int)flags);

	LogTrace("QueueManager reusing handle idx=%zu name=%s for name=%s\n",
		chosenIdx, m_queues[chosenIdx].Handle->GetName().c_str(), name.c_str());
	m_queues[chosenIdx].Handle->AddName(name);

	return m_queues[chosenIdx].Handle;
}
