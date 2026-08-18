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
#include "FusibleShader.h"
#include "ShaderBaker.h"

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

	////////////////////////////////////////////////

	/*
	//TEST: try to dynamically generate the shader
	auto es = make_shared<FusibleShader>("EmphasisFilter.glsl", "EmphasisFilter");
	auto us = make_shared<FusibleShader>("UpsampleFilter.glsl", "EmphasisFilter");

	auto stage1 = make_shared<BakedShaderStage>(es);
	auto stage2 = make_shared<BakedShaderStage>(us);

	ShaderBaker baker;
	baker.AddStage(stage1);
	baker.AddStage(stage2);

	auto str = baker.Bake();
	LogDebug("BAKED SHADER\n%s\n", str.c_str());
	*/

	//////////////////////////////////////////

	LogTrace("Start graph refresh\n");
	LogIndenter li;

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

	LogTrace("Graph refresh done\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduling

void FilterGraphExecutor::FindConcurrentNodes(
	FlowGraphNode* anchor,
	set<FlowGraphNode*>& workingSet,
	bool& needBegin,
	bool& needEnd)
{
	//All physical instrument inputs can be chained if eligible to run
	//(assume none use vulkan, this might break if they do?)
	auto chan = dynamic_cast<InstrumentChannel*>(anchor);
	if(chan && chan->GetInstrument() != nullptr)
	{
		LogTrace("Anchor is physical instrument channel, looking for more\n");

		for(auto f : m_runnableNodes)
		{
			if(f == anchor)
				continue;

			//Physical instrument channels are good
			auto fchan = dynamic_cast<InstrumentChannel*>(f);
			if(fchan && (fchan->GetInstrument() != nullptr) )
			{
				LogTrace("Adding node %s\n", GetName(f).c_str());
				workingSet.emplace(f);
				continue;
			}

			//Import / waveform generation filters need to run early to get them out of the way too
			//TODO: some generation filters may use vulkan so that will influence our dispatching
			//but for now assume they're lightweight
			auto t = dynamic_cast<Filter*>(f);
			if(t && (t->GetCategory() == Filter::CAT_GENERATION) )
			{
				LogTrace("Adding node %s\n", GetName(f).c_str());
				workingSet.emplace(t);
			}
		}
	}

	else
	{
		//Check flags on the anchor node
		auto flags = anchor->GetExecutionCapabilitiesMask();

		//Short names for some long flags
		const uint32_t canAppend =
			(uint32_t)FlowGraphNode::ExecutionCapabilities::CommandBufferAppend;
		const uint32_t canTailChain =
			(uint32_t)FlowGraphNode::ExecutionCapabilities::CommandBufferTailCall;
		const uint32_t isVulkan =
			(uint32_t)FlowGraphNode::ExecutionCapabilities::VulkanOnly;

		const uint32_t sourceFlags = canTailChain | isVulkan;
		const uint32_t sinkFlags = canAppend | isVulkan | canTailChain;

		//If it's vulkan-only and we can tail chain, look for more stuff
		needBegin = (flags & canAppend) != 0;
		needEnd = (flags & canTailChain) != 0;
		if( (flags & sourceFlags) == sourceFlags )
		{
			LogTrace("Anchor can tail chain, looking for more nodes\n");

			for(auto f : m_runnableNodes)
			{
				if(f == anchor)
					continue;
				auto mask = f->GetExecutionCapabilitiesMask();

				//Tail call capability required for now if we already have stuff in the working set
				//because we can't guarantee a non-tail-callable node is going to execute
				//at the end of the batch
				if( (mask & sinkFlags) == sinkFlags )
				{
					LogTrace("Adding node %s\n", GetName(f).c_str());
					workingSet.emplace(f);
				}
			}
		}
	}

	LogTrace("Found %zu nodes\n", workingSet.size());
}
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
			set<FlowGraphNode*> workingSet;
			FlowGraphNode* anchor = nullptr;
			if(!m_runnableNodes.empty())
			{
				anchor = *m_runnableNodes.begin();
				LogTrace("Anchor node is %s\n", GetName(anchor).c_str());
				workingSet.emplace(anchor);

				//Look for more stuff to run alongside it
				bool needBegin = false;
				bool needEnd = false;
				FindConcurrentNodes(anchor, workingSet, needBegin, needEnd);
				MakeBatchForNodes(batch, workingSet, needBegin, needEnd);

				//Look for next hop nodes we can run after a barrier
				//(unless the last node includes a submit, in which case stop)
				while(needEnd)
				{
					if(!FindNextHopNodes(batch))
						break;
				}

				break;
			}
		}

		//Still nothing to run? Block
		unique_lock<mutex> lock(m_workerCvarMutex);
		m_workerCvar.wait(lock);
	}

	return batch;
}

void FilterGraphExecutor::MakeBatchForNodes(
	SubmitBatch& batch,
	set<FlowGraphNode*>& workingSet,
	bool needBegin,
	bool needEnd)
{
	LogTrace("Making batch with %zu nodes (needBegin=%d, needEnd=%d)\n",
		workingSet.size(), needBegin, needEnd);
	ConcurrentDispatchBatch cbatch(needBegin, needEnd, workingSet);
	for(auto f : workingSet)
	{
		m_runnableNodes.erase(f);
		m_runningNodes.emplace(f);
	}
	batch.AddBatch(cbatch);
}

/**
	@brief Searches for nodes that will be eligible to run once anything in the batch has run and adds it

	Assumes m_mutex is locked

	@return True if we should keep searching for more hops, false if nothing more to do
 */
bool FilterGraphExecutor::FindNextHopNodes(SubmitBatch& batch)
{
	LogTrace("Looking for next-hop nodes\n");
	LogIndenter li;

	set<FlowGraphNode*> nodes;

	if(m_incompleteNodes.empty())
		return false;

	//Get the nodes already in the batch
	//These can't unblock filters in OTHER batches, as we haven't submitted them, so aren't "complete" WRT scheduler
	//but they can unblock filters in *this* batch since we can put a queue barrier between them
	set<FlowGraphNode*> pending = batch.GetNodes();

	//Don't look at anything in m_runnableNodes, we already considered those

	//Mask required for new nodes
	const uint32_t nextHopMask =
		(uint32_t)FlowGraphNode::ExecutionCapabilities::CommandBufferAppend |
		(uint32_t)FlowGraphNode::ExecutionCapabilities::VulkanOnly;
	const uint32_t tailCallMask = (uint32_t)FlowGraphNode::ExecutionCapabilities::CommandBufferTailCall;

	//Look for new filters that are eligible to run
	for(auto f : m_incompleteNodes)
	{
		//If it's already running (this includes the pending queue, so no need to check it here)
		//no point in starting it again, skip it
		if(m_runningNodes.find(f) != m_runningNodes.end())
			continue;

		//If this node is not purely GPU based, stop.
		//It might do CPU processing beforehand that depends on data we haven't generated yet!
		//Also bail if it can't be appended to an open command buffer.
		auto fmask = f->GetExecutionCapabilitiesMask();
		if( (fmask & nextHopMask) != nextHopMask)
			continue;

		//If it's not tail call capable, stop.
		//We will add append-only nodes in a separate pass at the very end if we found nothing else
		if( (fmask & tailCallMask) != tailCallMask )
			continue;

		//Not actively running.
		//Is it blocked by anything earlier in the batch?
		bool ok = true;
		for(size_t i=0; i<f->GetInputCount(); i++)
		{
			auto in = f->GetInput(i).m_channel;

			//If the source of this input is already done, we're good
			if(m_incompleteNodes.find(in) == m_incompleteNodes.end())
				continue;

			//If the source is not the current batch, we're blocked - stall
			if(pending.find(in) == pending.end())
			{
				ok = false;
				break;
			}
		}

		//Not blocked, it's runnable. Add to the batch
		if(ok)
		{
			LogTrace("Adding node %s\n", GetName(f).c_str());
			nodes.emplace(f);
		}
	}

	//If we found nodes in the first pass, append them to the batch and stop
	if(!nodes.empty())
	{
		MakeBatchForNodes(batch, nodes, true, true);
		return true;
	}

	//If nothing found, do a second pass but allow append-only nodes
	//These have to run in their own concurrent batch because they have to be at the end of the SubmitBatch
	else
	{
		for(auto f : m_incompleteNodes)
		{
			//If it's already running (this includes the pending queue, so no need to check it here)
			//no point in starting it again, skip it
			if(m_runningNodes.find(f) != m_runningNodes.end())
				continue;

			//If this node is not purely GPU based, stop.
			//It might do CPU processing beforehand that depends on data we haven't generated yet!
			//Also bail if it can't be appended to an open command buffer.
			auto fmask = f->GetExecutionCapabilitiesMask();
			if( (fmask & nextHopMask) != nextHopMask)
				continue;

			//If it's tail call capable, it was handled elsewhere, skip
			if( (fmask & tailCallMask) == tailCallMask )
				continue;

			//Not actively running.
			//Is it blocked by anything earlier in the batch?
			bool ok = true;
			for(size_t i=0; i<f->GetInputCount(); i++)
			{
				auto in = f->GetInput(i).m_channel;

				//If the source of this input is already done, we're good
				if(m_incompleteNodes.find(in) == m_incompleteNodes.end())
					continue;

				//If the source is not the current batch, we're blocked - stall
				if(pending.find(in) == pending.end())
				{
					ok = false;
					break;
				}
			}

			//Not blocked, it's runnable. Add to the batch and stop
			if(ok)
			{
				LogTrace("Adding append-only node %s\n", GetName(f).c_str());
				nodes.emplace(f);
				MakeBatchForNodes(batch, nodes, true, false);

				//We cannot append anything else to this batch if we get here
				return false;
			}
		}

		//Nothing found on the second pass if we get here.
		//We're truly out of available work.
		return false;
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
	std::shared_ptr<QueueHandle> queue(
		g_vkQueueManager->GetQueueFromPool(
			QueueManager::QUEUE_POOL_FILTER,
			"FilterGraphExecutor[" + to_string(i) + "].queue"));
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
