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
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetRGMIIDecoder::EthernetRGMIIDecoder(string color)
	: EthernetProtocolDecoder(color)
{
	//Digital inputs, so need to undo some stuff for the PHY layer decodes
	m_signalNames.clear();
	m_channels.clear();

	//Add inputs. Make data be the first, because we normally want the overlay shown there.
	m_signalNames.push_back("data");
	m_channels.push_back(NULL);
	m_signalNames.push_back("clk");
	m_channels.push_back(NULL);
	m_signalNames.push_back("ctl");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EthernetRGMIIDecoder::GetProtocolName()
{
	return "Ethernet - RGMII";
}

bool EthernetRGMIIDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if(channel->GetType() != OscilloscopeChannel::CHANNEL_TYPE_DIGITAL)
		return false;

	switch(i)
	{
		case 0:
			if(channel->GetWidth() == 4)
				return true;
			break;

		case 1:
		case 2:
		case 3:
			if(channel->GetWidth() == 1)
				return true;
			break;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EthernetRGMIIDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "RGMII(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

void EthernetRGMIIDecoder::Refresh()
{
	ClearPackets();

	//Get the input data
	for(int i=0; i<4; i++)
	{
		if(m_channels[i] == NULL)
		{
			SetData(NULL);
			return;
		}
	}
	auto data = dynamic_cast<DigitalBusWaveform*>(m_channels[0]->GetData());
	auto clk = dynamic_cast<DigitalWaveform*>(m_channels[1]->GetData());
	auto ctl = dynamic_cast<DigitalWaveform*>(m_channels[2]->GetData());
	if( (data == NULL) || (clk == NULL) || (ctl == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Sample everything on the clock edges
	DigitalWaveform dctl;
	DigitalBusWaveform ddata;
	SampleOnAnyEdges(ctl, clk, dctl);
	SampleOnAnyEdges(data, clk, ddata);

	//Need a reasonable number of samples or there's no point in decoding.
	//Cut off the last sample since we're DDR.
	size_t len = min(dctl.m_samples.size(), ddata.m_samples.size());
	if(len < 100)
	{
		SetData(NULL);
		return;
	}
	len --;

	//Create the output capture
	auto cap = new EthernetWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startPicoseconds = data->m_startPicoseconds;

	for(size_t i=0; i < len; i++)
	{
		if(!dctl.m_samples[i])
			continue;

		//Set of recovered bytes and timestamps
		vector<uint8_t> bytes;
		vector<uint64_t> starts;
		vector<uint64_t> ends;

		//TODO: handle error signal (ignored for now)
		while( (i < len) && (dctl.m_samples[i]) )
		{
			//Start time
			starts.push_back(ddata.m_offsets[i]);

			//Convert bits to bytes
			uint8_t dval = 0;
			for(size_t j=0; j<8; j++)
			{
				if(j < 4)
				{
					if(ddata.m_samples[i][j])
						dval |= (1 << j);
				}
				else if(ddata.m_samples[i+1][j-4])
					dval |= (1 << j);
			}
			bytes.push_back(dval);

			ends.push_back(ddata.m_offsets[i+1] + ddata.m_durations[i+1]);
			i += 2;
		}

		//Crunch the data
		BytesToFrames(bytes, starts, ends, cap);
	}

	SetData(cap);
}
