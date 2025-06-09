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
	@brief Declaration of LevelCrossingDetector
 */

#ifndef LevelCrossingDetector_h
#define LevelCrossingDetector_h

struct __attribute__((packed)) ZeroCrossingPushConstants
{
	int64_t	triggerPhase;
	int64_t timescale;
	uint32_t inputSize;
	uint32_t inputPerThread;
	uint32_t outputPerThread;
	float threshold;
};

struct __attribute__((packed)) PreGatherPushConstants
{
	uint32_t numBlocks;
	uint32_t stride;
};

struct __attribute__((packed)) GatherPushConstants
{
	uint32_t numBlocks;
	uint32_t stride;
};

/**
	@brief Helper for GPU accelerated level-crossing searches
 */
class LevelCrossingDetector
{
public:
	LevelCrossingDetector();

	void FindZeroCrossings(
		UniformAnalogWaveform* wfm,
		float threshold,
		std::vector<int64_t>& edges,
		vk::raii::CommandBuffer& cmdBuf,
		std::shared_ptr<QueueHandle> queue);

protected:
	std::unique_ptr<ComputePipeline> m_zeroCrossingPipeline;
	std::unique_ptr<ComputePipeline> m_preGatherPipeline;
	std::unique_ptr<ComputePipeline> m_gatherPipeline;

	AcceleratorBuffer<int64_t> m_temporaryResults;
	AcceleratorBuffer<int64_t> m_gatherIndexes;
	AcceleratorBuffer<int64_t> m_outbuf;
};

#endif
