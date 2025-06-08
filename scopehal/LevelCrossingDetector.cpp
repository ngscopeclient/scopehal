/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
	//Only initialize if we can actually run the shader (no bignum fallback, int64 is a hard requirement)
	if(g_hasShaderInt64)
	{
		m_zeroCrossingPipeline = make_unique<ComputePipeline>(
			"shaders/FindZeroCrossings.spv",
			2,
			sizeof(ZeroCrossingPushConstants));

		m_temporaryResults.SetCpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_temporaryResults.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
	}
}

void LevelCrossingDetector::FindZeroCrossings(
	UniformAnalogWaveform* wfm,
	float threshold,
	vk::raii::CommandBuffer& cmdBuf,
	std::shared_ptr<QueueHandle> queue)
{
	//Fallback in case GPU has no int64 support
	if(!g_hasShaderInt64)
	{
		//TODO
		//Filter::FindZeroCrossings(wfm, threshold, edges);
		return;
	}

	//TODO: we  should tune this
	const uint64_t numThreads = 1024;

	//Calculate some config
	size_t depth = wfm->size();
	ZeroCrossingPushConstants constants;
	constants.triggerPhase = wfm->m_triggerPhase;
	constants.timescale = wfm->m_timescale;
	constants.inputSize = depth;
	constants.inputPerThread = (constants.inputSize + numThreads) / numThreads;
	constants.outputPerThread = constants.inputPerThread + 1;
	constants.threshold = threshold;
	m_temporaryResults.resize(constants.outputPerThread * numThreads);

	cmdBuf.begin({});

	//Run the first-pass shader to find the edges in a sparse list
	m_zeroCrossingPipeline->BindBufferNonblocking(0, m_temporaryResults, cmdBuf, true);
	m_zeroCrossingPipeline->BindBufferNonblocking(1, wfm->m_samples, cmdBuf);
	const uint32_t compute_block_count = GetComputeBlockCount(numThreads, 64);
	m_zeroCrossingPipeline->Dispatch(cmdBuf, constants,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);
	m_temporaryResults.MarkModifiedFromGpu();

	//TODO: reduction shaders

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	{
		m_temporaryResults.PrepareForCpuAccess();
		LogNotice("GPU thread 0 found %" PRIi64 " timestamps\n", m_temporaryResults[0]);

		LogIndenter li;
		for(size_t i=0; i<5; i++)
			LogNotice("%" PRIi64 "\n", m_temporaryResults[i+1]);
	}
}
