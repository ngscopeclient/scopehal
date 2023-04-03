/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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

#include "../scopehal/scopehal.h"
#include "DDJMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DDJMeasurement::DDJMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);

	//Set up channels
	CreateInput("TIE");
	CreateInput("Threshold");
	CreateInput("Clock");

	for(int i=0; i<256; i++)
		m_table[i] = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DDJMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) &&
		(stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
		(stream.GetYAxisUnits() == Unit::UNIT_FS)
		)
	{
		return true;
	}
	if( (i <= 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DDJMeasurement::GetProtocolName()
{
	return "DDJ";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DDJMeasurement::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		m_streams[0].m_value = NAN;
		return;
	}

	//Get the input data
	auto tie = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(0));
	tie->PrepareForCpuAccess();

	//Sample the input data
	SparseDigitalWaveform samples;
	SampleOnAnyEdgesBase(GetInputWaveform(1), GetInputWaveform(2), samples);

	//DDJ history (8 UIs)
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
	int64_t tfirst = GetOffsetScaled(tie, 0);
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
			target = GetOffsetScaled(tie, itie);

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
	float ddjmax = 0;
	for(size_t i=0; i<num_bins; i++)
	{
		if(num_table[i] != 0)
		{
			float jitter = sum_table[i] * 1.0 / num_table[i];
			m_table[i] = jitter;
			ddjmin = min(ddjmin, jitter);
			ddjmax = max(ddjmax, jitter);
		}
		else
			m_table[i] = 0;
	}

	m_streams[0].m_value = ddjmax - ddjmin;
}
