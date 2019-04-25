
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
#include "USB2PacketDecoder.h"
#include "USB2PCSDecoder.h"
#include "USB2PacketRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

USB2PacketDecoder::USB2PacketDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("PCS");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* USB2PacketDecoder::CreateRenderer()
{
	return new USB2PacketRenderer(this);
}

bool USB2PacketDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (dynamic_cast<USB2PCSDecoder*>(channel) != NULL) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void USB2PacketDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "USB2Packet(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string USB2PacketDecoder::GetProtocolName()
{
	return "USB 1.x/2.0 Packet";
}

bool USB2PacketDecoder::IsOverlay()
{
	return true;
}

bool USB2PacketDecoder::NeedsConfig()
{
	return true;
}

double USB2PacketDecoder::GetVoltageRange()
{
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void USB2PacketDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	USB2PCSCapture* din = dynamic_cast<USB2PCSCapture*>(m_channels[0]->GetData());
	if( (din == NULL) || (din->GetDepth() == 0) )
	{
		SetData(NULL);
		return;
	}

	//Make the capture and copy our time scales from the input
	USB2PacketCapture* cap = new USB2PacketCapture;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	enum
	{
		STATE_IDLE,
		STATE_PID,
		STATE_END,
		STATE_TOKEN_0,
		STATE_TOKEN_1,
		STATE_SOF_0,
		STATE_SOF_1,
		STATE_DATA
	} state = STATE_IDLE;

	//Decode stuff
	uint8_t last = 0;
	uint64_t last_offset;
	for(size_t i=0; i<din->m_samples.size(); i++)
	{
		auto& sin = din->m_samples[i];

		switch(state)
		{
			case STATE_IDLE:

				//Expect IDLE or SYNC
				switch(sin.m_sample.m_type)
				{
					//Ignore idles
					case USB2PCSSymbol::TYPE_IDLE:
						break;

					//Start a new packet if we see a SYNC
					case USB2PCSSymbol::TYPE_SYNC:
						state = STATE_PID;
						break;

					//Anything else is an error
					default:
						cap->m_samples.push_back(USB2PacketSample(
							sin.m_offset,
							sin.m_duration,
							USB2PacketSymbol(USB2PacketSymbol::TYPE_ERROR, 0)));
						break;
				}

				break;

			//Started a new packet, expect PID
			case STATE_PID:

				//Should be data
				if(sin.m_sample.m_type != USB2PCSSymbol::TYPE_DATA)
				{
					cap->m_samples.push_back(USB2PacketSample(
						sin.m_offset,
						sin.m_duration,
						USB2PacketSymbol(USB2PacketSymbol::TYPE_ERROR, 0)));

					state = STATE_IDLE;
					continue;
				}

				//If the low bits don't match the complement of the high bits, we have a baed PID
				if( (sin.m_sample.m_data >> 4) != (0xf & ~sin.m_sample.m_data) )
				{
					cap->m_samples.push_back(USB2PacketSample(
						sin.m_offset,
						sin.m_duration,
						USB2PacketSymbol(USB2PacketSymbol::TYPE_ERROR, 0)));

					state = STATE_IDLE;
					continue;
				}

				//All good, add the PID
				cap->m_samples.push_back(USB2PacketSample(
					sin.m_offset,
					sin.m_duration,
					USB2PacketSymbol(USB2PacketSymbol::TYPE_PID, sin.m_sample.m_data)));

				//Look at the PID and decide what to expect next
				switch(sin.m_sample.m_data & 0xf)
				{
					case USB2PacketSymbol::PID_ACK:
					case USB2PacketSymbol::PID_STALL:
					case USB2PacketSymbol::PID_NAK:
					case USB2PacketSymbol::PID_NYET:
						state = STATE_END;
						break;

					//TODO: handle low bandwidth PRE stuff
					//for now assume USB 2.0 ERR
					case USB2PacketSymbol::PID_PRE_ERR:
						state = STATE_END;
						break;

					case USB2PacketSymbol::PID_IN:
					case USB2PacketSymbol::PID_OUT:
					case USB2PacketSymbol::PID_SETUP:
					case USB2PacketSymbol::PID_PING:
					case USB2PacketSymbol::PID_SPLIT:
						state = STATE_TOKEN_0;
						break;

					case USB2PacketSymbol::PID_SOF:
						state = STATE_SOF_0;
						break;

					case USB2PacketSymbol::PID_DATA0:
					case USB2PacketSymbol::PID_DATA1:
					case USB2PacketSymbol::PID_DATA2:
					case USB2PacketSymbol::PID_MDATA:
						state = STATE_DATA;
						break;
				}

				break;

			//Done, expect EOP
			case STATE_END:
				if(sin.m_sample.m_type != USB2PCSSymbol::TYPE_EOP)
				{
					cap->m_samples.push_back(USB2PacketSample(
						sin.m_offset,
						sin.m_duration,
						USB2PacketSymbol(USB2PacketSymbol::TYPE_ERROR, 0)));
				}
				break;

			//Tokens cross byte boundaries YAY!
			case STATE_TOKEN_0:

				//Pull out the 7-bit address
				cap->m_samples.push_back(USB2PacketSample(
					sin.m_offset,
					sin.m_duration,
					USB2PacketSymbol(USB2PacketSymbol::TYPE_ADDR, sin.m_sample.m_data & 0x7f)));

				last = sin.m_sample.m_data;

				state = STATE_TOKEN_1;
				break;

			case STATE_TOKEN_1:

				//Endpoint number
				cap->m_samples.push_back(USB2PacketSample(
					sin.m_offset,
					sin.m_duration / 2,
					USB2PacketSymbol(USB2PacketSymbol::TYPE_ENDP,
						( last >> 7) | ( (sin.m_sample.m_data & 0x7) << 1 )
						)));

				//CRC
				cap->m_samples.push_back(USB2PacketSample(
					sin.m_offset + sin.m_duration/2,
					sin.m_duration/2,
					USB2PacketSymbol(USB2PacketSymbol::TYPE_CRC5, sin.m_sample.m_data >> 3)));

				state = STATE_END;
				break;

			case STATE_SOF_0:

				last = sin.m_sample.m_data;
				last_offset = sin.m_offset;

				state = STATE_SOF_1;
				break;

			case STATE_SOF_1:

				//Frame number is the entire previous symbol, plus the low 3 bits of this one
				cap->m_samples.push_back(USB2PacketSample(
					last_offset,
					(sin.m_offset - last_offset) + sin.m_duration/2,
					USB2PacketSymbol(USB2PacketSymbol::TYPE_NFRAME,
						(sin.m_sample.m_data & 0x7 ) << 8 | last
						)));

				//CRC
				cap->m_samples.push_back(USB2PacketSample(
					sin.m_offset + sin.m_duration/2,
					sin.m_duration/2,
					USB2PacketSymbol(USB2PacketSymbol::TYPE_CRC5, sin.m_sample.m_data >> 3)));

				state = STATE_END;
				break;

			case STATE_DATA:

				//Assume data bytes are data (but they might be CRC, can't tell yet)
				if(sin.m_sample.m_type == USB2PCSSymbol::TYPE_DATA)
				{
					cap->m_samples.push_back(USB2PacketSample(
						sin.m_offset,
						sin.m_duration,
						USB2PacketSymbol(USB2PacketSymbol::TYPE_DATA, sin.m_sample.m_data)));
				}

				//Last two bytes were actually the CRC!
				//Merge them into the first one and delete the second
				else if(sin.m_sample.m_type == USB2PCSSymbol::TYPE_EOP)
				{
					auto& first = cap->m_samples[cap->m_samples.size() - 2];
					auto& second = cap->m_samples[cap->m_samples.size() - 1];
					first.m_duration += second.m_duration;
					first.m_sample.m_data = (first.m_sample.m_data << 8) | second.m_sample.m_data;
					first.m_sample.m_type = USB2PacketSymbol::TYPE_CRC16;

					cap->m_samples.resize(cap->m_samples.size()-1);
				}

				break;
		}

		//EOP always returns us to idle state
		if(sin.m_sample.m_type == USB2PCSSymbol::TYPE_EOP)
			state = STATE_IDLE;
	}

	//Done
	SetData(cap);
}
