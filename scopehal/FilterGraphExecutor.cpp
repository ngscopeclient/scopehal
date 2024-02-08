/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduling

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

	pThis->DoExecutorThread(i);
}

void FilterGraphExecutor::DoExecutorThread(size_t i)
{
	//Create a queue and command buffer for this thread's accelerated processing
	std::shared_ptr<QueueHandle> queue(g_vkQueueManager->GetComputeQueue("FilterGraphExecutor[" + to_string(i) + "].queue"));
	vk::CommandPoolCreateInfo poolInfo(
		vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queue->m_family );
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

		//Evaluate nodes as they become available, then stop when there's nothing left to do
		FlowGraphNode* f;
		while( (f = GetNextRunnableNode()) != nullptr)
		{
			//Make sure the filter's inputs are where we need them
			auto loc = f->GetInputLocation();
			if(loc != Filter::LOC_DONTCARE)
			{
				bool expectGpuInput = (loc == Filter::LOC_GPU);
				bool expectCpuInput = (loc == Filter::LOC_CPU);
				for(size_t j=0; j<f->GetInputCount(); j++)
				{
					auto data = f->GetInput(j).GetData();
					if(data)
					{
						if(expectGpuInput)
							data->PrepareForGpuAccess();
						else if(expectCpuInput)
							data->PrepareForCpuAccess();
					}
				}
			}

			//Actually execute the filter
			f->Refresh(cmdbuf, queue);

			//Filter execution has completed, remove it from the running list and mark as completed
			lock_guard<mutex> lock2(m_mutex);
			m_runningNodes.erase(f);
			m_incompleteNodes.erase(f);

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
