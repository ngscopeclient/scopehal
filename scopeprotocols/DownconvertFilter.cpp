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

#include "../scopehal/scopehal.h"
#include "DownconvertFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DownconvertFilter::DownconvertFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	ClearStreams();
	CreateInput("RF");
	AddStream("I");
	AddStream("Q");

	m_freqname = "LO Frequency";
	m_parameters[m_freqname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqname].SetFloatVal(1e9);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DownconvertFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double DownconvertFilter::GetVoltageRange()
{
	return m_inputs[0].m_channel->GetVoltageRange();
}

string DownconvertFilter::GetProtocolName()
{
	return "Downconvert";
}

bool DownconvertFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DownconvertFilter::NeedsConfig()
{
	return true;
}

void DownconvertFilter::SetDefaultName()
{
	char hwname[256];
	Unit hz(Unit::UNIT_HZ);
	snprintf(hwname, sizeof(hwname), "Downconvert(%s, %s)",
		GetInputDisplayName(0).c_str(),
		m_parameters[m_freqname].ToString().c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DownconvertFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		SetData(NULL, 1);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();

	//Calculate phase velocity
	double lo_freq = m_parameters[m_freqname].GetFloatVal();
	double sample_freq = 1e12f / din->m_timescale;
	double lo_cycles_per_sample = lo_freq / sample_freq;
	double lo_rad_per_sample = lo_cycles_per_sample * 2 * M_PI;

	//Do the actual mixing
	auto cap_i = new AnalogWaveform;
	auto cap_q = new AnalogWaveform;
	cap_i->Resize(len);
	cap_q->Resize(len);
	for(size_t i=0; i<len; i++)
	{
		//Copy timestamp
		int64_t timestamp		= din->m_offsets[i];
		int64_t duration		= din->m_durations[i];
		cap_i->m_offsets[i]		= timestamp;
		cap_q->m_offsets[i]		= timestamp;
		cap_i->m_durations[i]	= duration;
		cap_q->m_durations[i]	= duration;

		//Generate the LO and mix it in
		float phase = lo_rad_per_sample * timestamp;
		float samp = din->m_samples[i];
		cap_i->m_samples[i] 	= samp * sin(phase);
		cap_q->m_samples[i] 	= samp * cos(phase);
	}
	SetData(cap_i, 0);
	SetData(cap_q, 1);

	//Copy our time scales from the input
	cap_i->m_timescale 			= din->m_timescale;
	cap_q->m_timescale 			= din->m_timescale;
	cap_i->m_startTimestamp 	= din->m_startTimestamp;
	cap_q->m_startTimestamp 	= din->m_startTimestamp;
	cap_i->m_startPicoseconds	= din->m_startPicoseconds;
	cap_q->m_startPicoseconds	= din->m_startPicoseconds;
}
