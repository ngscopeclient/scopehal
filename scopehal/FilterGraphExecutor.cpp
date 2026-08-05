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
	@brief Implementation of FilterGraphExecutor
	@ingroup core
 */

#include "scopehal.h"
#include <shared_mutex>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SubmitBatch

void SubmitBatch::Run(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	if(m_batches.empty())
		return;

	LogTrace("Running batch\n");
	LogIndenter li;

	//Open command buffer if needed for first node
	if(m_batches[0].GetNeedBegin())
		cmdBuf.begin({});

	//Run the batches
	auto nbatches = m_batches.size();
	for(size_t i=0; i<nbatches; i++)
	{
		auto& b = m_batches[i];
		b.Run(cmdBuf, queue);

		//Add barriers between batches if tail calling
		if(b.GetNeedEnd() && (i+1 < nbatches) )
			ComputePipeline::AddComputeMemoryBarrier(cmdBuf);
	}

	//Submit if needed
	if(m_batches[m_batches.size() - 1].GetNeedEnd())
	{
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
	}
}

set<FlowGraphNode*> SubmitBatch::GetNodes()
{
	set<FlowGraphNode*> ret;

	for(auto& b : m_batches)
	{
		auto& nodes = b.GetNodes();
		for(auto n : nodes)
			ret.emplace(n);
	}

	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FilterGraphExecutor::FilterGraphExecutor(size_t numThreads)
	: m_allWorkersComplete(true)
	, m_terminating(false)
{
	//Create our thread pool
	for(size_t i=0; i<numThreads; i++)
		m_threads.push_back(make_unique<thread>(&FilterGraphExecutor::ExecutorThread, this, i));
}

FilterGraphExecutor::~FilterGraphExecutor()
{
	//Terminate worker threads
	m_terminating = true;
	m_workerCvar.notify_all();
	for(auto& t : m_threads)
		t->join();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup for a run

/**
	@brief Evaluates the filter graph, blocking until execution has completed
 */
void FilterGraphExecutor::RunBlocking(const set<FlowGraphNode*>& nodes)
{
	//Nothing to do if we have no nodes to run
	if(nodes.empty())
		return;

	{
		lock_guard<mutex> lock(m_perfStatsMutex);
		m_currentExecutionTime.clear();
	}

	{
		lock_guard<mutex> lock(m_mutex);

		if(!m_allWorkersComplete)
			LogWarning("Entering RunBlocking() but not all workers are complete from previous run\n");

		m_incompleteNodes = nodes;
		m_incompleteNodes.erase(nullptr);	//don't crash if a null filter somehow ended up in the list

		m_runnableNodes.clear();
		m_allWorkersComplete = false;

		Filter::ClearAnalysisCache();
	}

	//Wake up our workers
	m_workerCvar.notify_all();

	//Block until they're finished
	while(true)
	{
		unique_lock<mutex> lock(m_completionCvarMutex);
		m_completionCvar.wait(lock, [this]{return m_allWorkersComplete;});

		lock_guard<mutex> lock2(m_mutex);
		if(m_runnableNodes.empty())
			break;
	}

	//Update global performance stats
	{
		lock_guard<mutex> lock(m_perfStatsMutex);

		//For now, fixed half life exponential moving average
		float halflife = 8;
		float decay = 1 / pow(2, 1/halflife);

		//TODO: staleness or removing of some sort for old entries?

		//Add the new data
		for(auto& it : m_currentExecutionTime)
			m_lastExecutionTime[it.first] = (m_lastExecutionTime[it.first] * decay) + (it.second * (1-decay));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduling

/**
	@brief Returns the next batch of filters to run
 */
SubmitBatch FilterGraphExecutor::GetNextBatch()
{
	LogTrace("Filling work batch\n");
	LogIndenter li;

	SubmitBatch batch;

	while(true)
	{
		//Check for stuff
		{
			lock_guard<mutex> lock(m_mutex);

			//Nothing left to run? Stop
			if(m_incompleteNodes.empty())
				break;

			//Nothing ready to run? Update the run queue
			if(m_runnableNodes.empty())
				UpdateRunnable();

			//If there is something ready to run, grab it
			set<FlowGraphNode*> set;
			FlowGraphNode* anchor = nullptr;
			if(!m_runnableNodes.empty())
			{
				anchor = *m_runnableNodes.begin();
				LogTrace("Anchor node is %s\n", GetName(anchor).c_str());
				set.emplace(anchor);

				//Check flags on the anchor node
				auto flags = anchor->GetExecutionCapabilitiesMask();

				//Short names for some long flags
				const uint32_t canAppend =
					(uint32_t)FlowGraphNode::ExecutionCapabilities::CommandBufferAppend;
				const uint32_t canTailChain =
					(uint32_t)FlowGraphNode::ExecutionCapabilities::CommandBufferTailCall;
				const uint32_t isVulkan =
					(uint32_t)FlowGraphNode::ExecutionCapabilities::VulkanOnly;

				//If it's vulkan-only and we can tail chain, look for more stuff
				bool needBegin = (flags & canAppend) != 0;
				bool needEnd = (flags & canTailChain) != 0;
				if(flags & (canTailChain | isVulkan))
				{
					LogTrace("Anchor can tail chain, looking for more nodes\n");

					for(auto f : m_runnableNodes)
					{
						if(f == anchor)
							continue;
						auto mask = f->GetExecutionCapabilitiesMask();
						if(mask & (canAppend | isVulkan) )
						{
							LogTrace("Adding node %s\n", GetName(f).c_str());
							set.emplace(f);

							if( (mask & canTailChain) == 0)
							{
								needEnd = false;
								LogTrace("New node is not tail chain capable, stopping\n");
								break;
							}
						}
					}
				}

				//Make a concurrent batch and mark everything in it as running
				LogTrace("Making batch with %zu nodes (needBegin=%d, needEnd=%d)\n",
					set.size(), needBegin, needEnd);
				ConcurrentDispatchBatch cbatch(needBegin, needEnd, set);
				for(auto f : set)
				{
					m_runnableNodes.erase(f);
					m_runningNodes.emplace(f);
				}
				batch.AddBatch(cbatch);
				break;
			}
		}

		//Still nothing to run? Block
		unique_lock<mutex> lock(m_workerCvarMutex);
		m_workerCvar.wait(lock);
	}

	return batch;
}

/**
	@brief Returns the next filter available to run, blocking if none are ready.

	Returns null if there are no remaining filters to evaluate.
 */
FlowGraphNode* FilterGraphExecutor::GetNextRunnableNode()
{
	while(true)
	{
		//Check for stuff
		{
			lock_guard<mutex> lock(m_mutex);

			//Nothing left to run? Stop
			if(m_incompleteNodes.empty())
				return nullptr;

			//Nothing ready to run? Update the run queue
			if(m_runnableNodes.empty())
				UpdateRunnable();

			//If there is something ready to run, grab it
			if(!m_runnableNodes.empty())
			{
				auto f = *m_runnableNodes.begin();
				m_runnableNodes.erase(f);
				m_runningNodes.emplace(f);
				return f;
			}
		}

		//Still nothing to run? Block
		unique_lock<mutex> lock(m_workerCvarMutex);
		m_workerCvar.wait(lock);
	}
}

/**
	@brief Searches m_incompleteNodes for any that are unblocked, and adds them to m_runnableNodes

	Assumes m_mutex is locked
 */
void FilterGraphExecutor::UpdateRunnable()
{
	//Do nothing if we already have other filters marked runnable
	if(!m_runnableNodes.empty())
		return;

	//Look for new filters that are eligible to run
	for(auto f : m_incompleteNodes)
	{
		//If the filter is already running, nothing we can do
		if(m_runningNodes.find(f) != m_runningNodes.end())
			continue;

		//Not actively running.
		//Is it blocked by any of our incomplete filters?
		bool ok = true;
		for(size_t i=0; i<f->GetInputCount(); i++)
		{
			auto in = f->GetInput(i).m_channel;
			if(m_incompleteNodes.find(in) != m_incompleteNodes.end())
			{
				ok = false;
				break;
			}
		}

		//Not blocked. It's runnable.
		if(ok)
			m_runnableNodes.emplace(f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 	Main parallel execution logic

/**
	@brief Thread function to handle filter graph execution
 */
void FilterGraphExecutor::ExecutorThread(FilterGraphExecutor* pThis, size_t i)
{
	#ifdef __linux__
	pthread_setname_np(pthread_self(), "FilterGraph");
	#endif

	//Make locale handling thread safe on Windows
	#ifdef _WIN32
	_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
	Unit::SetDefaultLocale();
	#endif

	pThis->DoExecutorThread(i);
}

void FilterGraphExecutor::DoExecutorThread(size_t i)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range range("FilterGraphExecutor::DoExecutorThread");
	#endif

	//Create a queue and command buffer for this thread's accelerated processing
	std::shared_ptr<QueueHandle> queue(g_vkQueueManager->GetComputeQueue("FilterGraphExecutor[" + to_string(i) + "].queue"));
	vk::CommandPoolCreateInfo poolInfo(
		vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queue->GetQueue()->m_family );
	vk::raii::CommandPool pool(*g_vkComputeDevice, poolInfo);

	vk::CommandBufferAllocateInfo bufinfo(*pool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffer cmdbuf(std::move(vk::raii::CommandBuffers(*g_vkComputeDevice, bufinfo).front()));

	if(g_hasDebugUtils)
	{
		string prefix = string("FilterGraphExecutor[") + to_string(i) + "]";

		string poolname = prefix + ".pool";
		string bufname = prefix + ".cmdbuf";

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eCommandPool,
				reinterpret_cast<uint64_t>(static_cast<VkCommandPool>(*pool)),
				poolname.c_str()));

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eCommandBuffer,
				reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*cmdbuf)),
				bufname.c_str()));
	}

	//Main loop
	while(true)
	{
		{
			//Wait until the main thread starts a new round of execution, or the timeout elapses
			//When we time out, check if we're shutting down
			unique_lock<mutex> lock(m_workerCvarMutex);
			m_workerCvar.wait_for(lock, chrono::milliseconds(50));
		}

		//If they woke us up because the context is being destroyed, we're done
		if(m_terminating)
			break;

		//If we're already done, nothing to do
		if(m_allWorkersComplete)
			continue;

		//Get the next batch of work
		while(true)
		{
			//Pull the next batch from the scheduler and stop if it has no more work for us
			SubmitBatch batch = GetNextBatch();
			if(batch.empty())
				break;

			//Get the list of filters in the batch
			auto filters = batch.GetNodes();
			LogTrace("Runner %zu: got batch of %zu nodes\n", i, filters.size());

			//Run the batch
			double start = GetTime();
			batch.Run(cmdbuf, queue);
			double dt = GetTime() - start;
			int64_t fs = dt * FS_PER_SECOND;

			//Update performance stats
			{
				lock_guard<mutex> slock(m_perfStatsMutex);
				for(auto f : filters)
					m_currentExecutionTime[f] = fs;
			}

			//Filter execution has completed, remove them from the running list and mark as completed
			{
				lock_guard<mutex> lock2(m_mutex);
				for(auto f : filters)
				{
					m_runningNodes.erase(f);
					m_incompleteNodes.erase(f);
				}
			}

			//Wake up all threads that might have been waiting on this filter to complete
			m_workerCvar.notify_all();
		}

		//We have no more filters to run.
		//If this was the last filter (nothing left incomplete), we're done - wake up the main thread
		bool empty = false;
		{
			lock_guard<mutex> lock2(m_mutex);
			empty = m_incompleteNodes.empty();
		}
		if(empty)
		{
			{
				lock_guard<mutex> lock3(m_completionCvarMutex);
				m_allWorkersComplete = true;
			}

			m_completionCvar.notify_all();
		}
	}
}
