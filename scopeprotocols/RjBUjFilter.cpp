/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RjBUjFilter::RjBUjFilter(const string& color)
	: Filter(color, CAT_CLOCK)
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("TIE");
	CreateInput("Threshold");
	CreateInput("Clock");
	CreateInput("DDJ");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool RjBUjFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i <= 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;
	if( (i == 3) && (dynamic_cast<DDJMeasurement*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string RjBUjFilter::GetProtocolName()
{
	return "Rj + BUj";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void RjBUjFilter::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto tie = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(0));
	auto thresh = GetInputWaveform(1);
	auto clk = GetInputWaveform(2);
	auto ddj = dynamic_cast<DDJMeasurement*>(GetInput(3).m_channel);
	float* table = ddj->GetDDJTable();

	tie->PrepareForCpuAccess();
	thresh->PrepareForCpuAccess();
	clk->PrepareForCpuAccess();

	//Sample the input data
	SparseDigitalWaveform samples;
	SampleOnAnyEdgesBase(thresh, clk, samples);
	samples.PrepareForCpuAccess();

	//Set up output waveform
	auto cap = SetupSparseOutputWaveform(tie, 0, 0, 0);
	cap->PrepareForCpuAccess();

	//DDJ history (8 UIs)
	uint8_t window = 0;

	size_t tielen = tie->size();
	size_t samplen = samples.size();

	size_t itie = 0;

	//Main processing loop
	size_t nbits = 0;
	int64_t tfirst = tie->m_offsets[0] * tie->m_timescale + tie->m_triggerPhase;
	for(size_t idata=0; idata < samplen; idata ++)
	{
		//Sample the next bit in the thresholded waveform
		window = (window >> 1);
		if(samples.m_samples[idata])
			window |= 0x80;
		nbits ++;

		//need 8 in last_window, plus one more for the current bit
		if(nbits < 9)
			continue;

		//If we're still before the first TIE sample, nothing to do
		int64_t tstart = samples.m_offsets[idata];
		if(tstart < tfirst)
			continue;

		//Advance TIE samples if needed
		int64_t target = 0;
		while( (target < tfirst) && (itie < tielen) )
		{
			target = tie->m_offsets[itie] * tie->m_timescale + tie->m_triggerPhase;

			if(target < tstart)
				itie ++;
		}
		if(itie >= tielen)
			break;

		//If the TIE sample is after this bit, don't do anything.
		//We need edges within this UI.
		int64_t tend = tstart + samples.m_durations[idata];
		if(target > tend)
			continue;

		//We've got a good sample. Subtract the averaged DDJ from TIE to get the uncorrelated jitter (Rj + BUj).
		float uj = tie->m_samples[itie] - table[window];
		cap->m_samples[itie] = uj;
	}

	cap->MarkModifiedFromCpu();
}
