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

#ifndef FilterGraphExecutor_h
#define FilterGraphExecutor_h

#include <condition_variable>
#include <atomic>

/**
	@brief Simple std::barrier replacement because not all supported platforms currently implement std::barrier
 */
class Barrier
{
public:
	Barrier(size_t expectedThreads)
	: m_expectedThreads(expectedThreads)
	, m_waitingThreads(expectedThreads)
	, m_clearedThreads(expectedThreads)
	{}

	/**
		@brief Blocks until all of our threads have reached a sync point

		Two-phase implementation so we can reset counters for a second invocation
	 */
	void arrive_and_wait()
	{
		//Decrement number of waiting threads
		size_t numWaiting = --m_waitingThreads;

		//If we were the last thread, notify everyone else
		LogTrace("First sync: numWaiting=%zu\n", numWaiting);
		if(numWaiting == 0)
		{
			LogTrace("notifying all A\n");
			m_clearedThreads -= m_expectedThreads;
			m_cvarA.notify_all();
		}

		//If not, block until notified
		else
		{
			LogTrace("Blocking A\n");
			while(m_waitingThreads != 0)
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cvarA.wait(lock);
				LogTrace("cvarA woke up, waitingThreads = %zu\n", (size_t)m_waitingThreads);
			}
		}

		//At this point we've passed the sync point.
		//Atomically increment the "cleared" count so we know everyone's passed it.
		size_t numCleared = ++m_clearedThreads;
		LogTrace("Second sync: numWaiting=%zu\n", numWaiting);
		if(numCleared == m_expectedThreads)
		{
			LogTrace("notifying all B\n");
			m_waitingThreads += m_expectedThreads;
			m_cvarB.notify_all();
		}

		//If not, block until notified
		else
		{
			LogTrace("Blocking B\n");
			while(numCleared != m_expectedThreads)
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cvarB.wait(lock);
				LogTrace("cvarB woke up, numCleared = %zu\n", (size_t)numCleared);
			}
		}
	}

protected:
	size_t m_expectedThreads;

	std::atomic<size_t> m_waitingThreads;
	std::atomic<size_t> m_clearedThreads;

	std::mutex m_mutex;

	std::condition_variable m_cvarA;
	std::condition_variable m_cvarB;
};

/**
	@brief Execution manager / scheduler for the filter graph
 */
class FilterGraphExecutor
{
public:
	FilterGraphExecutor(size_t numThreads = 8);
	~FilterGraphExecutor();

	void RunBlocking(std::set<Filter*>& filters);

	Filter* GetNextRunnableFilter();

protected:
	static void ExecutorThread(FilterGraphExecutor* pThis, size_t i);

	std::mutex m_mutex;

	//Filters that have not yet been updated
	std::set<Filter*> m_incompleteFilters;

	//Filters that have no dependencies and are eligible to run now
	std::vector<Filter*> m_runnableFilters;

	//Set of thread contexts
	std::vector<std::unique_ptr<std::thread>> m_threads;

	//Barrier for waking up worker threads when work arrives
	Barrier m_barrier;

	//Shutdown flag
	bool m_terminating;
};

#endif
