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
	@author Andrew D. Zonenberg
	@brief Implementation of TestWaveformSource

	@ingroup core
 */
#include "scopehal.h"
#include "TestWaveformSource.h"
#include <complex>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initializes a TestWaveformSource

	@param rng	Random number generator instance that the source should use.
				The source maintains a persistent reference to the RNG so it must remain valid
				for the entire lifespan of the TestWaveformSource.
 */
TestWaveformSource::TestWaveformSource(minstd_rand& rng)
	: m_rng(rng)
	, m_cachedBinSize(0)
	, m_rectangularComputePipeline("shaders/RectangularWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_channelEmulationComputePipeline("shaders/DeEmbedFilter.spv", 3, sizeof(uint32_t))
	, m_noisySineComputePipeline("shaders/NoisySine.spv", 1, sizeof(NoisySinePushConstants))
	, m_cachedNumPoints(0)
	, m_cachedRawSize(0)
{
	TouchstoneParser sxp;
	sxp.Load(FindDataFile("channels/300mm-s2000m.s2p"), m_sparams);

	m_forwardInBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_forwardInBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_forwardOutBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_forwardOutBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_reverseOutBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_reverseOutBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

TestWaveformSource::~TestWaveformSource()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Signal generation

/**
	@brief Generates a step waveform

	The waveform starts at vlo for half of the total duration, the instantly transitions to vhi and remains at vhi
	for the remainder of the total length.

	@param vlo				Starting voltage
	@param vhi				Ending voltage
	@param sampleperiod		Interval, in femtoseconds, between samples
	@param depth			Number of points in the waveform
 */
WaveformBase* TestWaveformSource::GenerateStep(
	float vlo,
	float vhi,
	int64_t sampleperiod,
	size_t depth)
{
	auto ret = new UniformAnalogWaveform("Step");
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	size_t mid = depth/2;
	for(size_t i=0; i<depth; i++)
	{
		if(i < mid)
			ret->m_samples[i] = vlo;
		else
			ret->m_samples[i] = vhi;
	}

	return ret;
}

/**
	@brief Generates a sinewave with AWGN added

	@param cmdBuf			Vulkan command buffer to use
	@param queue			Vulkan queue to use
	@param amplitude		P-P amplitude of the waveform in volts
	@param startphase		Starting phase in radians
	@param period			Period of the sine, in femtoseconds
	@param sampleperiod		Interval between samples, in femtoseconds
	@param depth			Total number of samples to generate
	@param noise_stdev		Standard deviation of the AWGN in volts
 */
WaveformBase* TestWaveformSource::GenerateNoisySinewave(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue,
	float amplitude,
	float startphase,
	float period,
	int64_t sampleperiod,
	size_t depth,
	float noise_stdev)
{
	auto ret = new UniformAnalogWaveform("NoisySine");
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	//Calculate a bunch of constants
	const int numThreads = 32768;
	NoisySinePushConstants push;
	float samples_per_cycle = period * 1.0 / sampleperiod;
	push.numSamples = depth;
	push.samplesPerThread = (depth + numThreads) / numThreads;
	push.rngSeed = m_rng();
	push.startPhase = startphase;
	push.scale = amplitude / 2;	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor
	push.sigma = noise_stdev;
	push.radiansPerSample = 2 * M_PI / samples_per_cycle;

	//Do the actual waveform generation
	cmdBuf.begin({});
	m_noisySineComputePipeline.BindBufferNonblocking(0, ret->m_samples, cmdBuf, true);
	m_noisySineComputePipeline.Dispatch(cmdBuf, push, numThreads, 1);
	ret->MarkModifiedFromGpu();
	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	return ret;
}

/**
	@brief Generates a sum of two sinewaves with AWGN added

	@param amplitude		P-P amplitude of the waveform in volts
	@param startphase1		Starting phase of the first sine in radians
	@param startphase2		Starting phase of the second sine in radians
	@param period1			Period of the first sine, in femtoseconds
	@param period2			Period of the second sine, in femtoseconds
	@param sampleperiod		Interval between samples, in femtoseconds
	@param depth			Total number of samples to generate
	@param noise_stdev		Standard deviation of the AWGN in volts
	@param downloadCallback	Callback for download progress
 */
WaveformBase* TestWaveformSource::GenerateNoisySinewaveSum(
	float amplitude,
	float startphase1,
	float startphase2,
	float period1,
	float period2,
	int64_t sampleperiod,
	size_t depth,
	float noise_stdev,
	std::function<void(float)> downloadCallback)
{
	auto ret = new UniformAnalogWaveform("NoisySineSum");
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	normal_distribution<> noise(0, noise_stdev);

	float radians_per_sample1 = 2 * M_PI * sampleperiod / period1;
	float radians_per_sample2 = 2 * M_PI * sampleperiod / period2;

	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor.
	//Divide by 2 again to avoid clipping the sum of them
	float scale = amplitude / 4;

	for(size_t i=0; i<depth; i++)
	{
		ret->m_samples[i] = scale *
			(sinf(i*radians_per_sample1 + startphase1) + sinf(i*radians_per_sample2 + startphase2))
			+ noise(m_rng);
		if( (i % 1024 == 0) && downloadCallback)
			downloadCallback((float)i / (float)depth);
	}

	return ret;
}

/**
	@brief Generates a PRBS-31 waveform through a lossy channel with AWGN

	@param cmdBuf			Vulkan command buffer to use for channel emulation
	@param queue			Vulkan queue to use for channel emulation
	@param amplitude		P-P amplitude of the waveform in volts
	@param period			Unit interval, in femtoseconds
	@param sampleperiod		Interval between samples, in femtoseconds
	@param depth			Total number of samples to generate
	@param lpf				Emulate a lossy channel if true, no channel emulation if false
	@param noise_stdev		Standard deviation of the AWGN in volts
	@param downloadCallback	Callback for download progress
 */
WaveformBase* TestWaveformSource::GeneratePRBS31(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue,
	float amplitude,
	float period,
	int64_t sampleperiod,
	size_t depth,
	bool lpf,
	float noise_stdev,
	std::function<void(float)> downloadCallback
	)
{
	auto ret = new UniformAnalogWaveform("PRBS31");
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	//Generate the PRBS as a square wave. Interpolate zero crossings as needed.
	uint32_t prbs = rand();
	float scale = amplitude / 2;
	float phase_to_next_edge = period;
	bool value = false;
	for(size_t i=0; i<depth; i++)
	{
		//Increment phase accumulator
		float last_phase = phase_to_next_edge;
		phase_to_next_edge -= sampleperiod;

		bool last = value;
		if(phase_to_next_edge < 0)
		{
			uint32_t next = ( (prbs >> 30) ^ (prbs >> 27) ) & 1;
			prbs = (prbs << 1) | next;
			value = next;

			phase_to_next_edge += period;
		}

		//Not an edge, just repeat the value
		if(last == value)
			ret->m_samples[i] = value ? scale : -scale;

		//Edge - interpolate
		else
		{
			float last_voltage = last ? scale : -scale;
			float cur_voltage = value ? scale : -scale;

			float frac = 1 - (last_phase / sampleperiod);
			float delta = cur_voltage - last_voltage;

			ret->m_samples[i] = last_voltage + delta*frac;
		}

		if( (i % 1024 == 0) && downloadCallback)
			downloadCallback((float)i / (float)depth);
	}

	DegradeSerialData(ret, sampleperiod, depth, lpf, noise_stdev, cmdBuf, queue);

	return ret;
}

/**
	@brief Generates a K28.5 D16.2 (1000base-X / SGMII idle) waveform through a lossy channel with AWGN

	@param cmdBuf			Vulkan command buffer to use for channel emulation
	@param queue			Vulkan queue to use for channel emulation
	@param amplitude		P-P amplitude of the waveform in volts
	@param period			Unit interval, in femtoseconds
	@param sampleperiod		Interval between samples, in femtoseconds
	@param depth			Total number of samples to generate
	@param lpf				Emulate a lossy channel if true, no channel emulation if false
	@param noise_stdev		Standard deviation of the AWGN in volts
	@param downloadCallback	Callback for download progress
 */
WaveformBase* TestWaveformSource::Generate8b10b(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue,
	float amplitude,
	float period,
	int64_t sampleperiod,
	size_t depth,
	bool lpf,
	float noise_stdev,
	std::function<void(float)> downloadCallback)
{
	auto ret = new UniformAnalogWaveform("8B10B");
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	const int patternlen = 20;
	const bool pattern[patternlen] =
	{
		0, 0, 1, 1, 1, 1, 1, 0, 1, 0,		//K28.5
		1, 0, 0, 1, 0, 0, 0, 1, 0, 1		//D16.2
	};

	//Generate the data stream as a square wave. Interpolate zero crossings as needed.
	float scale = amplitude / 2;
	float phase_to_next_edge = period;
	bool value = false;
	int nbit = 0;
	for(size_t i=0; i<depth; i++)
	{
		//Increment phase accumulator
		float last_phase = phase_to_next_edge;
		phase_to_next_edge -= sampleperiod;

		bool last = value;
		if(phase_to_next_edge < 0)
		{
			value = pattern[nbit ++];
			if(nbit >= patternlen)
				nbit = 0;

			phase_to_next_edge += period;
		}

		//Not an edge, just repeat the value
		if(last == value)
			ret->m_samples[i] = value ? scale : -scale;

		//Edge - interpolate
		else
		{
			float last_voltage = last ? scale : -scale;
			float cur_voltage = value ? scale : -scale;

			float frac = 1 - (last_phase / sampleperiod);
			float delta = cur_voltage - last_voltage;

			ret->m_samples[i] = last_voltage + delta*frac;
		}

		if( (i % 1024 == 0) && downloadCallback)
			downloadCallback((float)i / (float)depth);
	}

	DegradeSerialData(ret, sampleperiod, depth, lpf, noise_stdev, cmdBuf, queue);

	return ret;
}

/**
	@brief Takes an idealized serial data stream and turns it into something less pretty

	The channel is a combination of a lossy S-parameter channel (approximately 300mm of microstrip on Shengyi S1000-2M)
	and AWGN with configurable standard deviation

	@param cap				Waveform to degrade
	@param sampleperiod		Period of the input waveform
	@param depth			Number of points in the input waveform
	@param lpf				True to perform channel emulation, false to only add noise
	@param noise_stdev		Standard deviation of the AWGN, in volts
	@param cmdBuf			Vulkan command buffer to use for channel emulation
	@param queue			Vulkan queue to use for channel emulation
 */
void TestWaveformSource::DegradeSerialData(
	UniformAnalogWaveform* cap,
	int64_t sampleperiod,
	size_t depth,
	bool lpf,
	float noise_stdev,
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	//assume input came from CPU
	cap->MarkModifiedFromCpu();

	//RNGs
	normal_distribution<> noise(0, noise_stdev);

	//Prepare for second pass: reallocate FFT buffer if sample depth changed
	const size_t npoints = next_pow2(depth);
	size_t nouts = npoints/2 + 1;
	bool sizechange = false;
	if(m_cachedNumPoints != npoints)
	{
		m_forwardInBuf.resize(npoints);
		m_forwardOutBuf.resize(2*nouts);
		m_reverseOutBuf.resize(npoints);

		m_cachedNumPoints = npoints;
		sizechange = true;
	}

	//Invalidate old vkFFT plans if size has changed
	if(m_vkForwardPlan)
	{
		if(m_vkForwardPlan->size() != npoints)
			m_vkForwardPlan = nullptr;
	}
	if(m_vkReversePlan)
	{
		if(m_vkReversePlan->size() != npoints)
			m_vkReversePlan = nullptr;
	}

	//Set up new FFT plans
	if(!m_vkForwardPlan)
		m_vkForwardPlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_FORWARD);
	if(!m_vkReversePlan)
		m_vkReversePlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_REVERSE);

	if(lpf)
	{
		double sample_ghz = 1e6 / sampleperiod;
		double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);

		//Resample our parameter to our FFT bin size if needed.
		//Cache trig function output because there's no AVX instructions for this.
		if( (fabs(m_cachedBinSize - bin_hz) > FLT_EPSILON) || sizechange)
		{
			m_resampledSparamCosines.clear();
			m_resampledSparamSines.clear();
			InterpolateSparameters(bin_hz, nouts);
		}

		//Prepare to do all of our compute stuff in one dispatch call to reduce overhead
		cmdBuf.begin({});

		//Copy and zero-pad the input as needed
		WindowFunctionArgs args;
		args.numActualSamples = depth;
		args.npoints = npoints;
		args.scale = 0;
		args.alpha0 = 0;
		args.alpha1 = 0;
		args.offsetIn = 0;
		args.offsetOut = 0;
		m_rectangularComputePipeline.BindBufferNonblocking(0, cap->m_samples, cmdBuf);
		m_rectangularComputePipeline.BindBufferNonblocking(1, m_forwardInBuf, cmdBuf, true);
		m_rectangularComputePipeline.Dispatch(cmdBuf, args, GetComputeBlockCount(npoints, 64));
		m_rectangularComputePipeline.AddComputeMemoryBarrier(cmdBuf);
		m_forwardInBuf.MarkModifiedFromGpu();

		//Do the actual FFT operation
		m_vkForwardPlan->AppendForward(m_forwardInBuf, m_forwardOutBuf, cmdBuf);
		m_forwardOutBuf.MarkModifiedFromGpu();

		//Apply the interpolated S-parameters
		m_channelEmulationComputePipeline.BindBufferNonblocking(0, m_forwardOutBuf, cmdBuf);
		m_channelEmulationComputePipeline.BindBufferNonblocking(1, m_resampledSparamSines, cmdBuf);
		m_channelEmulationComputePipeline.BindBufferNonblocking(2, m_resampledSparamCosines, cmdBuf);
		m_channelEmulationComputePipeline.Dispatch(cmdBuf, (uint32_t)nouts, GetComputeBlockCount(npoints, 64));
		m_channelEmulationComputePipeline.AddComputeMemoryBarrier(cmdBuf);
		m_forwardOutBuf.MarkModifiedFromGpu();

		//Do the actual FFT operation
		m_vkReversePlan->AppendReverse(m_forwardOutBuf, m_reverseOutBuf, cmdBuf);
		m_reverseOutBuf.MarkModifiedFromGpu();

		//Done, block until the compute operations finish
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//Next step on the CPU
		m_reverseOutBuf.PrepareForCpuAccess();

		//Calculate the group delay of the channel at the middle frequency bin
		auto& s21 = m_sparams[SPair(2, 1)];
		int64_t groupDelay = s21.GetGroupDelay(s21.size() / 2) * FS_PER_SECOND;
		int64_t groupDelaySamples = groupDelay / cap->m_timescale;

		//Calculate the actual start and end of the samples, accounting for garbage at the beginning of the channel
		size_t istart = groupDelaySamples;
		size_t iend = depth;
		size_t finalLen = iend - istart;

		//Rescale the FFT output and copy to the output, then add noise
		float fftscale = 1.0f / npoints;
		for(size_t i=0; i<finalLen; i++)
			cap->m_samples[i] = m_reverseOutBuf[i + istart] * fftscale + (float)noise(m_rng);

		//Resize the waveform to truncate garbage at the end
		cap->Resize(finalLen);
	}

	else
	{
		for(size_t i=0; i<depth; i++)
			cap->m_samples[i] += noise(m_rng);
	}
}

/**
	@brief Recalculate the cached S-parameters used for channel emulation

	@param bin_hz	Size of each FFT bin, in Hz
	@param nouts	Number of FFT bins
 */
void TestWaveformSource::InterpolateSparameters(float bin_hz, size_t nouts)
{
	m_cachedBinSize = bin_hz;

	auto& s21 = m_sparams[SPair(2, 1)];

	m_resampledSparamSines.resize(nouts);
	m_resampledSparamCosines.resize(nouts);

	for(size_t i=0; i<nouts; i++)
	{
		float freq = bin_hz * i;
		auto pt = s21.InterpolatePoint(freq);
		float mag = pt.m_amplitude;
		float ang = pt.m_phase;

		m_resampledSparamSines[i] = sin(ang) * mag;
		m_resampledSparamCosines[i] = cos(ang) * mag;
	}

	m_resampledSparamSines.MarkModifiedFromCpu();
	m_resampledSparamCosines.MarkModifiedFromCpu();
}
