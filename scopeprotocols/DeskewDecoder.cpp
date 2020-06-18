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
#include "DeskewDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DeskewDecoder::DeskewDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_skewname = "Skew";
	m_parameters[m_skewname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_skewname].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DeskewDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double DeskewDecoder::GetVoltageRange()
{
	return m_channels[0]->GetVoltageRange();
}

double DeskewDecoder::GetOffset()
{
	return m_channels[0]->GetOffset();
}

string DeskewDecoder::GetProtocolName()
{
	return "Deskew";
}

bool DeskewDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DeskewDecoder::NeedsConfig()
{
	//we need the offset to be specified, duh
	return true;
}

void DeskewDecoder::SetDefaultName()
{
	char hwname[256];
	float offset = m_parameters[m_skewname].GetFloatVal() * 1e12f;
	if(offset >= 0)
	{
		snprintf(
			hwname,
			sizeof(hwname),
			"%s + %s",
			m_channels[0]->m_displayname.c_str(), m_xAxisUnit.PrettyPrint(offset).c_str()
			);
	}
	else
	{
		snprintf(
			hwname,
			sizeof(hwname),
			"%s %s",
			m_channels[0]->m_displayname.c_str(), m_xAxisUnit.PrettyPrint(offset).c_str()
			);
	}

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DeskewDecoder::Refresh()
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

	//offset in seconds
	float offset = m_parameters[m_skewname].GetFloatVal();

	//convert to time ticks
	int64_t toff = round(offset * 1e12f / din->m_timescale);

	//Shift all of our samples
	auto cap = new AnalogWaveform;
	cap->Resize(len);
	float* out = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);
	float* a = (float*)__builtin_assume_aligned(&din->m_samples[0], 16);
	int64_t* tout = (int64_t*)__builtin_assume_aligned(&cap->m_offsets[0], 16);
	int64_t* ta = (int64_t*)__builtin_assume_aligned(&din->m_offsets[0], 16);
	memcpy((void*)&cap->m_durations[0], (void*)&din->m_durations[0], len * sizeof(int64_t));
	for(size_t i=0; i<len; i++)
	{
		out[i] 		= a[i];
		tout[i]		= ta[i] + toff;
	}
	SetData(cap);

	//Copy our time scales from the input
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
