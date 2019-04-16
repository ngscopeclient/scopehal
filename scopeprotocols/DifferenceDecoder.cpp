
/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
#include "../scopehal/AnalogRenderer.h"

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

ChannelRenderer* DifferenceDecoder::CreateRenderer()
{
	return new AnalogRenderer(this);
}

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
	//TODO: default, but allow overridnig
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
	AnalogCapture* din_p = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	AnalogCapture* din_n = dynamic_cast<AnalogCapture*>(m_channels[1]->GetData());
	if( (din_p == NULL) || (din_n == NULL) )
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	if(din_p->GetDepth() == 0)
	{
		SetData(NULL);
		return;
	}

	//Subtract all of our samples
	AnalogCapture* cap = new AnalogCapture;
	cap->m_samples.resize(din_p->m_samples.size());
	#pragma omp parallel for num_threads(4)
	for(size_t i=0; i<din_p->m_samples.size(); i++)
	{
		const AnalogSample& sin_p = din_p->m_samples[i];
		cap->m_samples[i] = AnalogSample(
			sin_p.m_offset,
			sin_p.m_duration,
			sin_p.m_sample - din_n->m_samples[i].m_sample);
	}

	SetData(cap);

	//Copy our time scales from the input
	//Use the first trace's timestamp as our start time if they differ
	cap->m_timescale = din_p->m_timescale;
	cap->m_startTimestamp = din_p->m_startTimestamp;
	cap->m_startPicoseconds = din_p->m_startPicoseconds;
}
