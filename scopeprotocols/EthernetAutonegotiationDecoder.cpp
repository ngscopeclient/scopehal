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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of EthernetAutonegotiationDecoder
 */

#include "../scopehal/scopehal.h"
#include "EthernetAutonegotiationDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetAutonegotiationDecoder::EthernetAutonegotiationDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EthernetAutonegotiationDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

string EthernetAutonegotiationDecoder::GetProtocolName()
{
	return "Ethernet Autonegotiation";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EthernetAutonegotiationDecoder::Refresh()
{
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);

	//Create the outbound data
	auto* cap = new EthernetAutonegotiationWaveform;
	cap->m_timescale = din->m_timescale;

	//Crunch it
	bool old_value = false;
	int64_t last_pulse = 0;
	bool code[16];
	int nbit = 0;
	int64_t frame_start = 0;
	bool last_was_data = false;
	auto len = din->m_samples.size();
	for(size_t i = 0; i < len; i ++)
	{
		bool sample_value = (din->m_samples[i] > 1.25);
		int64_t tm = din->m_offsets[i] * din->m_timescale;
		float dt = (tm - last_pulse) * 1e-9f;

		if(sample_value && !old_value)
		{
			//If delta is more than 150 us, we're starting a new frame and this is a clock pulse
			if(dt > 150)
			{
				nbit = 0;
				last_was_data = false;
				frame_start = din->m_offsets[i];
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

				cap->m_offsets.push_back(frame_start);
				cap->m_durations.push_back(din->m_offsets[i] + din->m_durations[i] - frame_start);
				cap->m_samples.push_back(ncode);

				nbit = 0;
			}


			last_pulse = tm;
		}
		old_value = sample_value;
	}

	SetData(cap, 0);
}

Gdk::Color EthernetAutonegotiationDecoder::GetColor(int /*i*/)
{
	return m_standardColors[COLOR_DATA];
}

string EthernetAutonegotiationDecoder::GetText(int i)
{
	auto data = dynamic_cast<EthernetAutonegotiationWaveform*>(GetData(0));
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto s = data->m_samples[i];
	unsigned int sel = s & 0x1f;
	unsigned int ability = (s >> 5) & 0x7f;
	bool xnp = (s >> 12) & 1;
	bool rf = (s >> 13) & 1;
	bool ack = (s >> 14) & 1;
	bool np = (s >> 15) & 1;

	//Not 802.3? Just display as hex
	char tmp[128];
	if(sel != 1)
	{
		snprintf(tmp, sizeof(tmp), "%04x", (int)s);
		return tmp;
	}

	//Yes, it's 802.3
	string ret = "Base: ";
	if(ability & 0x40)
		ret += "apause ";
	if(ability & 0x20)
		ret += "pause ";
	if(ability & 0x10)
		ret += "T4 ";
	if(ability & 0xc)
	{
		ret += "100/";
		if( (ability & 0xc) == 0xc)
			ret += "full+half ";
		else if(ability & 0x8)
			ret += "full ";
		else if(ability & 0x4)
			ret += "half ";
	}
	if(ability & 0x3)
	{
		ret += "10/";
		if( (ability & 0x3) == 0x3)
			ret += "full+half ";
		else if(ability & 0x2)
			ret += "full ";
		else if(ability & 0x1)
			ret += "half ";
	}

	if(xnp)
		ret += "XNP ";
	if(rf)
		ret += "FAULT ";
	if(ack)
		ret += "ACK ";
	if(np)
		ret += "Next-page";

	return ret;
}
