/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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
	@author Frederic BORRY
	@brief Implementation of TestDigitalWaveformSource

	@ingroup core
 */
#include "scopehal.h"
#include "TestDigitalWaveformSource.h"
#include <complex>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initializes a TestDigitalWaveformSource
 */
TestDigitalWaveformSource::TestDigitalWaveformSource()
{
}

TestDigitalWaveformSource::~TestDigitalWaveformSource()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Signal generation

/**
	@brief Generates a UART waveform

	@param wfm				Waveform to fill
	@param sampleperiod		Interval between samples, in femtoseconds
	@param depth			Total number of samples to generate
	@param baudrate			The baudrate of the UART link
 */
void TestDigitalWaveformSource::GenerateUART(
	SparseDigitalWaveform* wfm,
	int64_t sampleperiod,
	size_t depth,
	int64_t baudrate)
{
	wfm->m_triggerPhase = 0;
	wfm->m_timescale = sampleperiod;

	int64_t timeWindow = depth * sampleperiod;

	const int64_t bitPeriodFs = FS_PER_SECOND / baudrate;
	const int64_t bitPeriod = bitPeriodFs/sampleperiod;

	int64_t numBits = timeWindow / bitPeriodFs;

	wfm->Resize(numBits);

	int64_t currentTime = 0;

	int64_t bitCount = 0;

	auto push = [&bitCount,bitPeriod,numBits,depth] (SparseDigitalWaveform* w, int64_t time, bool v) -> bool
	{
		w->m_offsets.push_back(time);
		w->m_durations.push_back(time + bitPeriod < (int64_t)depth ? bitPeriod : depth - time);
		w->m_samples.push_back(v);
		bitCount++;
		return bitCount > numBits+1;
	};

	// Idle initial
	push(wfm, currentTime, true);
	currentTime += bitPeriod;

	string msg = "Hello World from ngscopeclient UART !";

	while(bitCount < numBits)
	{
		for(uint8_t c : msg)
		{
			// Start bit
			if(push(wfm, currentTime, false)) return;
			currentTime += bitPeriod;

			// Data bits (LSB first)
			for(int i = 0; i < 8; i++)
			{
				bool bit = (c >> i) & 1;
				if(push(wfm, currentTime, bit)) return;
				currentTime += bitPeriod;
			}

			// Stop bit
			if(push(wfm, currentTime, true)) return;
			currentTime += bitPeriod;
		}
	}

	// Idle final
	currentTime += bitPeriod;
	push(wfm, currentTime, true);
}

/**
	@brief Generates a UART waveform using UniformDigitalWaveform

	@param wfm				Waveform to fill
	@param sampleperiod		Interval between samples, in femtoseconds
	@param depth			Total number of samples to generate
	@param baudrate			The baudrate of the UART link
 */
void TestDigitalWaveformSource::GenerateUART(
	UniformDigitalWaveform* wfm,
	int64_t sampleperiod,
	size_t depth,
	int64_t baudrate)
{
	wfm->m_triggerPhase = 0;
	wfm->m_timescale = sampleperiod;

	int64_t timeWindow = depth * sampleperiod;

	const int64_t bitPeriodFs = FS_PER_SECOND / baudrate;

	int64_t numBits = timeWindow / bitPeriodFs;

	wfm->Resize(numBits);

	string msg = "Hello World from ngscopeclient UART uniform waveform !";

	const int64_t samplesPerBit = bitPeriodFs / sampleperiod;

	// Reqired bits
	const size_t totalSamples = depth;

	wfm->Resize(totalSamples);

	size_t sample = 0;

	auto emitBit = [&](bool level)
	{
		for(int64_t i = 0; (i < samplesPerBit && sample < totalSamples); i++)
			wfm->m_samples[sample++] = level;
	};

	// Idle initial
	emitBit(true);

	while(sample < totalSamples)
	{
		for(uint8_t c : msg)
		{
			// Start bit
			emitBit(false);

			// Data bits (LSB first)
			for(int i = 0; i < 8; i++)
				emitBit((c >> i) & 1);

			// Stop bit
			emitBit(true);
		}
	}

	// Idle final
	emitBit(true);
}

/**
	@brief Generates a UART clock waveform

	@param wfm				Waveform to fill
	@param sampleperiod		Interval between samples, in femtoseconds
	@param depth			Total number of samples to generate
 	@param baudrate			The baudrate of the UART link
*/
void TestDigitalWaveformSource::GenerateUARTClock(
	SparseDigitalWaveform* wfm,
	int64_t sampleperiod,
	size_t depth,
	int64_t baudrate)
{
	wfm->m_triggerPhase = 0;
	wfm->m_timescale = sampleperiod;

	int64_t timeWindow = depth * sampleperiod;

	const int64_t bitPeriodFs = (FS_PER_SECOND / baudrate)/2;
	const int64_t bitPeriod = bitPeriodFs/sampleperiod;

	int64_t numBits = timeWindow / bitPeriodFs;

	wfm->Resize(numBits);

	int64_t t = 0;

	int64_t bitCount = 0;

	auto push = [&bitCount,bitPeriod,depth](SparseDigitalWaveform* w, int64_t time, bool v)
	{
		w->m_offsets.push_back(time);
		w->m_durations.push_back(time + bitPeriod < (int64_t)depth ? bitPeriod : 0);
		w->m_samples.push_back(v);
		bitCount++;
	};

	while(bitCount < numBits)
	{
			push(wfm, t, false);
			t += bitPeriod;
			push(wfm, t, true);
			t += bitPeriod;
	}

	// Idle final
	t += bitPeriod;
	push(wfm, t, true);
}

void TestDigitalWaveformSource::GenerateSPI(SparseDigitalWaveform* cs, SparseDigitalWaveform* sclk,	SparseDigitalWaveform* mosi, int64_t sampleperiod, size_t depth)
{
	cs->m_triggerPhase = 0;
	cs->m_timescale = sampleperiod;
	sclk->m_triggerPhase = 0;
	sclk->m_timescale = sampleperiod;
	mosi->m_triggerPhase = 0;
	mosi->m_timescale = sampleperiod;

	string msg = "Hello ngscopeclient from SPI !";

	int64_t t = 0;
	int64_t numBits = msg.size()*8 + 2;
	const int64_t bitPeriod = depth/numBits;
	const int64_t half = bitPeriod / 2;

	cs->Resize(numBits);
	sclk->Resize(numBits);
	mosi->Resize(numBits);

	auto push = [](SparseDigitalWaveform* w, int64_t time, int64_t duration, bool v)
	{
		w->m_offsets.push_back(time);
		w->m_durations.push_back(duration);
		w->m_samples.push_back(v);
	};

	// Idle state
	push(cs,   t, bitPeriod, true);
	push(sclk, t, bitPeriod, false);
	push(mosi, t, bitPeriod, false);

	t += bitPeriod;

	// Assert CS
	push(cs, t, bitPeriod, false);

	for(uint8_t c : msg)
	{
		for(int i = 7; i >= 0; i--)
		{
			bool bit = (c >> i) & 1;

			// Data setup
			push(mosi, t, bitPeriod, bit);
			push(sclk, t, half, false);

			t += half;

			// Clock high (sampling edge)
			push(sclk, t, half, true);

			t += half;
		}
	}

	// Deassert CS
	t += bitPeriod;
	push(cs, t, bitPeriod, true);

}
