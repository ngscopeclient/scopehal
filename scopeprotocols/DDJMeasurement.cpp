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

#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DDJMeasurement::DDJMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	m_yAxisUnit = Unit(Unit::UNIT_FS);

	//Set up channels
	CreateInput("TIE");
	CreateInput("Threshold");
	CreateInput("Clock");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DDJMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i <= 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void DDJMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DDJ(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string DDJMeasurement::GetProtocolName()
{
	return "DDJ";
}

bool DDJMeasurement::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DDJMeasurement::IsScalarOutput()
{
	return true;
}

bool DDJMeasurement::NeedsConfig()
{
	//we have more than one input
	return true;
}

double DDJMeasurement::GetVoltageRange()
{
	return 0;
}

double DDJMeasurement::GetOffset()
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DDJMeasurement::Refresh()
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

	//Sample the input data
	DigitalWaveform samples;
	SampleOnAnyEdges(thresh, clk, samples);

	//Number of UIs of history to keep for ISI
	uint8_t window = 0;

	//Table of jitter indexed by history
	vector<size_t> num_table;
	vector<float> sum_table;
	size_t num_bins = 256;
	num_table.resize(num_bins);
	sum_table.resize(num_bins);
	for(size_t i=0; i<num_bins; i++)
	{
		num_table[i] = 0;
		sum_table[i] = 0;
	}

	size_t tielen = tie->m_samples.size();
	size_t samplen = samples.m_samples.size();

	size_t itie = 0;

	//Loop over the TIE and threshold waveform and assign jitter to bins
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

		//Save the info in the DDJ table
		num_table[window] ++;
		sum_table[window] += tie->m_samples[itie];
	}

	//Calculate DDJ
	float ddjmin =  FLT_MAX;
	float ddjmax = -FLT_MAX;
	for(size_t i=0; i<num_bins; i++)
	{
		if(num_table[i] != 0)
		{
			float jitter = sum_table[i] * 1.0 / num_table[i];
			ddjmin = min(ddjmin, jitter);
			ddjmax = max(ddjmax, jitter);
		}
	}

	auto cap = new AnalogWaveform;
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(1);
	cap->m_samples.push_back(ddjmax - ddjmin);
	cap->m_timescale = 1;
	cap->m_startTimestamp = tie->m_startTimestamp;
	cap->m_startFemtoseconds = tie->m_startFemtoseconds;
	SetData(cap, 0);
}
