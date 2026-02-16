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
	wfm->PrepareForCpuAccess();
	wfm->m_timescale = sampleperiod;

	int64_t timeWindow = depth * sampleperiod;

	const int64_t bitPeriodFs = FS_PER_SECOND / baudrate;
	const int64_t bitPeriod = bitPeriodFs/sampleperiod;

	int64_t numBits = timeWindow / bitPeriodFs;

	wfm->Resize(numBits+1);

	int64_t currentTime = 0;

	int64_t bitCount = 0;

	auto push = [&bitCount,bitPeriod,numBits,depth,&currentTime] (SparseDigitalWaveform* w, bool v) -> bool
	{
		w->m_offsets.push_back(currentTime);
		int64_t duration = currentTime + bitPeriod < (int64_t)depth ? bitPeriod : depth - currentTime;
		w->m_durations.push_back(duration);
		w->m_samples.push_back(v);
		currentTime += duration;
		bitCount++;
		return bitCount > numBits+1;
	};

	// Idle initial
	push(wfm, true);

	string msg = "Hello World from ngscopeclient UART !\n";

	while(bitCount < numBits)
	{
		for(uint8_t c : msg)
		{
			// Start bit
			if(push(wfm, false)) break;

			// Data bits (LSB first)
			for(int i = 0; i < 8; i++)
			{
				bool bit = (c >> i) & 1;
				if(push(wfm, bit)) break;
			}

			// Stop bit
			if(push(wfm, true)) break;
		}
	}

	// Idle final
	wfm->m_offsets.push_back(currentTime);
	wfm->m_durations.push_back(1);
	wfm->m_samples.push_back(true);

	wfm->MarkSamplesModifiedFromCpu();
	wfm->MarkTimestampsModifiedFromCpu();
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
	wfm->PrepareForCpuAccess();
	wfm->m_timescale = sampleperiod;

	int64_t timeWindow = depth * sampleperiod;

	const int64_t bitPeriodFs = (FS_PER_SECOND / baudrate)/2;
	const int64_t bitPeriod = bitPeriodFs/sampleperiod;

	int64_t numBits = timeWindow / bitPeriodFs;

	wfm->Resize(numBits+1);

	int64_t currentTime = 0;

	int64_t bitCount = 0;

	auto push = [&bitCount,bitPeriod,depth,&currentTime](SparseDigitalWaveform* w, bool v)
	{
		w->m_offsets.push_back(currentTime);
		int64_t duration = currentTime + bitPeriod < (int64_t)depth ? bitPeriod : depth - currentTime;
		w->m_durations.push_back(duration);
		w->m_samples.push_back(v);
		currentTime+=duration;
		bitCount++;
	};

	while(bitCount < numBits)
	{
		push(wfm, false);
		push(wfm, true);
	}

	// Idle final
	wfm->m_offsets.push_back(depth);
	wfm->m_durations.push_back(1);
	wfm->m_samples.push_back(true);

	wfm->MarkSamplesModifiedFromCpu();
	wfm->MarkTimestampsModifiedFromCpu();
}

void TestDigitalWaveformSource::GenerateSPI(SparseDigitalWaveform* cs, SparseDigitalWaveform* sclk,	SparseDigitalWaveform* mosi, int64_t sampleperiod, size_t depth)
{
	cs->PrepareForCpuAccess();
	cs->m_timescale = sampleperiod;
	sclk->PrepareForCpuAccess();
	sclk->m_timescale = sampleperiod;
	mosi->PrepareForCpuAccess();
	mosi->m_timescale = sampleperiod;

	string msg = "Hello ngscopeclient from SPI !\n";

	int64_t t = 0;
	int64_t numBits = msg.size()*8 + 4;
	const int64_t bitPeriod = depth/numBits;
	const int64_t half = bitPeriod / 2;

	cs->Resize(numBits+1);
	sclk->Resize(numBits*2+1);
	mosi->Resize(numBits+1);

	auto push = [](SparseDigitalWaveform* w, int64_t time, int64_t duration, bool v)
	{
		w->m_offsets.push_back(time);
		w->m_durations.push_back(duration);
		w->m_samples.push_back(v);
	};

	// Idle state
	push(cs,   t, 3*bitPeriod, true);
	push(sclk, t, 3*bitPeriod, false);
	push(mosi, t, 3*bitPeriod, false);

	t += (3*bitPeriod);

	// Assert CS
	push(cs, t, bitPeriod*msg.size(), false);

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
	push(cs, t, bitPeriod, true);
	push(sclk, t, bitPeriod, false);
	push(mosi, t, bitPeriod, false);
	t += bitPeriod;
	// Finale Sample
	push(cs, t, 1, true);
	push(sclk, t, 1, false);
	push(mosi, t, 1, false);

	cs->MarkSamplesModifiedFromCpu();
	cs->MarkTimestampsModifiedFromCpu();
	sclk->MarkSamplesModifiedFromCpu();
	sclk->MarkTimestampsModifiedFromCpu();
	mosi->MarkSamplesModifiedFromCpu();
	mosi->MarkTimestampsModifiedFromCpu();
}

void TestDigitalWaveformSource::GenerateParallel(std::vector<SparseDigitalWaveform*> &waveforms, int64_t sampleperiod, size_t depth)
{
	if(waveforms.size() != 9)
	{
		LogError("Invalid waveforms size: exected 9, found %zu.\n",waveforms.size());
		return;
	}
	string msg = "\x01\x02\x04\x08\x10\x20\x40\x80\xFFHello ngscopeclient from UART !";

	int64_t t = 0;
	int64_t numBits = msg.size();
	const int64_t bitPeriod = depth/numBits;
	const int64_t half = bitPeriod / 2;

	// Clock WF
	auto wfClk = waveforms[0];
	wfClk->PrepareForCpuAccess();
	wfClk->m_timescale = sampleperiod;
	wfClk->Resize(2*numBits+1);
	// Parallel lines waveforms
	for(int i = 0 ; i < 8 ; i++)
	{
		auto wf = waveforms[i+1];
		wf->PrepareForCpuAccess();
		wf->m_timescale = sampleperiod;
		wf->Resize(numBits+1);
	}

	auto push = [](SparseDigitalWaveform* w, int64_t time, int64_t duration, bool v)
	{
		w->m_offsets.push_back(time);
		w->m_durations.push_back(duration);
		w->m_samples.push_back(v);
	};

	for(uint8_t c : msg)
	{
		for(int i = 0; i < 8; i++)
		{
			bool bit = (c >> i) & 1;
			// Push data lines
			push(waveforms[i+1], t, bitPeriod, bit);
		}
		push(wfClk, t, half, false);

		t += half;

		// Clock high (sampling edge)
		push(wfClk, t, half, true);

		t += half;
	}

	// Final sample
	for(int i = 0; i < 8; i++)
	{
		push(waveforms[i+1], t, 1, false);
	}
	push(wfClk, t, 1, false);

	for(int i = 0 ; i < 9 ; i++)
	{
		auto wf = waveforms[i];
		wf->MarkSamplesModifiedFromCpu();
		wf->MarkTimestampsModifiedFromCpu();
	}
}
