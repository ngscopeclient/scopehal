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

ClockJitterDecoder::ClockJitterDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_CLOCK)
{
	m_yAxisUnit = Unit(Unit::UNIT_PS);

	//Set up channels
	m_signalNames.push_back("Clock");
	m_signalNames.push_back("Golden");
	m_channels.push_back(NULL);
	m_channels.push_back(NULL);

	m_maxTie = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* ClockJitterDecoder::CreateRenderer()
{
	return NULL;
}

bool ClockJitterDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void ClockJitterDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "TIE(%s, %s)",
		m_channels[0]->m_displayname.c_str(), m_channels[1]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string ClockJitterDecoder::GetProtocolName()
{
	return "Clock Jitter (TIE)";
}

bool ClockJitterDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool ClockJitterDecoder::NeedsConfig()
{
	//we have more than one input
	return true;
}

double ClockJitterDecoder::GetVoltageRange()
{
	return m_maxTie * 2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ClockJitterDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	AnalogCapture* clk = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	DigitalCapture* golden = dynamic_cast<DigitalCapture*>(m_channels[1]->GetData());
	if( (clk == NULL) || (golden == NULL) )
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = clk->m_samples.size();
	if(golden->m_samples.size() < len)
		len = golden->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Create the output
	AnalogCapture* cap = new AnalogCapture;

	//Timestamps of the edges
	vector<int64_t> edges;
	FindZeroCrossings(clk, 0, edges);

	m_maxTie = 1;

	//For each input clock edge, find the closest recovered clock edge
	size_t iedge = 0;
	for(auto atime : edges)
	{
		if(iedge >= len)
			break;

		int64_t prev_edge = golden->m_samples[iedge].m_offset * golden->m_timescale;
		int64_t next_edge = prev_edge;
		size_t jedge = iedge;

		bool hit = false;

		//Look for a pair of edges bracketing our edge
		while(true)
		{
			prev_edge = next_edge;
			next_edge = golden->m_samples[jedge].m_offset * golden->m_timescale;

			//First golden edge is after this signal edge
			if(prev_edge > atime)
				break;

			//Bracketed
			if( (prev_edge < atime) && (next_edge > atime) )
			{
				hit = true;
				break;
			}

			//No, keep looking
			jedge ++;

			//End of capture
			if(jedge >= len)
				break;
		}

		//No interval error possible without a reference clock edge.
		if(!hit)
			continue;

		//Hit! We're bracketed. Start the next search from this edge
		iedge = jedge;

		//Since the CDR filter adds a 90 degree phase offset for sampling in the middle of the data eye,
		//we need to use the *midpoint* of the golden clock cycle as the nominal position of the clock
		//edge for TIE measurements.
		int64_t golden_period = next_edge - prev_edge;
		int64_t golden_center = prev_edge + golden_period/2;
		golden_center += 1.5*clk->m_timescale;			//TODO: why is this needed?
		int64_t tie = atime - golden_center;

		m_maxTie = max(m_maxTie, fabs(tie));
		cap->m_samples.push_back(AnalogSample(
			atime, golden_period, tie));
	}

	SetData(cap);

	//Copy start time etc from the input
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startPicoseconds = 0;
}
