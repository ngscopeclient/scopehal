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
	@brief Implementation of LevelCrossingDetector
 */
#include "scopehal.h"
#include "LevelCrossingDetector.h"

using namespace std;

LevelCrossingDetector::LevelCrossingDetector()
{
	//Only initialize most stuff if we can actually run the shader (no bignum fallback, int64 is a hard requirement)
	if(g_hasShaderInt64)
	{
		m_zeroCrossingPipeline = make_unique<ComputePipeline>(
			"shaders/FindZeroCrossings.spv",
			2,
			sizeof(ZeroCrossingPushConstants));

		//don't bother with a CPU side allocation here
		m_temporaryResults.SetCpuAccessHint(AcceleratorBuffer<int64_t>::HINT_NEVER);
		m_temporaryResults.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);

		m_preGatherPipeline = make_unique<ComputePipeline>(
			"shaders/PreGather.spv",
			2,
			sizeof(PreGatherPushConstants));

		//we need this readable from the CPU to get the final index count
		m_gatherIndexes.SetCpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_gatherIndexes.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);

		m_gatherPipeline = make_unique<ComputePipeline>(
			"shaders/Gather.spv",
			3,
			sizeof(GatherPushConstants));

		m_outbuf.SetCpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_outbuf.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
	}

	//Still need output buffer
	else
	{
		m_outbuf.SetCpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_outbuf.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_NEVER);
	}
}

int64_t LevelCrossingDetector::FindZeroCrossings(
	UniformAnalogWaveform* wfm,
	float threshold,
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	//Fallback in case GPU has no int64 support
	if(!g_hasShaderInt64)
	{
		vector<int64_t> edges;
		Filter::FindZeroCrossings(wfm, threshold, edges);

		int64_t len = edges.size();
		m_outbuf.resize(len);
		memcpy(&m_outbuf[0], &edges[0], len*sizeof(int64_t));
		return len;
	}

	//This value experimentally gives the best speedup for an NVIDIA 2080 Ti vs an Intel Xeon Gold 6144
	//Maybe consider dynamic tuning in the future at initialization?
	const uint64_t numThreads = 8192;

	cmdBuf.begin({});

	//First shader pass: find edges and produce a sparse list
	size_t depth = wfm->size();
	ZeroCrossingPushConstants zpush;
	zpush.triggerPhase = wfm->m_triggerPhase;
	zpush.timescale = wfm->m_timescale;
	zpush.inputSize = depth;
	zpush.inputPerThread = (zpush.inputSize + numThreads) / numThreads;
	zpush.outputPerThread = zpush.inputPerThread + 1;
	zpush.threshold = threshold;
	m_temporaryResults.resize(zpush.outputPerThread * numThreads);

	m_zeroCrossingPipeline->BindBufferNonblocking(0, m_temporaryResults, cmdBuf, true);
	m_zeroCrossingPipeline->BindBufferNonblocking(1, wfm->m_samples, cmdBuf);
	const uint32_t compute_block_count = GetComputeBlockCount(numThreads, 64);
	m_zeroCrossingPipeline->Dispatch(cmdBuf, zpush,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	m_temporaryResults.MarkModifiedFromGpu();
	m_zeroCrossingPipeline->AddComputeMemoryBarrier(cmdBuf);

	//Second pass: find boundaries of each block to find where the output blocks start
	//(the very last entry here is going to be the total number of edges we found)
	PreGatherPushConstants ppush;
	ppush.numBlocks = numThreads+1;
	ppush.stride = zpush.outputPerThread;
	m_gatherIndexes.resize(numThreads + 1);

	m_preGatherPipeline->BindBufferNonblocking(0, m_gatherIndexes, cmdBuf, true);
	m_preGatherPipeline->BindBufferNonblocking(1, m_temporaryResults, cmdBuf);
	m_preGatherPipeline->Dispatch(cmdBuf, ppush, GetComputeBlockCount(numThreads+1, 64), 1);

	m_gatherIndexes.MarkModifiedFromGpu();
	m_preGatherPipeline->AddComputeMemoryBarrier(cmdBuf);

	//Third pass: final reduction
	GatherPushConstants gpush;
	gpush.numBlocks = numThreads;
	gpush.stride = zpush.outputPerThread;
	m_outbuf.resize(depth);

	m_gatherPipeline->BindBufferNonblocking(0, m_outbuf, cmdBuf, true);
	m_gatherPipeline->BindBufferNonblocking(1, m_temporaryResults, cmdBuf);
	m_gatherPipeline->BindBufferNonblocking(2, m_gatherIndexes, cmdBuf);
	m_gatherPipeline->Dispatch(cmdBuf, gpush, GetComputeBlockCount(numThreads, 64), 1);

	m_outbuf.MarkModifiedFromGpu();

	m_gatherIndexes.PrepareForCpuAccessNonblocking(cmdBuf);

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	//Grab the length off the GPU immediately then resize the buffer so we can use normal iterators on it
	//m_gatherIndexes.PrepareForCpuAccess();
	auto len = m_gatherIndexes[numThreads];
	m_outbuf.resize(len);
	return len;
}
