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
	@brief Declaration of CouplerDeEmbedFilter
 */
#ifndef CouplerDeEmbedFilter_h
#define CouplerDeEmbedFilter_h

#include "DeEmbedFilter.h"

class CouplerSParameters
{
public:
	CouplerSParameters()
		: m_cachedBinSize(0)
		, m_groupDelayFs(0)
		, m_groupDelaySamples(0)
	{}

	/**
		@brief Check to see if we need to refresh our cache
	 */
	bool NeedUpdate(WaveformBase* wmag, WaveformBase* wang, double bin_hz)
	{
		//Check if bin size changed
		if(fabs(m_cachedBinSize - bin_hz) > FLT_EPSILON)
			return true;

		if( (m_magKey != wmag) || (m_angleKey != wang) )
			return true;
		return false;
	}

	/**
		@brief Refresh the cached data
	 */
	void Refresh(
		WaveformBase* wmag,
		WaveformBase* wang,
		double bin_hz,
		bool invert,
		size_t nouts,
		float maxGain,
		int64_t timescale,
		size_t npoints)
	{
		//Update cache keys to reflect the current waveforms we're processing
		m_magKey = wmag;
		m_angleKey = wang;

		m_resampledSparamCosines.clear();
		m_resampledSparamSines.clear();
		InterpolateSparameters(wmag, wang, bin_hz, invert, nouts, maxGain);

		m_groupDelayFs = GetGroupDelay();
		m_groupDelaySamples = ceil( m_groupDelayFs / timescale );

		//Sanity check: if we have noisy or poor quality S-parameter data, group delay might not make sense.
		//Skip this correction pass in that case.
		if(static_cast<size_t>(llabs(m_groupDelaySamples)) >= npoints)
		{
			m_groupDelayFs = 0;
			m_groupDelaySamples = 0;
		}
	}

	void InterpolateSparameters(
		WaveformBase* wmag,
		WaveformBase* wang,
		float bin_hz,
		bool invert,
		size_t nouts,
		float maxGain);

	virtual int64_t GetGroupDelay();

	AcceleratorBuffer<float> m_resampledSparamSines;
	AcceleratorBuffer<float> m_resampledSparamCosines;

	WaveformCacheKey m_magKey;
	WaveformCacheKey m_angleKey;

	SParameterVector m_cachedSparams;

	double m_cachedBinSize;

	int64_t m_groupDelayFs;
	int64_t m_groupDelaySamples;
};

class CouplerDeEmbedFilter : public Filter
{
public:
	CouplerDeEmbedFilter(const std::string& color);
	virtual ~CouplerDeEmbedFilter();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(CouplerDeEmbedFilter)

protected:

	void ProcessScalarInput(
		vk::raii::CommandBuffer& cmdBuf,
		std::unique_ptr<VulkanFFTPlan>& plan,
		AcceleratorBuffer<float>& samplesIn,
		AcceleratorBuffer<float>& samplesOut,
		size_t npointsPadded,
		size_t npointsUnpadded
		);

	void ApplySParameters(
		vk::raii::CommandBuffer& cmdBuf,
		AcceleratorBuffer<float>& samplesIn,
		AcceleratorBuffer<float>& samplesOut,
		CouplerSParameters& params,
		size_t npoints,
		size_t nouts);

	void ApplySParametersInPlace(
		vk::raii::CommandBuffer& cmdBuf,
		AcceleratorBuffer<float>& samplesInout,
		CouplerSParameters& params,
		size_t npoints,
		size_t nouts);

	void SubtractInPlace(
		vk::raii::CommandBuffer& cmdBuf,
		AcceleratorBuffer<float>& samplesInout,
		AcceleratorBuffer<float>& samplesSub,
		size_t npoints);

	void GenerateScalarOutput(
		vk::raii::CommandBuffer& cmdBuf,
		std::unique_ptr<VulkanFFTPlan>& plan,
		size_t istart,
		size_t iend,
		WaveformBase* refin,
		size_t stream,
		size_t npoints,
		int64_t phaseshift,
		AcceleratorBuffer<float>& samplesIn);

	void GroupDelayCorrection(
		CouplerSParameters& params,
		size_t& istart,
		size_t& iend,
		int64_t& phaseshift,
		bool invert);

	std::string m_maxGainName;

	enum TruncationMode
	{
		TRUNC_AUTO,
		TRUNC_MANUAL
	};

	float m_cachedMaxGain;

	size_t m_cachedNumPoints;

	CouplerSParameters m_forwardCoupledParams;
	CouplerSParameters m_reverseCoupledParams;
	CouplerSParameters m_forwardLeakageParams;
	CouplerSParameters m_reverseLeakageParams;

	AcceleratorBuffer<float> m_scalarTempBuf1;
	AcceleratorBuffer<float> m_vectorTempBuf1;
	AcceleratorBuffer<float> m_vectorTempBuf2;
	AcceleratorBuffer<float> m_vectorTempBuf3;

	ComputePipeline m_rectangularComputePipeline;
	ComputePipeline m_deEmbedComputePipeline;
	ComputePipeline m_deEmbedInPlaceComputePipeline;
	ComputePipeline m_normalizeComputePipeline;
	ComputePipeline m_subtractInPlaceComputePipeline;

	std::unique_ptr<VulkanFFTPlan> m_vkForwardPlan;
	std::unique_ptr<VulkanFFTPlan> m_vkForwardPlan2;

	std::unique_ptr<VulkanFFTPlan> m_vkReversePlan;
	std::unique_ptr<VulkanFFTPlan> m_vkReversePlan2;
	std::unique_ptr<VulkanFFTPlan> m_vkReversePlan3;
};

#endif
