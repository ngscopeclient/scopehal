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
#include "DifferenceDecoder.h"
#include "FFTDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DifferenceDecoder::DifferenceDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	m_signalNames.push_back("IN+");
	m_signalNames.push_back("IN-");
	m_channels.push_back(NULL);
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DifferenceDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void DifferenceDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "%s - %s", m_channels[0]->m_displayname.c_str(), m_channels[1]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string DifferenceDecoder::GetProtocolName()
{
	return "Subtract";
}

bool DifferenceDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DifferenceDecoder::NeedsConfig()
{
	//we have more than one input
	return true;
}

double DifferenceDecoder::GetVoltageRange()
{
	//TODO: default, but allow overriding
	double v1 = m_channels[0]->GetVoltageRange();
	double v2 = m_channels[1]->GetVoltageRange();
	return max(v1, v2) * 2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DifferenceDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	auto din_p = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	auto din_n = dynamic_cast<AnalogWaveform*>(m_channels[1]->GetData());
	if( (din_p == NULL) || (din_n == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Set up units and complain if they're inconsistent
	m_yAxisUnit = m_channels[0]->GetYAxisUnits();
	if(m_yAxisUnit != m_channels[1]->GetYAxisUnits())
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = din_p->m_samples.size();
	if(din_n->m_samples.size() < len)
		len = din_n->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Create the output
	AnalogWaveform* cap;
	if(dynamic_cast<FFTWaveform*>(din_p) != NULL)
		cap = new FFTWaveform;
	else
		cap = new AnalogWaveform;

	//Subtract all of our samples
	cap->Resize(len);
	memcpy(&cap->m_offsets[0],		&din_p->m_offsets[0],	sizeof(int64_t)*len);
	memcpy(&cap->m_durations[0],	&din_p->m_durations[0], sizeof(int64_t)*len);
	#pragma omp parallel for
	for(size_t i=0; i<len; i++)
		cap->m_samples[i] 		= din_p->m_samples[i] - din_n->m_samples[i];

	SetData(cap);

	//Copy our time scales from the input
	//Use the first trace's timestamp as our start time if they differ
	cap->m_timescale 		= din_p->m_timescale;
	cap->m_startTimestamp 	= din_p->m_startTimestamp;
	cap->m_startPicoseconds = din_p->m_startPicoseconds;
}
