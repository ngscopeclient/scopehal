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
	@brief Declaration of ClockRecoveryFilter
 */
#ifndef ClockRecoveryFilter_h
#define ClockRecoveryFilter_h

#include "../scopehal/LevelCrossingDetector.h"

class ClockRecoveryConstants
{
public:
	int64_t		initialPeriod;
	int64_t		fnyquist;
	int64_t		tend;
	int64_t		timescale;
	int64_t		triggerPhase;
	uint32_t	nedges;
	uint32_t	maxOffsetsPerThread;
	uint32_t	maxInputSamples;
};

class ClockRecoveryFilter : public Filter
{
public:
	ClockRecoveryFilter(const std::string& color);
	virtual ~ClockRecoveryFilter();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(ClockRecoveryFilter)

	///@brief Allow our zero crossings to be reused in downstream filters (e.g. TIE) if valid (input is uniform)
	AcceleratorBuffer<int64_t>& GetZeroCrossings()
	{ return m_detector.GetResults(); }

	float GetThreshold()
	{ return m_threshold.GetFloatVal(); }

protected:
	void FillSquarewaveGeneric(SparseDigitalWaveform& cap);

	void InnerLoopWithGating(
		SparseDigitalWaveform& cap,
		SparseAnalogWaveform& scap,
		AcceleratorBuffer<int64_t>& edges,
		size_t nedges,
		int64_t tend,
		int64_t initialPeriod,
		int64_t halfPeriod,
		int64_t fnyquist,
		WaveformBase* gate,
		SparseDigitalWaveform* sgate,
		UniformDigitalWaveform* ugate);

	void InnerLoopWithNoGating(
		SparseDigitalWaveform& cap,
		SparseAnalogWaveform& scap,
		AcceleratorBuffer<int64_t>& edges,
		size_t nedges,
		int64_t tend,
		int64_t initialPeriod,
		int64_t halfPeriod,
		int64_t fnyquist);

#ifdef __x86_64__
	void FillSquarewaveAVX2(SparseDigitalWaveform& cap);
#endif

	///@brief Nominal symbol rate, in Hz
	FilterParameter& m_baudRate;

	///@brief Threshold for edge detection
	FilterParameter& m_threshold;

	enum MtModes
	{
		MT_SINGLE_THREAD,
		MT_GPU
	};

	/**
		@brief Enable the GPU version of the filter where possible

		Normally we don't need this switch, but since there can be slight changes to jitter behavior it's probably
		better to have it than not
	 */
	FilterParameter& m_mtMode;

	LevelCrossingDetector m_detector;

	///@brief Compute pipeline for filling output
	std::shared_ptr<ComputePipeline> m_fillSquarewaveAndDurationsComputePipeline;

	///@brief Compute pipeline for first PLL pass
	std::shared_ptr<ComputePipeline> m_firstPassComputePipeline;

	///@brief Compute pipeline for second PLL pass
	std::shared_ptr<ComputePipeline> m_secondPassComputePipeline;

	///@brief Compute pipeline for final reduction pass
	std::shared_ptr<ComputePipeline> m_finalPassComputePipeline;

	///@brief Output timestamp buffer for first PLL pass
	AcceleratorBuffer<int64_t> m_firstPassTimestamps;

	/**
		@brief Output status buffer for first PLL pass

		2 int64s per thread:
			Number of samples written
			Ending period
	 */
	AcceleratorBuffer<int64_t> m_firstPassState;

	///@brief Output timestamp buffer for second PLL pass
	AcceleratorBuffer<int64_t> m_secondPassTimestamps;

	/**
		@brief Output status buffer for second PLL pass

		2 int64s per thread:
			Number of samples written
			Ending period
	 */
	AcceleratorBuffer<int64_t> m_secondPassState;
};

#endif
