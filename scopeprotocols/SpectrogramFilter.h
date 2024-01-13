/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of SpectrogramFilter
 */
#ifndef SpectrogramFilter_h
#define SpectrogramFilter_h

#include "VulkanFFTPlan.h"

#include "../scopehal/DensityFunctionWaveform.h"

struct SpectrogramPostprocessArgs
{
	uint32_t nblocks;
	uint32_t nouts;
	uint32_t ygrid;
	float logscale;
	float impscale;
	float minscale;
	float irange;
};

class SpectrogramWaveform : public DensityFunctionWaveform
{
public:
	SpectrogramWaveform(size_t width, size_t height, double binsize, double bottomEdgeFrequency);
	virtual ~SpectrogramWaveform();

	//not copyable or assignable
	SpectrogramWaveform(const SpectrogramWaveform&) =delete;
	SpectrogramWaveform& operator=(const SpectrogramWaveform&) =delete;

	double GetBinSize()
	{ return m_binsize; }

	double GetBottomEdgeFrequency()
	{ return m_bottomEdgeFrequency; }

protected:
	double m_binsize;
	double m_bottomEdgeFrequency;
};

class SpectrogramFilter : public Filter
{
public:
	SpectrogramFilter(const std::string& color);
	virtual ~SpectrogramFilter();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;

	static std::string GetProtocolName();
	virtual DataLocation GetInputLocation() override;

	virtual float GetVoltageRange(size_t stream) override;
	virtual float GetOffset(size_t stream) override;
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual void SetVoltageRange(float range, size_t stream) override;
	virtual void SetOffset(float offset, size_t stream) override;

	PROTOCOL_DECODER_INITPROC(SpectrogramFilter)

protected:
	virtual void ReallocateBuffers(size_t fftlen, size_t nblocks);

	AcceleratorBuffer<float> m_rdinbuf;
	AcceleratorBuffer<float> m_rdoutbuf;

	size_t m_cachedFFTLength;
	size_t m_cachedFFTNumBlocks;

	float m_range;
	float m_offset;

	std::string m_windowName;
	std::string m_fftLengthName;
	std::string m_rangeMinName;
	std::string m_rangeMaxName;

	std::unique_ptr<VulkanFFTPlan> m_vkPlan;

	ComputePipeline m_blackmanHarrisComputePipeline;
	ComputePipeline m_rectangularComputePipeline;
	ComputePipeline m_cosineSumComputePipeline;

	ComputePipeline m_postprocessComputePipeline;
};

#endif
