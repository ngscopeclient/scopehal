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

bool operator<(const QueueInfo& a, const QueueInfo& b)
{
	if(a.Family < b.Family)
		return true;
	if(a.Family > b.Family)
		return false;

	if(a.Index < b.Index)
		return true;
	return false;
}

QueueManager::QueueManager(vk::raii::PhysicalDevice* phys, std::shared_ptr<vk::raii::Device> device)
: m_phys(phys)
, m_device(device)
, m_mutex()
, m_queues()
{
	auto families = phys->getQueueFamilyProperties();
	for(size_t family=0; family<families.size(); family++)
	{
		for(size_t idx=0; idx<families[family].queueCount; idx++)
		{
			QueueInfo q =
			{
				family,
				idx,
				families[family].queueFlags,
				make_shared<QueueWrapper>(device, family, idx)
			};
			m_queues.push_back(make_shared<QueueInfo>(q));
		}
	}
	//Sort the queues in ascending order of feature flag count
	//FIXME-CXX20 Use std::popcount() for sorting when we move to C++20
	static_assert(sizeof(vk::QueueFlags::MaskType) == sizeof(uint32_t));
	sort(m_queues.begin(), m_queues.end(),
		[](shared_ptr<QueueInfo> const& a, shared_ptr<QueueInfo> const& b) -> bool
		{
			size_t flag_count_a = 0;
			size_t flag_count_b = 0;
			for(size_t i=0; i<sizeof(vk::QueueFlags::MaskType)*8; i++)
			{
				if(static_cast<uint32_t>(a->Flags) & (1<<i))
					flag_count_a++;
				if(static_cast<uint32_t>(b->Flags) & (1<<i))
					flag_count_b++;
			}
			return flag_count_a < flag_count_b;
		});
	LogDebug("Sorted queues:\n");
	LogIndenter li;
	for(auto& pq : m_queues)
		LogDebug("Family=%zu Index=%zu Flags=%08x\n", pq->Family, pq->Index, (uint32_t)pq->Flags);

	//Names for pools
	m_poolNames[QUEUE_POOL_RENDER] = "Render";
	m_poolNames[QUEUE_POOL_RASTERIZE] = "Rasterize";
	m_poolNames[QUEUE_POOL_DRIVER] = "Driver";
	m_poolNames[QUEUE_POOL_FILTER] = "Filter";
	m_poolNames[QUEUE_POOL_TRANSFER] = "Transfer";
	m_poolNames[QUEUE_POOL_MISC] = "Misc";

	/*
		Figure out how many queues we have that are *eligible* to be in each pool
		(have the minimum number of feature flags set to use it).

		This does not guarantee that the queue will actually be placed in the pool.
	 */
	std::map<QueuePoolID, std::vector<std::shared_ptr<QueueInfo>> > eligiblePools;
	auto renderFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
	auto computeFlags = vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
	auto transferFlags = vk::QueueFlagBits::eTransfer;
	for(auto pq : m_queues)
	{
		//Render pool needs graphics, compute, transfer
		if( (pq->Flags & renderFlags) == renderFlags)
			eligiblePools[QUEUE_POOL_RENDER].push_back(pq);

		//Transfer pool just needs transfer
		if( (pq->Flags & transferFlags) == transferFlags)
			eligiblePools[QUEUE_POOL_TRANSFER].push_back(pq);

		//All other pools need compute + transfer
		if( (pq->Flags & computeFlags) == computeFlags)
		{
			eligiblePools[QUEUE_POOL_RASTERIZE].push_back(pq);
			eligiblePools[QUEUE_POOL_DRIVER].push_back(pq);
			eligiblePools[QUEUE_POOL_FILTER].push_back(pq);
			eligiblePools[QUEUE_POOL_MISC].push_back(pq);
		}
	}

	//Print out the list of eligible queues for each pool
	LogTrace("Eligible queues:\n");
	for(auto it : m_poolNames)
	{
		LogIndenter li2;
		auto& queues = eligiblePools[it.first];
		LogTrace("%s:\n", it.second.c_str());
		for(auto& pq : queues)
		{
			LogIndenter li3;
			LogTrace("Family=%zu Index=%zu Flags=%08x\n", pq->Family, pq->Index, (uint32_t)pq->Flags);
		}
	}

	shared_ptr<QueueInfo> lastAllocatedQueue = nullptr;

	//If there is only one queue eligible to go in a pool, put it there. That's easy.
	set<shared_ptr<QueueInfo>> allocatedQueues;
	LogTrace("Queue allocation, pass 1: single choice\n");
	for(auto it : m_poolNames)
	{
		LogIndenter li2;
		auto& queues = eligiblePools[it.first];
		LogTrace("%s:\n", it.second.c_str());
		LogIndenter li3;

		//NO eligible queues! We can't proceed, something is wrong
		if(queues.size() == 0)
			LogFatal("No eligible %s queues found\n", it.second.c_str());

		//One queue found? Use it
		if(queues.size() == 1)
		{
			auto pq = queues[0];
			m_pools[it.first].push_back(pq);
			allocatedQueues.emplace(pq);
			LogTrace("Family=%zu Index=%zu Flags=%08x\n", pq->Family, pq->Index, (uint32_t)pq->Flags);
			lastAllocatedQueue = pq;
		}
	}

	//Allocate one queue to each pool, if we have enough
	LogTrace("Queue allocation, pass 2: one queue per pool\n");
	for(auto it : m_poolNames)
	{
		//If there's already a queue in this pool, don't add another yet
		if(!m_pools[it.first].empty())
			continue;

		LogIndenter li2;
		auto& queues = eligiblePools[it.first];
		LogTrace("%s:\n", it.second.c_str());
		LogIndenter li3;

		//Look for an unallocated queue and assign it
		for(auto pq : queues)
		{
			if(allocatedQueues.find(pq) == allocatedQueues.end())
			{
				m_pools[it.first].push_back(pq);
				allocatedQueues.emplace(pq);
				LogTrace("Family=%zu Index=%zu Flags=%08x\n", pq->Family, pq->Index, (uint32_t)pq->Flags);
				lastAllocatedQueue = pq;
				break;
			}
		}
	}

	//If we still have some pools with no queues, we don't have enough to go around.
	//Need to oversubscribe.
	//For now, re-issue the last queue (stuff earlier in the list prefers to have dedicated queues)
	LogTrace("Queue allocation, pass 3: oversubscription\n");
	for(auto it : m_poolNames)
	{
		//If there's already a queue in this pool, don't add any others
		if(!m_pools[it.first].empty())
			continue;

		LogIndenter li2;
		LogTrace("%s:\n", it.second.c_str());
		LogIndenter li3;

		//There are no unallocated queues! We need to oversubscribe.
		//Use the last allocated queue again
		auto pq = lastAllocatedQueue;
		m_pools[it.first].push_back(pq);
		LogTrace("Family=%zu Index=%zu Flags=%08x\n", pq->Family, pq->Index, (uint32_t)pq->Flags);
	}

	//TODO: If we are on an NVIDIA platform and have a lot of queues, allocate extras to the filter and driver pools

	//Print out the final queue assignments
	LogTrace("Final queue assignments:\n");
	for(auto it : m_poolNames)
	{
		LogIndenter li2;
		auto& queues = m_pools[it.first];
		LogTrace("%s:\n", it.second.c_str());
		for(auto& pq : queues)
		{
			LogIndenter li3;
			LogTrace("Family=%zu Index=%zu Flags=%08x\n", pq->Family, pq->Index, (uint32_t)pq->Flags);
		}
	}
}

/**
	@brief Get a queue handle for a specific pool, preferring one that has less contention
 */
shared_ptr<QueueHandle> QueueManager::GetQueueFromPool(QueuePoolID id, std::string name)
{
	const lock_guard<mutex> lock(m_mutex);

	//This will choose the first queue in the target pool that is not yet used.
	//If all queues in the pool are used, the queue with the fewest existing handles is chosen.
	ssize_t chosenIdx = -1;
	auto& pool = m_pools[id];
	for(size_t i=0; i<pool.size(); i++)
	{
		//If handle is unallocated, use it right away
		if(pool[i]->Handle.use_count() <= 1)
		{
			LogTrace("Creating family=%zu index=%zu name=%s\n",
				pool[i]->Family, pool[i]->Index, name.c_str());

			return make_shared<QueueHandle>(pool[i]->Handle, name);
		}

		//If we did not have a queue at all, this one will work. Use it until/unless we find something better
		if(chosenIdx == -1)
			chosenIdx = i;

		//If the new queue has less handles, prefer it
		else if(pool[i]->Handle.use_count() < m_queues[chosenIdx]->Handle.use_count())
		{
			//If the new queue has more flags, don't prefer it.
			//This prevents e.g. blocking the graphics queue when allocating compute queues
			if(m_queues[chosenIdx]->Flags < pool[i]->Flags )
				continue;

			chosenIdx = i;
		}
	}

	LogTrace("Reusing handle idx=%zu name=%s rc=%ld for name=%s\n",
		chosenIdx,
		pool[chosenIdx]->Handle->GetName().c_str(),
		pool[chosenIdx]->Handle.use_count(),
		name.c_str());

	return make_shared<QueueHandle>(pool[chosenIdx]->Handle, name);
}

shared_ptr<QueueHandle> QueueManager::GetQueueWithFlags(vk::QueueFlags flags, std::string name)
{
	const lock_guard<mutex> lock(m_mutex);

	//TODO: smart allocation from queue pools to create less contention

	//This will choose the first queue with matching flags that is not yet used.
	//If all queues with matching flags are used, the queue with the fewest
	//existing handles is chosen.
	//Because we sort m_queues by flag count in the constructor, the first match
	//should be the one with the least feature flags that satisfies the request.
	ssize_t chosenIdx = -1;
	for(size_t i=0; i<m_queues.size(); i++)
	{
		//Skip if flags don't match
		if((m_queues[i]->Flags & flags) != flags)
			continue;

		//If handle is unallocated, use it right away
		if(m_queues[i]->Handle.use_count() <= 1)
		{
			LogTrace("Creating family=%zu index=%zu name=%s\n",
				m_queues[i]->Family, m_queues[i]->Index, name.c_str());

			return make_shared<QueueHandle>(m_queues[i]->Handle, name);
		}

		//If we did not have a queue at all, this one will work. Use it until/unless we find something better
		if(chosenIdx == -1)
			chosenIdx = i;

		//If the new queue has less handles, prefer it
		else if(m_queues[i]->Handle.use_count() < m_queues[chosenIdx]->Handle.use_count())
		{
			//If the new queue has more flags, don't prefer it.
			//This prevents e.g. blocking the graphics queue when allocating compute queues
			if(m_queues[chosenIdx]->Flags < m_queues[i]->Flags )
				continue;

			chosenIdx = i;
		}
	}

	if(chosenIdx < 0)
		LogFatal("Failed to locate a vulkan queue satisfying the flags 0x%x", (unsigned int)flags);

	LogTrace("Reusing handle idx=%zu name=%s rc=%ld for name=%s\n",
		chosenIdx,
		m_queues[chosenIdx]->Handle->GetName().c_str(),
		m_queues[chosenIdx]->Handle.use_count(),
		name.c_str());

	return make_shared<QueueHandle>(m_queues[chosenIdx]->Handle, name);
}
