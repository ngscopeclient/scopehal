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

#include "scopeprotocols.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetRGMIIDecoder::EthernetRGMIIDecoder(const string& color)
	: EthernetProtocolDecoder(color)
{
	//Digital inputs, so need to undo some stuff for the PHY layer decodes
	m_signalNames.clear();
	m_inputs.clear();

	//Add inputs. Make data be the first, because we normally want the overlay shown there.
	CreateInput("data");
	CreateInput("clk");
	CreateInput("ctl");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EthernetRGMIIDecoder::GetProtocolName()
{
	return "Ethernet - RGMII";
}

bool EthernetRGMIIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	auto chan = stream.m_channel;
	if(chan == NULL)
		return false;

	switch(i)
	{
		case 0:
			if(chan->GetType() != OscilloscopeChannel::CHANNEL_TYPE_DIGITAL_BUS)
				return true;
			break;

		case 1:
		case 2:
			if(chan->GetType() != OscilloscopeChannel::CHANNEL_TYPE_DIGITAL)
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
	snprintf(hwname, sizeof(hwname), "RGMII(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

void EthernetRGMIIDecoder::Refresh()
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
	auto ctl = GetDigitalInputWaveform(2);

	//Sample everything on the clock edges
	DigitalWaveform dctl;
	DigitalBusWaveform ddata;
	SampleOnAnyEdges(ctl, clk, dctl);
	SampleOnAnyEdges(data, clk, ddata);

	//Need a reasonable number of samples or there's no point in decoding.
	//Cut off the last few samples because we might be either DDR or SDR and need to seek past our current position.
	size_t len = min(dctl.m_samples.size(), ddata.m_samples.size());
	if(len < 100)
	{
		SetData(NULL, 0);
		return;
	}
	len -= 4;

	//Create the output capture
	auto cap = new EthernetWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startFemtoseconds = data->m_startFemtoseconds;

	//skip first 2 samples so we can get a full clock cycle before starting
	for(size_t i=2; i < len; i++)
	{
		//Not sending a frame. Decode in-band status
		if(!dctl.m_samples[i])
		{
			//Extract in-band status
			uint8_t status = 0;
			for(size_t j=0; j<4; j++)
			{
				if(ddata.m_samples[i][j])
					status |= (1 << j);
			}

			//Same status? Merge samples
			bool extend = false;
			size_t last = cap->m_samples.size() - 1;
			if(!cap->m_samples.empty())
			{
				auto& sample = cap->m_samples[last];
				if( (sample.m_type == EthernetFrameSegment::TYPE_INBAND_STATUS) &&
					(sample.m_data[0] == status) )
				{
					extend = true;
				}
			}

			//Decode
			if(extend)
				cap->m_durations[last] = ddata.m_offsets[i] + ddata.m_durations[i] - cap->m_offsets[last];
			else
			{
				cap->m_offsets.push_back(ddata.m_offsets[i]);
				cap->m_durations.push_back(ddata.m_durations[i]);
				cap->m_samples.push_back(EthernetFrameSegment(EthernetFrameSegment::TYPE_INBAND_STATUS, status));
			}

			continue;
		}

		//We're processing a frame.
		//Figure out the clock period.
		//Need to do this cycle-by-cycle in case the link speed changes during a deep capture
		//TODO: alert if clock isn't close to one of the three legal frequencies
		int64_t clkperiod = dctl.m_offsets[i] - dctl.m_offsets[i-2];
		bool ddr = false;		//Default to 2.5/25 MHz SDR.
		if(clkperiod < 10000000)	//Faster than 100 MHz? assume it's 125 MHz DDR.
			ddr = true;

		//Set of recovered bytes and timestamps
		vector<uint8_t> bytes;
		vector<uint64_t> starts;
		vector<uint64_t> ends;

		//TODO: handle error signal (ignored for now)
		while( (i < len) && (dctl.m_samples[i]) )
		{
			//Start time
			starts.push_back(ddata.m_offsets[i]);

			if(ddr)
			{
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

			else
			{
				//Convert bits to bytes
				uint8_t dval = 0;
				for(size_t j=0; j<8; j++)
				{
					if(j < 4)
					{
						if(ddata.m_samples[i][j])
							dval |= (1 << j);
					}
					else if(ddata.m_samples[i+2][j-4])
						dval |= (1 << j);
				}
				bytes.push_back(dval);

				ends.push_back(ddata.m_offsets[i+3] + ddata.m_durations[i+3]);
				i += 4;
			}
		}
		//Crunch the data
		BytesToFrames(bytes, starts, ends, cap);
	}

	SetData(cap, 0);
}
