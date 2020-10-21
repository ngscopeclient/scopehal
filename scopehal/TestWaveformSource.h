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
	@brief Declaration of TestWaveformSource
 */

#ifndef TestWaveformSource_h
#define TestWaveformSource_h

#include "../scopehal/AlignedAllocator.h"
#include <ffts.h>
#include <random>

/**
	@brief Helper class for generating test waveforms

	Used by DemoOscilloscope as well as various unit tests etc.
 */
class TestWaveformSource
{
public:
	TestWaveformSource(std::mt19937& rng);
	virtual ~TestWaveformSource();

	TestWaveformSource(const TestWaveformSource&) =delete;
	TestWaveformSource& operator=(const TestWaveformSource&) =delete;

	WaveformBase* GenerateNoisySinewave(
		float amplitude,
		float startphase,
		float period,
		int64_t sampleperiod,
		size_t depth,
		float noise_amplitude = 0.01);

	WaveformBase* GenerateNoisySinewaveMix(
		float amplitude,
		float startphase1,
		float startphase2,
		float period1,
		float period2,
		int64_t sampleperiod,
		size_t depth,
		float noise_amplitude = 0.01);

	WaveformBase* GeneratePRBS31(
		float amplitude,
		float period,
		int64_t sampleperiod,
		size_t depth);

	WaveformBase* Generate8b10b(
		float amplitude,
		float period,
		int64_t sampleperiod,
		size_t depth);

	void DegradeSerialData(AnalogWaveform* cap, int64_t sampleperiod, size_t depth);

protected:
	std::mt19937& m_rng;

	//FFT stuff
	AlignedAllocator<float, 32> m_allocator;
	ffts_plan_t* m_forwardPlan;
	ffts_plan_t* m_reversePlan;
	size_t m_cachedNumPoints;
	size_t m_cachedRawSize;

	float* m_forwardInBuf;
	float* m_forwardOutBuf;
	float* m_reverseOutBuf;
};

#endif
