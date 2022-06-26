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

#include "../scopehal/scopehal.h"
#include "VICPDecoder.h"
#include "TCPDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VICPDecoder::VICPDecoder(const string& color)
	: PacketDecoder(color, CAT_SERIAL)
{
	CreateInput("TX");
	CreateInput("RX");
}

VICPDecoder::~VICPDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool VICPDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (dynamic_cast<TCPWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string VICPDecoder::GetProtocolName()
{
	return "VICP";
}

vector<string> VICPDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Op");
	ret.push_back("Direction");
	ret.push_back("Sequence");
	ret.push_back("Length");
	ret.push_back("Data");
	return ret;
}

bool VICPDecoder::GetShowDataColumn()
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void VICPDecoder::Refresh()
{
	ClearPackets();

	auto tx = dynamic_cast<TCPWaveform*>(GetInputWaveform(0));
	auto rx = dynamic_cast<TCPWaveform*>(GetInputWaveform(1));
	if( (tx == nullptr) || (rx == nullptr) )
	{
		SetData(nullptr, 0);
		return;
	}

	//Create the waveform. Call SetData() early on so we can use GetText() in the packet decode
	auto cap = new VICPWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = tx->m_startTimestamp;
	cap->m_startFemtoseconds = tx->m_startFemtoseconds;
	cap->m_triggerPhase = 0;
	SetData(cap, 0);

	size_t txlen = tx->m_samples.size();
	size_t rxlen = rx->m_samples.size();
	size_t itx = 0;
	size_t irx = 0;
	Packet* pack = nullptr;
	while(true)
	{
		//At end of both input streams? Stop
		if( (itx >= txlen) && (irx >= rxlen) )
			break;

		//See if the next TX or RX packet comes first
		int64_t nextTX = 0x7fffffffffffffffL;
		if(itx < txlen)
			nextTX = tx->m_offsets[itx]*tx->m_timescale + tx->m_triggerPhase;

		int64_t nextRX = 0x7fffffffffffffffL;
		if(irx < rxlen)
			nextRX = rx->m_offsets[irx]*rx->m_timescale + rx->m_triggerPhase;

		//Grab the waveform we're working with
		bool nextIsTx = (nextTX <= nextRX);
		size_t& i = nextIsTx ? itx : irx;
		size_t len = nextIsTx ? txlen : rxlen;
		TCPWaveform* p = nextIsTx ? tx : rx;

		int payloadBytesLeft = 0;

		//Process it
		int state = 0;
		bool done = false;
		bool continuing = false;
		while(i < len)
		{
			auto& sym = p->m_samples[i];
			bool err = false;

			int64_t start = p->m_offsets[i] * p->m_timescale + p->m_triggerPhase;
			int64_t dur = p->m_durations[i] * p->m_timescale;

			//If we see an error symbol, stop the current packet and move on
			if(sym.m_type == TCPSymbol::TYPE_ERROR)
			{
				i++;
				break;
			}

			//Try continuing an existing TCP segment as a new frame.
			if(continuing && (sym.m_type != TCPSymbol::TYPE_DATA) )
			{
				continuing = false;
				break;
			}

			//Unexpected source port? This packet is over, move on to the next one
			if( (state != 0) && (state != 11) && (sym.m_type == TCPSymbol::TYPE_SOURCE_PORT) )
				break;

			switch(state)
			{
				//Start of a new TCP segment (source port)
				case 0:

					//If we see "data" we might be continuing an existing TCP segment with a new VICP frame.
					if( (sym.m_type == TCPSymbol::TYPE_DATA) && continuing)
						state = 2;

					//Expect "source port", abort if we see anything else
					else if(sym.m_type != TCPSymbol::TYPE_SOURCE_PORT)
						err = true;

					else
					{
						//All good, we have a source port.
						//If we are on the TX side, it should be 1861 (VICP), 0x0745.
						//Don't care about RX.
						if(nextIsTx)
						{
							if( (sym.m_data[0] != 0x07) || (sym.m_data[1] != 0x45) )
							{
								err = true;
								break;
							}
						}

						//If we get here, port number is valid or dontcare.
						//Move on to destination port.
						state = 1;
						i++;
					}
					break;

				//Destination port
				case 1:

					//Expect "dest port", abort if we see anything else
					if(sym.m_type != TCPSymbol::TYPE_DEST_PORT)
					{
						err = true;
						break;
					}

					//All good, we have a dest port.
					//If we are on the RX side, it should be 1861 (VICP), 0x0745.
					//Don't care about TX.
					if(!nextIsTx)
					{
						if( (sym.m_data[0] != 0x07) || (sym.m_data[1] != 0x45) )
						{
							err = true;
							break;
						}
					}

					//If we get here, port number is valid or dontcare.
					//Move on to remaining header fields
					state = 2;
					i++;
					break;

				//Discard all headers
				case 2:

					//Anything but data? We don't care, ignore it
					if(sym.m_type != TCPSymbol::TYPE_DATA)
						i++;

					else
					{
						//It's a data byte! Specifically, our opcode field.
						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(VICPSymbol(VICPSymbol::TYPE_OPCODE, sym.m_data[0]));

						//Create a new packet
						pack = new Packet;
						pack->m_offset = start;
						pack->m_len = 0;
						m_packets.push_back(pack);

						//Save the opcode
						pack->m_headers["Op"] = GetText(cap->m_samples.size() - 1);

						//Set color to reflect direction of the packet
						if(!nextIsTx)
						{
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							pack->m_headers["Direction"] = "Command";
						}
						else
						{
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							pack->m_headers["Direction"] = "Reply";
						}

						state = 3;
						i++;
					}

					break;

				//Expect protocol version 0x01
				case 3:

					//Should be data
					if(sym.m_type != TCPSymbol::TYPE_DATA)
						err = true;

					else
					{
						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(VICPSymbol(VICPSymbol::TYPE_VERSION, sym.m_data[0]));

						state = 4;
						i++;
					}
					break;

				//Sequence number
				case 4:

					//Should be data
					if(sym.m_type != TCPSymbol::TYPE_DATA)
						err = true;

					else
					{
						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(VICPSymbol(VICPSymbol::TYPE_SEQ, sym.m_data[0]));

						//Save the sequence number header
						pack->m_headers["Sequence"] = to_string(sym.m_data[0]);

						state = 5;
						i++;
					}
					break;

				//Expect reserved 0x00
				case 5:

					//Should be data
					if(sym.m_type != TCPSymbol::TYPE_DATA)
						err = true;

					else
					{
						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(VICPSymbol(VICPSymbol::TYPE_RESERVED, sym.m_data[0]));

						state = 6;
						i++;
					}
					break;

				//Start of length header
				case 6:

					//Should be data
					if(sym.m_type != TCPSymbol::TYPE_DATA)
						err = true;

					else
					{
						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(VICPSymbol(VICPSymbol::TYPE_LENGTH, sym.m_data[0]));

						state = 7;
						i++;
					}

					break;

				//Continue length header
				case 7:
				case 8:
				case 9:

					//Should be data
					if(sym.m_type != TCPSymbol::TYPE_DATA)
						err = true;

					else
					{
						size_t clen = cap->m_offsets.size();
						cap->m_durations[clen-1] = (start+dur) - cap->m_offsets[clen-1];
						cap->m_samples[clen-1].m_data = (cap->m_samples[clen-1].m_data << 8) | sym.m_data[0];

						payloadBytesLeft = cap->m_samples[clen-1].m_data;
						pack->m_headers["Length"] = to_string(payloadBytesLeft);

						state++;
						i++;
					}
					break;

				//First byte of payload data
				case 10:

					//Should be data
					if(sym.m_type != TCPSymbol::TYPE_DATA)
						err = true;

					else
					{
						string tmp;
						tmp += (char)sym.m_data[0];

						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(VICPSymbol(VICPSymbol::TYPE_DATA, tmp));

						state = 11;
						i++;

						payloadBytesLeft --;
					}

					break;

				//Additional payload data
				case 11:

					//Start of a new TCP segment before we've hit the end of the frame?
					//Skip headers then go back here
					if(sym.m_type == TCPSymbol::TYPE_SOURCE_PORT)
					{
						i++;
						state = 12;
					}

					//Should be data, abort if we see something else
					else if(sym.m_type != TCPSymbol::TYPE_DATA)
						err = true;

					else
					{
						size_t clen = cap->m_offsets.size();
						cap->m_durations[clen-1] = (start+dur) - cap->m_offsets[clen-1];

						//Truncate displayed content to keep the UI size reasonable
						if(cap->m_samples[clen-1].m_str.length() > 256)
						{
						}

						else
						{
							char ch = sym.m_data[0];
							if(ch == '\r')
								cap->m_samples[clen-1].m_str += "\\r";
							else if(ch == '\n')
								cap->m_samples[clen-1].m_str += "\\n";
							else if(!isprint(ch))
								cap->m_samples[clen-1].m_str += ".";
							else
								cap->m_samples[clen-1].m_str += ch;

							pack->m_headers["Data"] = cap->m_samples[clen-1].m_str;
						}

						i++;
						payloadBytesLeft --;

						if(payloadBytesLeft == 0)
							done = true;
					}

					break;

				//Discard extra headers for data split across multiple TCP segments
				case 12:
					if(sym.m_type == TCPSymbol::TYPE_DATA)
					{
						state = 11;
					}
					else
						i++;
					break;

			}

			//Error? Discard whatever we were looking at and move on
			if(err)
			{
				i++;
				break;
			}

			if(done)
			{
				state = 0;
				continuing = true;
			}
			else
				continuing = false;
		}
	}
}

Gdk::Color VICPDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<VICPWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const VICPSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case VICPSymbol::TYPE_RESERVED:
				if(s.m_data == 0)
					return m_standardColors[COLOR_PREAMBLE];
				else
					return m_standardColors[COLOR_ERROR];

			case VICPSymbol::TYPE_OPCODE:
				return m_standardColors[COLOR_CONTROL];

			case VICPSymbol::TYPE_VERSION:
				if(s.m_data == 1)
					return m_standardColors[COLOR_CONTROL];
				else
					return m_standardColors[COLOR_ERROR];

			case VICPSymbol::TYPE_SEQ:
				return m_standardColors[COLOR_CONTROL];

			case VICPSymbol::TYPE_LENGTH:
				return m_standardColors[COLOR_ADDRESS];

			case VICPSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string VICPDecoder::GetText(int i)
{
	auto capture = dynamic_cast<VICPWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const VICPSymbol& s = capture->m_samples[i];
		char tmp[128];

		switch(s.m_type)
		{
			case VICPSymbol::TYPE_OPCODE:
				{
					string ret = "";
					if(s.m_data & 0x80)
						ret += "DATA ";
					if(s.m_data & 0x40)
						ret += "REMOTE ";
					if(s.m_data & 0x20)
						ret += "LOCKOUT ";
					if(s.m_data & 0x10)
						ret += "CLEAR ";
					if(s.m_data & 0x8)
						ret += "SRQ ";
					if(s.m_data & 0x4)
						ret += "REQ ";
					//0x02 reserved?
					if(s.m_data & 0x1)
						ret += "EOI ";

					return ret;
				}
				break;


			case VICPSymbol::TYPE_VERSION:
				snprintf(tmp, sizeof(tmp), "Version %d", s.m_data);
				return string(tmp);

			case VICPSymbol::TYPE_SEQ:
				snprintf(tmp, sizeof(tmp), "Seq %d", s.m_data);
				return string(tmp);

			case VICPSymbol::TYPE_RESERVED:
				if(s.m_data == 0)
					return "RESERVED";
				else
					return "ERROR";

			case VICPSymbol::TYPE_LENGTH:
				snprintf(tmp, sizeof(tmp), "Len %d", s.m_data);
				return string(tmp);

			case VICPSymbol::TYPE_DATA:
				return s.m_str;

			default:
				return "ERROR";
		}

		return string(tmp);
	}
	return "";
}

bool VICPDecoder::CanMerge(Packet* /*first*/, Packet* /*cur*/, Packet* /*next*/)
{
	return false;
}

Packet* VICPDecoder::CreateMergedHeader(Packet* /*pack*/, size_t /*i*/)
{
	return NULL;
}
