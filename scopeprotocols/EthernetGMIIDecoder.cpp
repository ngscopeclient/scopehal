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

EthernetGMIIDecoder::EthernetGMIIDecoder(const string& color)
	: EthernetProtocolDecoder(color)
{
	//Digital inputs, so need to undo some stuff for the PHY layer decodes
	m_signalNames.clear();
	m_inputs.clear();

	//Add inputs. Make data be the first, because we normally want the overlay shown there.
	CreateInput("data");
	CreateInput("clk");
	CreateInput("en");
	CreateInput("er");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EthernetGMIIDecoder::GetProtocolName()
{
	return "Ethernet - GMII";
}

bool EthernetGMIIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	auto chan = stream.m_channel;
	if(chan == NULL)
		return false;

	if(chan->GetType() != OscilloscopeChannel::CHANNEL_TYPE_DIGITAL)
		return false;

	switch(i)
	{
		case 0:
			if(chan->GetWidth() == 8)
				return true;
			break;

		case 1:
		case 2:
		case 3:
			if(chan->GetWidth() == 1)
				return true;
			break;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EthernetGMIIDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "GMII(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

void EthernetGMIIDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto data = GetDigitalBusInputWaveform(0);
	auto clk = GetDigitalInputWaveform(1);
	auto en = GetDigitalInputWaveform(2);
	auto er = GetDigitalInputWaveform(3);

	//Sample everything on the clock edges
	DigitalWaveform den;
	DigitalWaveform der;
	DigitalBusWaveform ddata;
	SampleOnRisingEdges(en, clk, den);
	SampleOnRisingEdges(er, clk, der);
	SampleOnRisingEdges(data, clk, ddata);

	//Create the output capture
	auto cap = new EthernetWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startPicoseconds = data->m_startPicoseconds;

	size_t len = den.m_samples.size();
	len = min(len, der.m_samples.size());
	len = min(len, ddata.m_samples.size());
	for(size_t i=0; i < len; i++)
	{
		if(!den.m_samples[i])
			continue;

		//Set of recovered bytes and timestamps
		vector<uint8_t> bytes;
		vector<uint64_t> starts;
		vector<uint64_t> ends;

		//TODO: handle error signal (ignored for now)
		while( (i < len) && (den.m_samples[i]) )
		{
			//Convert bits to bytes
			uint8_t dval = 0;
			for(size_t j=0; j<8; j++)
			{
				if(ddata.m_samples[i][j])
					dval |= (1 << j);
			}

			bytes.push_back(dval);
			starts.push_back(ddata.m_offsets[i]);
			ends.push_back(ddata.m_offsets[i] + ddata.m_durations[i]);
			i++;
		}

		//Crunch the data
		BytesToFrames(bytes, starts, ends, cap);
	}

	SetData(cap, 0);
}
