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
	@brief Declaration of TestWaveformSource
	@ingroup core
 */

#ifndef TestWaveformSource_h
#define TestWaveformSource_h

#include "VulkanFFTPlan.h"
#include <random>

/**
	@brief Helper class for generating test waveforms

	Used by DemoOscilloscope as well as various unit tests

	@ingroup core
 */
class TestWaveformSource
{
public:
	TestWaveformSource(std::minstd_rand& rng);
	virtual ~TestWaveformSource();

	TestWaveformSource(const TestWaveformSource&) =delete;
	TestWaveformSource& operator=(const TestWaveformSource&) =delete;

	WaveformBase* GenerateNoisySinewave(
		float amplitude,
		float startphase,
		float period,
		int64_t sampleperiod,
		size_t depth,
		float noise_stdev = 0.01);

	WaveformBase* GenerateNoisySinewaveMix(
		float amplitude,
		float startphase1,
		float startphase2,
		float period1,
		float period2,
		int64_t sampleperiod,
		size_t depth,
		float noise_stdev = 0.01);

	WaveformBase* GeneratePRBS31(
		vk::raii::CommandBuffer& cmdBuf,
		std::shared_ptr<QueueHandle> queue,
		float amplitude,
		float period,
		int64_t sampleperiod,
		size_t depth,
		bool lpf = true,
		float noise_stdev = 0.01);

	WaveformBase* Generate8b10b(
		vk::raii::CommandBuffer& cmdBuf,
		std::shared_ptr<QueueHandle> queue,
		float amplitude,
		float period,
		int64_t sampleperiod,
		size_t depth,
		bool lpf = true,
		float noise_stdev = 0.01);

	WaveformBase* GenerateStep(
		float vlo,
		float vhi,
		int64_t sampleperiod,
		size_t depth);

	void DegradeSerialData(
		UniformAnalogWaveform* cap,
		int64_t sampleperiod,
		size_t depth,
		bool lpf,
		float noise_stdev,
		vk::raii::CommandBuffer& cmdBuf,
		std::shared_ptr<QueueHandle> queue);

protected:

	///@brief Random number generator
	std::minstd_rand& m_rng;

	///@brief Input buffer for FFTs
	AcceleratorBuffer<float> m_forwardInBuf;

	///@brief Output buffer for FFTs
	AcceleratorBuffer<float> m_forwardOutBuf;

	///@brief Output buffer for IFFTs
	AcceleratorBuffer<float> m_reverseOutBuf;

	///@brief FFT plan
	std::unique_ptr<VulkanFFTPlan> m_vkForwardPlan;

	///@brief Inverse FFT plan
	std::unique_ptr<VulkanFFTPlan> m_vkReversePlan;

	///@brief FFT bin size, in Hz
	double m_cachedBinSize;

	///@brief Serial channel S-parameter real part
	AcceleratorBuffer<float> m_resampledSparamSines;

	///@brief Serial channel S-parameter imaginary part
	AcceleratorBuffer<float> m_resampledSparamCosines;

	///@brief Compute pipeline for our window function
	ComputePipeline m_rectangularComputePipeline;

	///@brief Compute pipeline for channel emulation
	ComputePipeline m_channelEmulationComputePipeline;

	///@brief S-parameters of the channel
	SParameters m_sparams;

	///@brief FFT point count as of last cache update
	size_t m_cachedNumPoints;

	///@brief Input size of FFT as of last cache update
	size_t m_cachedRawSize;

	void InterpolateSparameters(float bin_hz, size_t nouts);
};

#endif
