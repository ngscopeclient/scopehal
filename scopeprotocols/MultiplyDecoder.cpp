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
#include "MultiplyDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MultiplyDecoder::MultiplyDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	m_signalNames.push_back("a");
	m_channels.push_back(NULL);

	m_signalNames.push_back("b");
	m_channels.push_back(NULL);

	m_range = 1;
	m_offset = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MultiplyDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i < 2) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double MultiplyDecoder::GetVoltageRange()
{
	return m_range;
}

double MultiplyDecoder::GetOffset()
{
	return -m_offset;
}

string MultiplyDecoder::GetProtocolName()
{
	return "Multiply";
}

bool MultiplyDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool MultiplyDecoder::NeedsConfig()
{
	return true;
}

void MultiplyDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Multiply(%s, %s)",
		m_channels[0]->m_displayname.c_str(),
		m_channels[1]->m_displayname.c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MultiplyDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	auto a = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	auto b = dynamic_cast<AnalogWaveform*>(m_channels[1]->GetData());
	if(!a || !b)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	auto len = min(a->m_samples.size(), b->m_samples.size());
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Multiply the units
	m_yAxisUnit = m_channels[0]->GetYAxisUnits() * m_channels[1]->GetYAxisUnits();

	//Set up the output waveform
	auto cap = new AnalogWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(a);

	float* fa = (float*)__builtin_assume_aligned(&a->m_samples[0], 16);
	float* fb = (float*)__builtin_assume_aligned(&b->m_samples[0], 16);
	float* fdst = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);
	for(size_t i=0; i<len; i++)
		fdst[i] = fa[i] * fb[i];

	//Calculate range of the output waveform
	float x = GetMaxVoltage(cap);
	float n = GetMinVoltage(cap);
	m_range = x - n;
	m_offset = (x+n)/2;

	//Copy our time scales from the input
	cap->m_timescale 		= a->m_timescale;
	cap->m_startTimestamp 	= a->m_startTimestamp;
	cap->m_startPicoseconds = a->m_startPicoseconds;

	SetData(cap);
}
