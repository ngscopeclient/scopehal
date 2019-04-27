
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
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
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

bool USB2PacketDecoder::GetShowDataColumn()
{
	return false;
}

vector<string> USB2PacketDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Device");
	ret.push_back("Endpoint");
	ret.push_back("Length");
	ret.push_back("Details");
	return ret;
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

				//If the low bits don't match the complement of the high bits, we have a bad PID
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

	//Decode packets in the capture
	FindPackets(cap);
}

void USB2PacketDecoder::FindPackets(USB2PacketCapture* cap)
{
	ClearPackets();

	//Stop when we have no chance of fitting a full packet
	for(size_t i=0; i<cap->m_samples.size() - 2;)
	{
		//Every packet should start with a PID. Discard unknown garbage.
		auto& psample = cap->m_samples[i];
		if(psample.m_sample.m_type != USB2PacketSymbol::TYPE_PID)
		{
			i++;
			continue;
		}
		uint8_t pid = psample.m_sample.m_data & 0xf;
		i++;

		//See what the PID is
		switch(pid)
		{
			case USB2PacketSymbol::PID_SOF:
				DecodeSof(cap, psample, i);
				break;

			case USB2PacketSymbol::PID_SETUP:
				DecodeSetup(cap, psample, i);
				break;

			case USB2PacketSymbol::PID_IN:
			case USB2PacketSymbol::PID_OUT:
				DecodeData(cap, psample, i);
				break;

			default:
				LogDebug("Unexpected PID %x\n", pid);
		}
	}
}

void USB2PacketDecoder::DecodeSof(USB2PacketCapture* cap, USB2PacketSample& start, size_t& i)
{
	//A SOF should contain a TYPE_NFRAME and a TYPE_CRC5
	//Bail out if we only have part of the packet
	if(i+1 >= cap->m_samples.size())
	{
		LogDebug("Truncated SOF\n");
		return;
	}

	//TODO: better display for invalid/malformed packets
	USB2PacketSample& snframe = cap->m_samples[i++];
	USB2PacketSample& scrc = cap->m_samples[i++];
	if(snframe.m_sample.m_type != USB2PacketSymbol::TYPE_NFRAME)
		return;
	if(scrc.m_sample.m_type != USB2PacketSymbol::TYPE_CRC5)
		return;

	//Make the packet
	Packet* pack = new Packet;
	pack->m_offset = start.m_offset * cap->m_timescale;
	pack->m_headers["Type"] = "SOF";
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "Sequence = %u", snframe.m_sample.m_data);
	pack->m_headers["Details"] = tmp;
	pack->m_len = ((scrc.m_offset + scrc.m_duration) * cap->m_timescale) - pack->m_offset;
	m_packets.push_back(pack);

	pack->m_headers["Device"] = "--";
	pack->m_headers["Endpoint"] = "--";
	pack->m_headers["Length"] = "2";
}

void USB2PacketDecoder::DecodeSetup(USB2PacketCapture* cap, USB2PacketSample& start, size_t& i)
{
	//A SETUP packet should contain ADDR, ENDP, CRC5
	//Bail out if we only have part of the packet.
	if(i+2 >= cap->m_samples.size())
	{
		LogDebug("Truncated SETUP\n");
		return;
	}
	USB2PacketSample& saddr = cap->m_samples[i++];
	USB2PacketSample& sendp = cap->m_samples[i++];
	USB2PacketSample& scrc = cap->m_samples[i++];

	//TODO: better display for invalid/malformed packets
	if(saddr.m_sample.m_type != USB2PacketSymbol::TYPE_ADDR)
	{
		LogError("not TYPE_ADDR\n");
		return;
	}
	if(sendp.m_sample.m_type != USB2PacketSymbol::TYPE_ENDP)
	{
		LogError("not TYPE_ENDP\n");
		return;
	}
	if(scrc.m_sample.m_type != USB2PacketSymbol::TYPE_CRC5)
	{
		LogError("not TYPE_CRC5\n");
		return;
	}

	//Expect a DATA0 packet next
	//Should be PID, 8 bytes, CRC16.
	//Bail out if we only have part of the packet.
	if(i+9 >= cap->m_samples.size())
	{
		LogDebug("Truncated data\n");
		return;
	}
	USB2PacketSample& sdatpid = cap->m_samples[i++];
	if(sdatpid.m_sample.m_type != USB2PacketSymbol::TYPE_PID)
	{
		LogError("Not PID\n");
		return;
	}
	if( (sdatpid.m_sample.m_data & 0xf) != USB2PacketSymbol::PID_DATA0)
	{
		LogError("not DATA0\n");
		return;
	}
	uint16_t data[8] = {0};
	for(int j=0; j<8; j++)
	{
		USB2PacketSample& sdat = cap->m_samples[i++];
		if(sdat.m_sample.m_type != USB2PacketSymbol::TYPE_DATA)
		{
			LogError("not data\n");
			return;
		}
		data[j] = sdat.m_sample.m_data;
	}
	USB2PacketSample& sdcrc = cap->m_samples[i++];
	if(sdcrc.m_sample.m_type != USB2PacketSymbol::TYPE_CRC16)
	{
		LogError("not CRC16\n");
		return;
	}

	//Expect ACK/NAK
	string ack = "";
	if(i >= cap->m_samples.size())
	{
		LogDebug("Truncated ACK\n");
		return;
	}
	USB2PacketSample& sack = cap->m_samples[i++];
	if(sack.m_sample.m_type == USB2PacketSymbol::TYPE_PID)
	{
		if( (sack.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_ACK)
			ack = "ACK";
		else if( (sack.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_NAK)
			ack = "NAK";
		else
			ack = "Unknown end PID";
	}

	//Make the packet
	Packet* pack = new Packet;
	pack->m_offset = start.m_offset * cap->m_timescale;
	pack->m_headers["Type"] = "SETUP";
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%d", saddr.m_sample.m_data);
	pack->m_headers["Device"] = tmp;
	snprintf(tmp, sizeof(tmp), "%d", sendp.m_sample.m_data);
	pack->m_headers["Endpoint"] = tmp;
	pack->m_headers["Length"] = "8";	//constant

	//Decode setup details
	uint8_t bmRequestType = data[0];
	uint8_t bRequest = data[1];
	uint16_t wValue = (data[3] << 8) | data[2];
	uint16_t wIndex = (data[5] << 8) | data[4];
	uint16_t wLength = (data[7] << 8) | data[6];
	bool out = bmRequestType >> 7;
	uint8_t type = (bmRequestType  >> 5) & 3;
	uint8_t dest = bmRequestType & 0x1f;
	string stype;
	switch(type)
	{
		case 0:
			stype = "Standard";
			break;
		case 1:
			stype = "Class";
			break;
		case 2:
			stype = "Vendor";
			break;
		case 3:
		default:
			stype = "Reserved";
			break;
	}
	string sdest;
	switch(dest)
	{
		case 0:
			sdest = "device";
			break;
		case 1:
			sdest = "interface";
			break;
		case 2:
			sdest = "endpoint";
			break;
		case 3:
		default:
			sdest = "reserved";
			break;
	}
	snprintf(
		tmp,
		sizeof(tmp),
		"%s %s req to %s bRequest=%x wValue=%x wIndex=%x wLength=%u %s",
		out ? "Host:" : "Dev:",
		stype.c_str(),
		sdest.c_str(),
		bRequest,
		wValue,
		wIndex,
		wLength,
		ack.c_str());
	pack->m_headers["Details"] = tmp;

	//Done
	pack->m_len = ((sdcrc.m_offset + sdcrc.m_duration) * cap->m_timescale) - pack->m_offset;
	m_packets.push_back(pack);
}

void USB2PacketDecoder::DecodeData(USB2PacketCapture* cap, USB2PacketSample& start, size_t& i)
{
	//The IN/OUT packet should contain ADDR, ENDP, CRC5
	//Bail out if we only have part of the packet.
	if(i+2 >= cap->m_samples.size())
		return;
	USB2PacketSample& saddr = cap->m_samples[i++];
	USB2PacketSample& sendp = cap->m_samples[i++];
	USB2PacketSample& scrc = cap->m_samples[i++];

	//TODO: better display for invalid/malformed packets
	if(saddr.m_sample.m_type != USB2PacketSymbol::TYPE_ADDR)
	{
		LogError("not TYPE_ADDR\n");
		return;
	}
	if(sendp.m_sample.m_type != USB2PacketSymbol::TYPE_ENDP)
	{
		LogError("not TYPE_ENDP\n");
		return;
	}
	if(scrc.m_sample.m_type != USB2PacketSymbol::TYPE_CRC5)
	{
		LogError("not TYPE_CRC5\n");
		return;
	}

	//Expect minimum DATA, 0 or more data bytes, ACK
	if(i >= cap->m_samples.size())
	{
		LogDebug("Truncated DATA\n");
		return;
	}

	char tmp[256];

	//Look for the DATA packet after the IN/OUT
	USB2PacketSample& sdatpid = cap->m_samples[i];
	if(sdatpid.m_sample.m_type != USB2PacketSymbol::TYPE_PID)
	{
		LogError("Not PID\n");
		return;
	}
	//We can get a SOF thrown in anywhere, handle that first
	if( (sdatpid.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_SOF)
	{
		LogDebug("Random SOF in data stream (i=%zu)\n", i);
		DecodeSof(cap, sdatpid, i);
		sdatpid = cap->m_samples[i];
	}
	else if( (sdatpid.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_NAK)
	{
		i++;

		//Add a line for the aborted transaction
		Packet* pack = new Packet;
		pack->m_offset = start.m_offset * cap->m_timescale;
		if( (start.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_IN)
			pack->m_headers["Type"] = "IN";
		else
			pack->m_headers["Type"] = "OUT";
		char tmp[256];
		snprintf(tmp, sizeof(tmp), "%d", saddr.m_sample.m_data);
		pack->m_headers["Device"] = tmp;
		snprintf(tmp, sizeof(tmp), "%d", sendp.m_sample.m_data);
		pack->m_headers["Endpoint"] = tmp;
		pack->m_headers["Details"] = "NAK";
		m_packets.push_back(pack);
		return;
	}
	else	//normal data
		i++;
	if( ( (sdatpid.m_sample.m_data & 0xf) != USB2PacketSymbol::PID_DATA0) &&
		( (sdatpid.m_sample.m_data & 0xf) != USB2PacketSymbol::PID_DATA1) )
	{
		LogError("Not data PID (%x, i=%zu)\n", sdatpid.m_sample.m_data, i);

		//DEBUG
		Packet* pack = new Packet;
		pack->m_offset = start.m_offset * cap->m_timescale;
		pack->m_headers["Details"] = "ERROR";
		m_packets.push_back(pack);
		return;
	}

	//Create the new packet
	Packet* pack = new Packet;
	pack->m_offset = start.m_offset * cap->m_timescale;
	if( (start.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_IN)
		pack->m_headers["Type"] = "IN";
	else
		pack->m_headers["Type"] = "OUT";
	snprintf(tmp, sizeof(tmp), "%d", saddr.m_sample.m_data);
	pack->m_headers["Device"] = tmp;
	snprintf(tmp, sizeof(tmp), "%d", sendp.m_sample.m_data);
	pack->m_headers["Endpoint"] = tmp;

	//Read the data
	while(i < cap->m_samples.size())
	{
		USB2PacketSample& s = cap->m_samples[i++];

		//Keep adding data
		if(s.m_sample.m_type == USB2PacketSymbol::TYPE_DATA)
		{
			pack->m_data.push_back(s.m_sample.m_data);
			pack->m_len = ((s.m_offset + s.m_duration) * cap->m_timescale) - pack->m_offset;
		}

		//Next should be a CRC16
		else if(s.m_sample.m_type == USB2PacketSymbol::TYPE_CRC16)
		{
			//TODO: verify the CRC
			break;
		}
	}

	//Expect ACK/NAK
	if(i >= cap->m_samples.size())
	{
		LogDebug("Truncated ACK\n");
		return;
	}
	string ack = "";
	USB2PacketSample& sack = cap->m_samples[i++];
	if(sack.m_sample.m_type == USB2PacketSymbol::TYPE_PID)
	{
		if( (sack.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_ACK)
			ack = "";
		else if( (sack.m_sample.m_data & 0xf) == USB2PacketSymbol::PID_NAK)
			ack = "NAK";
		else
			ack = "Unknown end PID";
	}

	//TODO: handle errors better
	else
	{
		LogDebug("DecodeData got type %x instead of ACK/NAK\n", sack.m_sample.m_type);
		ack = "Not a PID";
	}

	//Format the data
	string details = "";
	for(auto b : pack->m_data)
	{
		snprintf(tmp, sizeof(tmp), "%02x ", b);
		details += tmp;
	}
	details += ack;
	pack->m_headers["Details"] = details;

	snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
	pack->m_headers["Length"] = tmp;

	m_packets.push_back(pack);
}
