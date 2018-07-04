/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of EthernetAutonegotiationDecoder
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "../scopehal/AsciiRenderer.h"
#include "EthernetAutonegotiationRenderer.h"
#include "EthernetAutonegotiationDecoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetAutonegotiationDecoder::EthernetAutonegotiationDecoder(
	std::string hwname, std::string color)
	: ProtocolDecoder(hwname, OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EthernetAutonegotiationDecoder::NeedsConfig()
{
	return false;
}

ChannelRenderer* EthernetAutonegotiationDecoder::CreateRenderer()
{
	return new EthernetAutonegotiationRenderer(this);
}

bool EthernetAutonegotiationDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

std::string EthernetAutonegotiationDecoder::GetProtocolName()
{
	return "Ethernet Autonegotiation";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EthernetAutonegotiationDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	AnalogCapture* din = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//Create the outbound data
	auto* cap = new EthernetAutonegotiationCapture;
	m_timescale = m_channels[0]->m_timescale;
	cap->m_timescale = din->m_timescale;

	//Crunch it
	bool old_value = false;
	int64_t last_pulse = 0;
	bool code[16];
	int nbit = 0;
	int64_t frame_start = 0;
	bool last_was_data = false;
	for(size_t i = 0; i < din->m_samples.size(); i ++)
	{
		auto sample = din->m_samples[i];
		float v = sample;
		bool sample_value = (v > 1.25);
		int64_t tm = sample.m_offset * din->m_timescale;
		float dt = (tm - last_pulse) * 1e-6f;

		if(sample_value && !old_value)
		{
			//If delta is more than 150 us, we're starting a new frame and this is a clock pulse
			if(dt > 150)
			{
				nbit = 0;
				last_was_data = false;
				frame_start = sample.m_offset;
			}

			//If delta is less than 30 us, it's a glitch - skip it
			else if(dt < 30)
			{
			}

			//If we got a data pulse in the last cycle, this is a clock pulse. Don't touch the data
			else if(last_was_data)
			{
				last_was_data = false;
			}

			//If delta is more than 75 us, it's a clock pulse and the code bit was a zero
			else if(dt > 75)
			{
				code[nbit ++] = false;
				last_was_data = false;
			}

			//Delta is between 30 and 75 us. It's a "1" code bit
			else
			{
				code[nbit ++] = true;
				last_was_data = true;
			}

			//If we just read the 16th bit, crunch it
			if(nbit == 16)
			{
				uint16_t ncode = 0;
				for(int j=0; j<16; j++)
					ncode |= (code[j] << j);

				cap->m_samples.push_back(EthernetAutonegotiationSample(
					frame_start,
					sample.m_offset + sample.m_duration - frame_start,
					ncode));

				nbit = 0;
			}


			last_pulse = tm;
		}
		old_value = sample_value;
	}

	SetData(cap);
}
