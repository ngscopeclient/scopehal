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

#include "../scopehal/scopehal.h"
#include "PAM4DemodulatorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PAM4DemodulatorFilter::PAM4DemodulatorFilter(const string& color)
	: Filter(color, CAT_SERIAL)
	, m_lowerThreshName("Lower Threshold")
	, m_midThreshName("Middle Threshold")
	, m_upperThreshName("Upper Threshold")
{
	AddDigitalStream("data");
	AddDigitalStream("clk");
	CreateInput("data");
	CreateInput("clk");

	m_parameters[m_lowerThreshName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_lowerThreshName].SetFloatVal(-0.07);

	m_parameters[m_midThreshName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_midThreshName].SetFloatVal(0.005);

	m_parameters[m_upperThreshName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_upperThreshName].SetFloatVal(0.09);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PAM4DemodulatorFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PAM4DemodulatorFilter::GetProtocolName()
{
	return "PAM4 Demodulator";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PAM4DemodulatorFilter::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Sample the input data
	auto din = GetAnalogInputWaveform(0);
	auto clk = GetDigitalInputWaveform(1);
	AnalogWaveform samples;
	SampleOnAnyEdgesWithInterpolation(din, clk, samples);
	size_t len = samples.m_samples.size();

	//Get the thresholds
	float thresholds[3] =
	{
		m_parameters[m_lowerThreshName].GetFloatVal(),
		m_parameters[m_midThreshName].GetFloatVal(),
		m_parameters[m_upperThreshName].GetFloatVal()
	};

	//Create the captures
	auto dcap = new DigitalWaveform;
	dcap->m_timescale = 1;
	dcap->m_startTimestamp = din->m_startTimestamp;
	dcap->m_startFemtoseconds = din->m_startFemtoseconds;
	dcap->m_triggerPhase = 0;
	dcap->m_densePacked = false;
	SetData(dcap, 0);

	auto ccap = new DigitalWaveform;
	ccap->m_timescale = 1;
	ccap->m_startTimestamp = din->m_startTimestamp;
	ccap->m_startFemtoseconds = din->m_startFemtoseconds;
	ccap->m_triggerPhase = 0;
	ccap->m_densePacked = false;
	SetData(ccap, 1);

	//Decode the input data, one symbol (two output bits) at a time
	dcap->Resize(len*2);
	ccap->Resize(len*2);
	for(size_t i=0; i<len; i++)
	{
		//Duration and offset get split in half
		int64_t dur = samples.m_durations[i];
		int64_t off = samples.m_offsets[i];
		int64_t halfdur = dur / 2;
		int64_t qdur = halfdur / 2;

		//First bit: first half of the symbol
		dcap->m_offsets[i*2] = off;
		dcap->m_durations[i*2] = halfdur;

		ccap->m_offsets[i*2] = off + qdur;
		if(i > 0)
			ccap->m_durations[i*2] = ccap->m_offsets[i*2] - (ccap->m_offsets[i*2 - 1] + ccap->m_durations[i*2 - 1]);
		else
			ccap->m_durations[i*2] = halfdur;

		//Second bit: other half of the symbol
		dcap->m_offsets[i*2 + 1] = off + halfdur;
		dcap->m_durations[i*2 + 1] = dur - halfdur;

		ccap->m_offsets[i*2 + 1] = off + halfdur + qdur;
		ccap->m_durations[i*2 + 1] = halfdur;

		//Fill clock
		ccap->m_samples[i*2] = 0;
		ccap->m_samples[i*2 + 1] = 1;

		//Fill data bits
		//(note gray coding, levels are 00, 01, 11, 10)
		float v = samples.m_samples[i];
		if(v < thresholds[0])
		{
			dcap->m_samples[i*2] = 0;
			dcap->m_samples[i*2 + 1] = 0;
		}
		else if(v < thresholds[1])
		{
			dcap->m_samples[i*2] = 0;
			dcap->m_samples[i*2 + 1] = 1;
		}
		else if(v < thresholds[2])
		{
			dcap->m_samples[i*2] = 1;
			dcap->m_samples[i*2 + 1] = 1;
		}
		else
		{
			dcap->m_samples[i*2] = 1;
			dcap->m_samples[i*2 + 1] = 0;
		}
	}
}
