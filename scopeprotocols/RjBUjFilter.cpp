/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_CLOCK)
{
	SetYAxisUnits(Unit(Unit::UNIT_FS), 0);

	//Set up channels
	CreateInput("TIE");
	CreateInput("Threshold");
	CreateInput("Clock");
	CreateInput("DDJ");

	ClearSweeps();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool RjBUjFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i <= 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;
	if( (i == 3) && (dynamic_cast<DDJMeasurement*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void RjBUjFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "RjBUj(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string RjBUjFilter::GetProtocolName()
{
	return "Rj + BUj";
}

bool RjBUjFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool RjBUjFilter::NeedsConfig()
{
	//we have more than one input
	return true;
}

float RjBUjFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

float RjBUjFilter::GetOffset(size_t /*stream*/)
{
	return m_offset;
}

void RjBUjFilter::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
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
	auto tie = GetAnalogInputWaveform(0);
	auto thresh = GetDigitalInputWaveform(1);
	auto clk = GetDigitalInputWaveform(2);
	auto ddj = dynamic_cast<DDJMeasurement*>(GetInput(3).m_channel);
	float* table = ddj->GetDDJTable();

	//Sample the input data
	DigitalWaveform samples;
	SampleOnAnyEdges(thresh, clk, samples);

	//Set up output waveform
	auto cap = SetupOutputWaveform(tie, 0, 0, 0);

	//DDJ history (8 UIs)
	uint8_t window = 0;

	size_t tielen = tie->m_samples.size();
	size_t samplen = samples.m_samples.size();

	size_t itie = 0;

	float vmax = -FLT_MAX;
	float vmin = FLT_MAX;

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

		vmax = max(vmax, uj);
		vmin = min(vmin, uj);
	}

	//Calculate bounds
	m_max = max(m_max, (float)vmax);
	m_min = min(m_min, (float)vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );
}
