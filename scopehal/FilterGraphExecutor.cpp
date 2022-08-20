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

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FilterGraphExecutor::FilterGraphExecutor(size_t numThreads)
	: m_barrier(numThreads + 1)
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
	m_barrier.arrive_and_wait();
	for(auto& t : m_threads)
		t->join();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup for a run

/**
	@brief Evaluates the filter graph, blocking until execution has completed
 */
void FilterGraphExecutor::RunBlocking(set<Filter*>& filters)
{
	LogTrace("Initializing execution context with %zu filters\n", filters.size());

	m_incompleteFilters = filters;
	m_runnableFilters.clear();

	Filter::ClearAnalysisCache();

	//Wait for all worker threads to be ready (should be instantaneous, they should be waiting for us to get here)
	LogTrace("At barrier in main thread\n");
	m_barrier.arrive_and_wait();
	LogTrace("Barrier cleared in main thread\n");


	///////////////
	//DEBUG: old filter scheduler

	//Prepare to topologically sort filter nodes into blocks capable of parallel evaluation.
	//Block 0 may only depend on physical scope channels.
	//Block 1 may depend on decodes in block 0 or physical channels.
	//Block 2 may depend on 1/0/physical, etc.
	typedef vector<Filter*> FilterBlock;
	vector<FilterBlock> blocks;

	//Working set starts out aspThis->m_barrier.arrive_and_wait(); all decoders
	auto working = filters;

	//Each iteration, put all decodes that only depend on previous blocks into this block.
	for(int block=0; !working.empty(); block++)
	{
		FilterBlock current_block;
		for(auto w : working)
		{
			auto d = static_cast<Filter*>(w);

			//Check if we have any inputs that are still in the working set.
			bool ok = true;
			for(size_t i=0; i<d->GetInputCount(); i++)
			{
				auto in = d->GetInput(i).m_channel;
				if(working.find((Filter*)in) != working.end())
				{
					ok = false;
					break;
				}
			}

			//All inputs are in previous blocks, we're good to go for the current block
			if(ok)
				current_block.push_back(d);
		}

		//Anything we assigned this iteration shouldn't be in the working set for next time.
		//It does, however, have to get saved in the output block.
		for(auto d : current_block)
			working.erase(d);
		blocks.push_back(current_block);
	}

	//Evaluate the blocks, taking advantage of parallelism between them
	for(auto& block : blocks)
	{
		#pragma omp parallel for
		for(size_t i=0; i<block.size(); i++)
			block[i]->Refresh();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduling

/**
	@brief Returns the next filter available to run, blocking if none are ready.

	Returns null if there are no remaining filters to evaluate.
 */
Filter* FilterGraphExecutor::GetNextRunnableFilter()
{
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 	omp_ge

/**
	@brief Thread function to handle filter graph execution
 */
void FilterGraphExecutor::ExecutorThread(FilterGraphExecutor* pThis, size_t i)
{
	#ifdef __linux__
	pthread_setname_np(pthread_self(), "FilterGraph");
	#endif
	LogTrace("ExecutorThread %zu starting\n", i);

	while(true)
	{
		//Wait until the main thread starts a new round of execution
		LogTrace("ExecutorThread %zu at barrier\n", i);
		pThis->m_barrier.arrive_and_wait();
		LogTrace("ExecutorThread %zu cleared barrier\n", i);

		//If they woke us up because the context is being destroyed, we're done
		if(pThis->m_terminating)
			break;

		//Evaluate filter objects as they become available, then stop when there's nothing left to do
		Filter* f;
		while( (f = pThis->GetNextRunnableFilter()) != nullptr)
		{
			LogTrace("Evaluating %s in thread %zu\n", f->GetDisplayName().c_str(), i);
			f->Refresh();
		}
	}

	LogTrace("ExecutorThread %zu exiting\n", i);
}
