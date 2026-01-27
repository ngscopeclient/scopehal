/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of PAMEdgeDetectorFilter
 */
#ifndef PAMEdgeDetectorFilter_h
#define PAMEdgeDetectorFilter_h

#include <cinttypes>
#include "../scopehal/ActionProvider.h"

class PAMEdgeDetectorConstants
{
public:
	uint32_t len;
	uint32_t order;
	uint32_t inputPerThread;
	uint32_t outputPerThread;
};

class PAMEdgeDetectorMergeConstants
{
public:
	int64_t halfui;
	int64_t timescale;
	uint32_t numIndexes;
	uint32_t numSamples;
	uint32_t inputPerThread;
	uint32_t outputPerThread;
	uint32_t order;
};

class PAMEdgeDetectorFilter
	: public Filter
	, public ActionProvider
{
public:
	PAMEdgeDetectorFilter(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual std::vector<std::string> EnumActions() override;
	virtual bool PerformAction(const std::string& id) override;

	PROTOCOL_DECODER_INITPROC(PAMEdgeDetectorFilter)

protected:
	void AutoLevel(UniformAnalogWaveform* din);

	FilterParameter& m_order;
	FilterParameter& m_baud;

	AcceleratorBuffer<uint32_t> m_edgeIndexes;
	AcceleratorBuffer<uint8_t> m_edgeStates;
	AcceleratorBuffer<uint8_t> m_edgeRising;

	AcceleratorBuffer<uint32_t> m_edgeCount;

	AcceleratorBuffer<uint32_t> m_edgeIndexesScratch;
	AcceleratorBuffer<uint8_t> m_edgeStatesScratch;
	AcceleratorBuffer<uint8_t> m_edgeRisingScratch;

	AcceleratorBuffer<int64_t> m_edgeOffsetsScratch;

	AcceleratorBuffer<float> m_thresholds;
	AcceleratorBuffer<float> m_levels;

	///@brief Compute pipeline for first edge detection pass
	std::shared_ptr<ComputePipeline> m_firstPassComputePipeline;

	///@brief Compute pipeline for second (merge) edge detection pass
	std::shared_ptr<ComputePipeline> m_secondPassComputePipeline;

	///@brief Compute pipeline for first-pass merge
	std::shared_ptr<ComputePipeline> m_initialMergeComputePipeline;

	///@brief Compute pipeline for final merge pass
	std::shared_ptr<ComputePipeline> m_finalMergeComputePipeline;
};

#endif
