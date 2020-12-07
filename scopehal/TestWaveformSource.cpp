/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
 */
#include "scopehal.h"
#include "TestWaveformSource.h"
#include <complex>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TestWaveformSource::TestWaveformSource(mt19937& rng)
	: m_rng(rng)
{
	m_forwardPlan = NULL;
	m_reversePlan = NULL;

	m_cachedNumPoints = 0;
	m_cachedRawSize = 0;

	m_forwardInBuf = NULL;
	m_forwardOutBuf = NULL;
	m_reverseOutBuf = NULL;
}

TestWaveformSource::~TestWaveformSource()
{
	if(m_forwardPlan)
		ffts_free(m_forwardPlan);
	if(m_reversePlan)
		ffts_free(m_reversePlan);

	m_allocator.deallocate(m_forwardInBuf);
	m_allocator.deallocate(m_forwardOutBuf);
	m_allocator.deallocate(m_reverseOutBuf);

	m_forwardPlan = NULL;
	m_reversePlan = NULL;
	m_forwardInBuf = NULL;
	m_forwardOutBuf = NULL;
	m_reverseOutBuf = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Signal generation

/**
	@brief Generates a sinewave with a bit of extra noise added
 */
WaveformBase* TestWaveformSource::GenerateNoisySinewave(
	float amplitude,
	float startphase,
	float period,
	int64_t sampleperiod,
	size_t depth,
	float noise_amplitude)
{
	auto ret = new AnalogWaveform;
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	normal_distribution<> noise(0, noise_amplitude);

	float samples_per_cycle = period * 1.0 / sampleperiod;
	float radians_per_sample = 2 * M_PI / samples_per_cycle;

	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor
	float scale = amplitude / 2;

	for(size_t i=0; i<depth; i++)
	{
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

		ret->m_samples[i] = scale * sinf(i*radians_per_sample + startphase) + noise(m_rng);
	}

	return ret;
}

/**
	@brief Generates a mix of two sinewaves plus some noise
 */
WaveformBase* TestWaveformSource::GenerateNoisySinewaveMix(
	float amplitude,
	float startphase1,
	float startphase2,
	float period1,
	float period2,
	int64_t sampleperiod,
	size_t depth,
	float noise_amplitude)
{
	auto ret = new AnalogWaveform;
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	normal_distribution<> noise(0, noise_amplitude);

	float radians_per_sample1 = 2 * M_PI * sampleperiod / period1;
	float radians_per_sample2 = 2 * M_PI * sampleperiod / period2;

	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor.
	//Divide by 2 again to avoid clipping the sum of them
	float scale = amplitude / 4;

	for(size_t i=0; i<depth; i++)
	{
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

		ret->m_samples[i] = scale *
			(sinf(i*radians_per_sample1 + startphase1) + sinf(i*radians_per_sample2 + startphase2))
			+ noise(m_rng);
	}

	return ret;
}

WaveformBase* TestWaveformSource::GeneratePRBS31(
	float amplitude,
	float period,
	int64_t sampleperiod,
	size_t depth)
{
	auto ret = new AnalogWaveform;
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	//Generate the PRBS as a square wave. Interpolate zero crossings as needed.
	uint32_t prbs = rand();
	float scale = amplitude / 2;
	float phase_to_next_edge = period;
	bool value = false;
	for(size_t i=0; i<depth; i++)
	{
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

		//Increment phase accumulator
		float last_phase = phase_to_next_edge;
		phase_to_next_edge -= sampleperiod;

		bool last = value;
		if(phase_to_next_edge < 0)
		{
			uint32_t next = ( (prbs >> 31) ^ (prbs >> 28) ) & 1;
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
	}

	DegradeSerialData(ret, sampleperiod, depth);

	return ret;
}

WaveformBase* TestWaveformSource::Generate8b10b(
	float amplitude,
	float period,
	int64_t sampleperiod,
	size_t depth)
{
	auto ret = new AnalogWaveform;
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
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

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
	}

	DegradeSerialData(ret, sampleperiod, depth);

	return ret;
}

/**
	@brief Takes an idealized serial data stream and turns it into something less pretty

	by adding noise and a band-limiting filter
 */
void TestWaveformSource::DegradeSerialData(AnalogWaveform* cap, int64_t sampleperiod, size_t depth)
{
	//RNGs
	normal_distribution<> noise(0, 0.01);

	//Prepare for second pass: reallocate FFT buffer if sample depth changed
	const size_t npoints = next_pow2(depth);
	size_t nouts = npoints/2 + 1;
	if(m_cachedNumPoints != npoints)
	{
		if(m_forwardPlan)
			ffts_free(m_forwardPlan);
		m_forwardPlan = ffts_init_1d_real(npoints, FFTS_FORWARD);

		if(m_reversePlan)
			ffts_free(m_reversePlan);
		m_reversePlan = ffts_init_1d_real(npoints, FFTS_BACKWARD);

		m_forwardInBuf = m_allocator.allocate(npoints);
		m_forwardOutBuf = m_allocator.allocate(2*nouts);
		m_reverseOutBuf = m_allocator.allocate(npoints);

		m_cachedNumPoints = npoints;
	}

	//Copy the input, then fill any extra space with zeroes
	memcpy(m_forwardInBuf, &cap->m_samples[0], depth*sizeof(float));
	for(size_t i=depth; i<npoints; i++)
		m_forwardInBuf[i] = 0;

	//Do the forward FFT
	ffts_execute(m_forwardPlan, &m_forwardInBuf[0], &m_forwardOutBuf[0]);

	//Simple channel response model
	double sample_ghz = 1e6 / sampleperiod;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);
	complex<float> pole(0, -FreqToPhase(5e9));
	float prescale = abs(pole);
	for(size_t i = 0; i<nouts; i++)
	{
		complex<float> s(0, FreqToPhase(bin_hz * i));
		complex<float> h = prescale * complex<float>(1, 0) / (s - pole);

		float binscale = abs(h);
		m_forwardOutBuf[i*2] *= binscale;		//real
		m_forwardOutBuf[i*2 + 1] *= binscale;	//imag
	}

	//Calculate the inverse FFT
	ffts_execute(m_reversePlan, &m_forwardOutBuf[0], &m_reverseOutBuf[0]);

	//Rescale the FFT output and copy to the output, then add noise
	float fftscale = 1.0f / npoints;
	for(size_t i=0; i<depth; i++)
		cap->m_samples[i] = m_reverseOutBuf[i] * fftscale + noise(m_rng);
}
