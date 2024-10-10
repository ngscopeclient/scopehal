/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of FilterGraphExecutor
	@ingroup core
 */

#ifndef FilterGraphExecutor_h
#define FilterGraphExecutor_h

#include <condition_variable>
#include <atomic>

/**
	@brief Execution manager / scheduler for the filter graph
	@ingroup core
 */
class FilterGraphExecutor
{
public:
	FilterGraphExecutor(size_t numThreads = 8);
	~FilterGraphExecutor();

	void RunBlocking(const std::set<FlowGraphNode*>& nodes);

	FlowGraphNode* GetNextRunnableNode();

	///@brief Get the run times of the most recent filter graph evaluation
	std::map<FlowGraphNode*, int64_t> GetRunTimes()
	{
		std::lock_guard<std::mutex> lock(m_perfStatsMutex);
		return m_lastExecutionTime;
	}

protected:
	static void ExecutorThread(FilterGraphExecutor* pThis, size_t i);
	void DoExecutorThread(size_t i);

	void UpdateRunnable();

	///@brief Mutex for access to shared state
	std::mutex m_mutex;

	///@brief Nodes that have not yet been updated
	std::set<FlowGraphNode*> m_incompleteNodes;

	///@brief Nodes that have no dependencies and are eligible to run now
	std::set<FlowGraphNode*> m_runnableNodes;

	///@brief Nodes that are actively being run
	std::set<FlowGraphNode*> m_runningNodes;

	///@brief Set of thread contexts
	std::vector<std::unique_ptr<std::thread>> m_threads;

	///@brief Condition variable for waking up worker threads when work arrives
	std::condition_variable m_workerCvar;

	///@brief Mutex for access to m_workerCvar
	std::mutex m_workerCvarMutex;

	///@brief Condition variable for waking up main thread when work is complete
	std::condition_variable m_completionCvar;

	///@brief Mutex for access to m_completionCvar
	std::mutex m_completionCvarMutex;

	///@brief Indicates that all worker threads have finished executing this pass
	bool m_allWorkersComplete;

	///@brief Shutdown flag
	bool m_terminating;

	///@brief Performance statistics from previous execution
	std::map<FlowGraphNode*, int64_t> m_lastExecutionTime;

	///@brief Performance statistics from current execution
	std::map<FlowGraphNode*, int64_t> m_currentExecutionTime;

	///@brief Mutex for updating performance statistics
	std::mutex m_perfStatsMutex;
};

#endif
