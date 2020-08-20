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
#include "LOMixDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LOMixDecoder::LOMixDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	m_signalNames.push_back("RF");
	m_channels.push_back(NULL);

	m_freqname = "LO Frequency";
	m_parameters[m_freqname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_freqname].SetFloatVal(1e9);

	m_phasename = "LO Phase (deg)";
	m_parameters[m_phasename] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_phasename].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool LOMixDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double LOMixDecoder::GetVoltageRange()
{
	return m_channels[0]->GetVoltageRange();
}

string LOMixDecoder::GetProtocolName()
{
	return "LO Mix";
}

bool LOMixDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool LOMixDecoder::NeedsConfig()
{
	return true;
}

void LOMixDecoder::SetDefaultName()
{
	char hwname[256];
	float freq = m_parameters[m_freqname].GetFloatVal();
	float phase = m_parameters[m_phasename].GetFloatVal();
	Unit hz(Unit::UNIT_HZ);
	snprintf(hwname, sizeof(hwname), "LOMix(%s, %s, %.0f)",
		m_channels[0]->m_displayname.c_str(),
		hz.PrettyPrint(freq).c_str(),
		phase);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void LOMixDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = din->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Convert phase to radians
	double lo_phase_deg = m_parameters[m_phasename].GetFloatVal();
	double lo_phase_rad = lo_phase_deg * M_PI / 180;

	//Calculate phase velocity
	double lo_freq = m_parameters[m_freqname].GetFloatVal();
	double sample_freq = 1e12f / din->m_timescale;
	double lo_cycles_per_sample = lo_freq / sample_freq;
	double lo_rad_per_sample = lo_cycles_per_sample * 2 * M_PI;

	//Do the actual mixing
	auto cap = new AnalogWaveform;
	cap->Resize(len);
	for(size_t i=0; i<len; i++)
	{
		//Copy timestamp
		int64_t timestamp	= din->m_offsets[i];
		cap->m_offsets[i]	= timestamp;
		cap->m_durations[i]	= din->m_durations[i];

		//Generate the LO and mix it in
		float lo = sin(lo_rad_per_sample * timestamp + lo_phase_rad);
		cap->m_samples[i] 	= din->m_samples[i] * lo;
	}
	SetData(cap);

	//Copy our time scales from the input
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
