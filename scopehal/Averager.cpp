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
	@brief Implementation of Averager
 */
#include "scopehal.h"
#include "Averager.h"

using namespace std;

Averager::Averager()
{
	m_computePipeline = make_unique<ComputePipeline>(
		"shaders/ReductionSum.spv",
		2,
		sizeof(ReductionSumPushConstants));

	//we need this readable from the CPU to do the final summation
	m_temporaryResults.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_temporaryResults.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

float Averager::Average(
	UniformAnalogWaveform* wfm,
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	//This value experimentally gives the best speedup for an NVIDIA 2080 Ti vs an Intel Xeon Gold 6144
	//Maybe consider dynamic tuning in the future at initialization?
	const uint64_t numThreads = 16384;

	cmdBuf.begin({});

	//Do the reduction summation
	size_t depth = wfm->size();
	ReductionSumPushConstants push;
	push.numSamples = depth;
	push.samplesPerThread = (depth + numThreads) / numThreads;
	m_temporaryResults.resize(numThreads);

	m_computePipeline->BindBufferNonblocking(0, m_temporaryResults, cmdBuf, true);
	m_computePipeline->BindBufferNonblocking(1, wfm->m_samples, cmdBuf);
	m_computePipeline->Dispatch(cmdBuf, push, numThreads, 1);

	m_temporaryResults.MarkModifiedFromGpu();

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	//Do the final summation
	m_temporaryResults.PrepareForCpuAccess();
	float finalSum = 0;
	for(uint64_t i=0; i<numThreads; i++)
		finalSum += m_temporaryResults[i];

	return finalSum / depth;
}
