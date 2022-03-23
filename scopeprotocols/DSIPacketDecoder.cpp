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
#include "DSIPacketDecoder.h"
#include "DPhyDataDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DSIPacketDecoder::DSIPacketDecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	CreateInput("data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DSIPacketDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<DPhyDataDecoder*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DSIPacketDecoder::GetProtocolName()
{
	return "MIPI DSI Packet";
}

bool DSIPacketDecoder::NeedsConfig()
{
	//No config needed
	return false;
}

void DSIPacketDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DSIPacket(%s)",	GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DSIPacketDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<DPhyDataWaveform*>(GetInputWaveform(0));
	size_t len = din->m_samples.size();

	enum
	{
		STATE_IDLE,
		STATE_HEADER,

		STATE_LONG_LEN_LO,
		STATE_LONG_LEN_HI,
		STATE_LONG_ECC,
		STATE_LONG_DATA,
		STATE_LONG_CHECKSUM_LO,
		STATE_LONG_CHECKSUM_HI,

		STATE_SHORT_DATA_0,
		STATE_SHORT_DATA_1,
		STATE_SHORT_ECC,

		STATE_DROP
	} state = STATE_IDLE;

	//Create output waveform
	auto cap = new DSIWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	SetData(cap, 0);

	//Main decode loop
	size_t current_len = 0;
	size_t bytes_read = 0;
	uint8_t current_type = 0;
	uint8_t current_vc = 0;
	uint16_t expected_checksum = 0;
	uint16_t current_checksum = 0;
	int64_t tstart = 0;
	Packet* pack = NULL;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];
		auto off = din->m_offsets[i];
		auto dur = din->m_durations[i];
		auto end = off + dur;
		auto halfdur = dur/2;

		switch(state)
		{
			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Headers

			//Wait for a SOF
			case STATE_IDLE:
				if(s.m_type == DPhyDataSymbol::TYPE_SOT)
					state = STATE_HEADER;
				break;	//end STATE_IDLE

			//Read header byte and figure out packet type
			case STATE_HEADER:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					current_type = s.m_data & 0x3f;
					current_vc = s.m_data >> 6;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(halfdur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_VC, current_vc));

					switch(current_type)
					{
						//Type codes for long packets
						case TYPE_NULL:
						case TYPE_BLANKING:
						case TYPE_GENERIC_LONG_WRITE:
						case TYPE_DCS_LONG_WRITE:
						case TYPE_PACKED_PIXEL_RGB565:
						case TYPE_PACKED_PIXEL_RGB666:
						case TYPE_LOOSE_PIXEL_RGB666:
						case TYPE_PACKED_PIXEL_RGB888:
							state = STATE_LONG_LEN_LO;
							cap->m_offsets.push_back(off + halfdur);
							cap->m_durations.push_back(dur - halfdur);
							cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_IDENTIFIER, current_type));
							break;

						//Type codes for short packets
						case TYPE_VSYNC_START:
						case TYPE_VSYNC_END:
						case TYPE_HSYNC_START:
						case TYPE_HSYNC_END:
						case TYPE_EOTP:
						case TYPE_CM_OFF:
						case TYPE_CM_ON:
						case TYPE_SHUT_DOWN:
						case TYPE_TURN_ON:
						case TYPE_GENERIC_SHORT_WRITE_0PARAM:
						case TYPE_GENERIC_SHORT_WRITE_1PARAM:
						case TYPE_GENERIC_SHORT_WRITE_2PARAM:
						case TYPE_GENERIC_READ_0PARAM:
						case TYPE_GENERIC_READ_1PARAM:
						case TYPE_GENERIC_READ_2PARAM:
						case TYPE_DCS_SHORT_WRITE_0PARAM:
						case TYPE_DCS_SHORT_WRITE_1PARAM:
						case TYPE_DCS_READ:
						case TYPE_SET_MAX_RETURN_SIZE:
							state = STATE_SHORT_DATA_0;
							cap->m_offsets.push_back(off + halfdur);
							cap->m_durations.push_back(dur - halfdur);
							cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_IDENTIFIER, current_type));
							break;

						//Any unknown type gets dropped.
						//We have to discard the rest of the burst because we can't know length of this packet otherwise.
						default:
							cap->m_offsets.push_back(off + halfdur);
							cap->m_durations.push_back(dur - halfdur);
							cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
							state = STATE_DROP;
							break;
					}

					//Create the packet
					pack = new Packet;
					pack->m_offset = off * din->m_timescale;
					pack->m_len = 0;
					pack->m_headers["VC"] = to_string(current_vc);
					pack->m_headers["Type"] = GetText(cap->m_offsets.size() - 1);
					m_packets.push_back(pack);

					//Set the color for the packet
					switch(current_type)
					{
						//Framing/blanking/ignored
						case TYPE_NULL:
						case TYPE_BLANKING:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DEFAULT];
							break;

						//Sync and commands
						case TYPE_EOTP:
						case TYPE_VSYNC_START:
						case TYPE_VSYNC_END:
						case TYPE_HSYNC_START:
						case TYPE_HSYNC_END:
						case TYPE_CM_OFF:
						case TYPE_CM_ON:
						case TYPE_SHUT_DOWN:
						case TYPE_TURN_ON:
						case TYPE_SET_MAX_RETURN_SIZE:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//Write data
						case TYPE_GENERIC_LONG_WRITE:
						case TYPE_DCS_LONG_WRITE:
						case TYPE_PACKED_PIXEL_RGB565:
						case TYPE_PACKED_PIXEL_RGB666:
						case TYPE_LOOSE_PIXEL_RGB666:
						case TYPE_PACKED_PIXEL_RGB888:
						case TYPE_GENERIC_SHORT_WRITE_0PARAM:
						case TYPE_GENERIC_SHORT_WRITE_1PARAM:
						case TYPE_GENERIC_SHORT_WRITE_2PARAM:
						case TYPE_DCS_SHORT_WRITE_0PARAM:
						case TYPE_DCS_SHORT_WRITE_1PARAM:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							break;

						//Read data
						case TYPE_GENERIC_READ_0PARAM:
						case TYPE_GENERIC_READ_1PARAM:
						case TYPE_GENERIC_READ_2PARAM:
						case TYPE_DCS_READ:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						//Invalid/illegal
						default:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
							break;
					}
				}
				else if(s.m_type == DPhyDataSymbol::TYPE_EOT)
					state = STATE_IDLE;
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_HEADER

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Long packets

			//Length of a long packet
			case STATE_LONG_LEN_LO:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					tstart = off;
					current_len = s.m_data;
					state = STATE_LONG_LEN_HI;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_LONG_LEN_LO
			case STATE_LONG_LEN_HI:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					current_len |= (s.m_data << 8);

					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_LEN, current_len));

					state = STATE_LONG_ECC;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_LONG_LEN_HI

			//ECC value
			case STATE_LONG_ECC:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);

					uint8_t ecc_in[3] =
					{
						static_cast<uint8_t>((current_vc << 6) | current_type),
						static_cast<uint8_t>(current_len & 0xff),
						static_cast<uint8_t>(current_len >> 8)
					};
					//TODO: compute ecc value and check

					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ECC_OK, s.m_data));

					//If the packet had no content, jump right to the checksum
					expected_checksum = 0xffff;
					if(current_len == 0)
						state = STATE_LONG_CHECKSUM_LO;

					//Nope, we've got data
					else
					{
						state = STATE_LONG_DATA;
						bytes_read = 0;
					}
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_LONG_ECC

			//Read data for a long packet
			case STATE_LONG_DATA:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_DATA, s.m_data));

					expected_checksum = UpdateCRC(expected_checksum, s.m_data);

					pack->m_data.push_back(s.m_data);

					//At end of packet, read checksum
					bytes_read ++;
					if(bytes_read == current_len)
						state = STATE_LONG_CHECKSUM_LO;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_LONG_DATA

			//16-bit packet checksum
			case STATE_LONG_CHECKSUM_LO:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					tstart = off;
					current_checksum = s.m_data;
					state = STATE_LONG_CHECKSUM_HI;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_LONG_CHECKSUM_LO
			case STATE_LONG_CHECKSUM_HI:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					current_checksum |= (s.m_data << 8);

					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);

					//Verify checksum.
					//0x0000 means "checksum not calculated" so always passes
					if( (current_checksum == expected_checksum) || (current_checksum == 0x0000) )
						cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_CHECKSUM_OK, current_checksum));
					else
						cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_CHECKSUM_BAD, current_checksum));

					//Packet is over now
					state = STATE_HEADER;
					pack->m_headers["Length"] = to_string(pack->m_data.size());
					pack->m_len = (end * din->m_timescale) - pack->m_offset;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_LONG_CHECKSUM_HI

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Short packets

			case STATE_SHORT_DATA_0:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_DATA, s.m_data));

					pack->m_data.push_back(s.m_data);

					state = STATE_SHORT_DATA_1;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_SHORT_DATA_0

			case STATE_SHORT_DATA_1:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_DATA, s.m_data));

					pack->m_data.push_back(s.m_data);

					state = STATE_SHORT_ECC;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_SHORT_DATA_1

			//ECC value
			case STATE_SHORT_ECC:
				if(s.m_type == DPhyDataSymbol::TYPE_HS_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);

					uint8_t ecc_in[3] =
					{
						static_cast<uint8_t>((current_vc << 6) | current_type),
						static_cast<uint8_t>(current_len & 0xff),
						static_cast<uint8_t>(current_len >> 8)
					};
					//TODO: compute ecc value and check

					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ECC_OK, s.m_data));

					//Done
					state = STATE_HEADER;
					pack->m_len = (end * din->m_timescale) - pack->m_offset;
					pack->m_headers["Length"] = to_string(pack->m_data.size());
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_ERROR));
					state = STATE_DROP;
				}
				break;	//end STATE_LONG_ECC

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Bad packets

			//Wait for end of packet
			case STATE_DROP:
				if(s.m_type == DPhyDataSymbol::TYPE_EOT)
					state = STATE_IDLE;
				break;	//end STATE_DROP
		}
	}
}

Gdk::Color DSIPacketDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<DSIWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const DSISymbol& s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case DSISymbol::TYPE_VC:
			case DSISymbol::TYPE_IDENTIFIER:
				return m_standardColors[COLOR_ADDRESS];

			case DSISymbol::TYPE_LEN:
				return m_standardColors[COLOR_CONTROL];

			case DSISymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case DSISymbol::TYPE_ECC_OK:
			case DSISymbol::TYPE_CHECKSUM_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];

			case DSISymbol::TYPE_ECC_BAD:
			case DSISymbol::TYPE_CHECKSUM_BAD:
			case DSISymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string DSIPacketDecoder::GetText(int i)
{
	char tmp[128] = "";
	auto capture = dynamic_cast<DSIWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const DSISymbol& s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case DSISymbol::TYPE_VC:
				snprintf(tmp, sizeof(tmp), "VC%d", s.m_data >> 6);
				return tmp;

			case DSISymbol::TYPE_IDENTIFIER:
				switch(s.m_data & 0x3f)
				{
					case TYPE_VSYNC_START:	return "VSYNC Start";
					case TYPE_VSYNC_END:	return "VSYNC End";
					case TYPE_HSYNC_START:	return "HSYNC Start";
					case TYPE_HSYNC_END:	return "HSYNC End";
					case TYPE_EOTP:			return "End of TX";
					case TYPE_CM_OFF:		return "CM Off";
					case TYPE_CM_ON:		return "CM On";
					case TYPE_SHUT_DOWN:	return "Shut Down";
					case TYPE_TURN_ON:		return "Turn On";

					case TYPE_GENERIC_SHORT_WRITE_0PARAM:
					case TYPE_GENERIC_SHORT_WRITE_1PARAM:
					case TYPE_GENERIC_SHORT_WRITE_2PARAM:
					case TYPE_GENERIC_LONG_WRITE:
						return "Generic Write";

					case TYPE_GENERIC_READ_0PARAM:
					case TYPE_GENERIC_READ_1PARAM:
					case TYPE_GENERIC_READ_2PARAM:
						return "Generic Read";

					case TYPE_DCS_SHORT_WRITE_0PARAM:
					case TYPE_DCS_SHORT_WRITE_1PARAM:
					case TYPE_DCS_LONG_WRITE:
						return "DCS Write";

					case TYPE_DCS_READ:				return "DCS Read";
					case TYPE_SET_MAX_RETURN_SIZE:	return "Set Max Return Size";
					case TYPE_NULL:					return "Null";
					case TYPE_BLANKING:				return "Blank";
					case TYPE_PACKED_PIXEL_RGB565:	return "RGB565";
					case TYPE_PACKED_PIXEL_RGB666:	return "RGB666";
					case TYPE_LOOSE_PIXEL_RGB666:	return "RGB666 Loose";
					case TYPE_PACKED_PIXEL_RGB888:	return "RGB888";

					default:
						snprintf(tmp, sizeof(tmp), "RSVD %02x", s.m_data & 0x3f);
						return tmp;
				}
				break;

			case DSISymbol::TYPE_LEN:
				snprintf(tmp, sizeof(tmp), "Len %d", s.m_data);
				return tmp;

			case DSISymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				return tmp;

			case DSISymbol::TYPE_ECC_OK:
			case DSISymbol::TYPE_ECC_BAD:
				snprintf(tmp, sizeof(tmp), "ECC %02x", s.m_data);
				return tmp;

			case DSISymbol::TYPE_CHECKSUM_OK:
			case DSISymbol::TYPE_CHECKSUM_BAD:
				snprintf(tmp, sizeof(tmp), "Check %04x", s.m_data);
				return tmp;

			case DSISymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}

uint16_t DSIPacketDecoder::UpdateCRC(uint16_t crc, uint8_t data)
{
	//CRC16 with polynomial x^16 + x^12 + x^5 + x^0 (CRC-16-CCITT)
	uint16_t poly = 0x8408;
	for(int i=0; i<8; i++)
	{
		if( ((data >> i) ^ crc) & 1 )
			crc = (crc >> 1) ^ poly;
		else
			crc >>= 1;
	}

	return crc;
}

vector<string> DSIPacketDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("VC");
	ret.push_back("Type");
	ret.push_back("Length");
	return ret;
}

bool DSIPacketDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//If packets are from different VCs we can't merge them
	if(first->m_headers["VC"] != next->m_headers["VC"])
		return false;

	//Merge consecutive null packets
	if( (first->m_headers["Type"] == "Null") && (next->m_headers["Type"] == "Null") )
		return true;

	//Can merge EoTX or null after a video data packet
	if( (first->m_headers["Type"] == "RGB888") ||
		(first->m_headers["Type"] == "RGB666") ||
		(first->m_headers["Type"] == "RGB666 Loose") ||
		(first->m_headers["Type"] == "RGB565"))
	{
		if(	(next->m_headers["Type"] == "End of TX") ||
			(next->m_headers["Type"] == "Null") )
		{
			return true;
		}
	}

	//Merge H/VSYNC start and end.
	//Also allow merging null/EoTX after them
	if( (first->m_headers["Type"] == "HSYNC Start") )
	{
		if( (next->m_headers["Type"] == "HSYNC End") ||
			(next->m_headers["Type"] == "End of TX") ||
			(next->m_headers["Type"] == "Null") )
		{
			return true;
		}
	}
	if( (first->m_headers["Type"] == "VSYNC Start") )
	{
		if( (next->m_headers["Type"] == "VSYNC End") ||
			(next->m_headers["Type"] == "End of TX") ||
			(next->m_headers["Type"] == "Null") )
		{
			return true;
		}
	}

	return false;
}

Packet* DSIPacketDecoder::CreateMergedHeader(Packet* pack, size_t /*i*/)
{
	//Default copy
	Packet* ret = new Packet;
	ret->m_offset = pack->m_offset;
	ret->m_len = pack->m_len;
	ret->m_headers["VC"] = pack->m_headers["VC"];

	if( (pack->m_headers["Type"] == "RGB888") ||
		(pack->m_headers["Type"] == "RGB666") ||
		(pack->m_headers["Type"] == "RGB666 Loose") ||
		(pack->m_headers["Type"] == "RGB565"))
	{
		ret->m_headers["Type"] = pack->m_headers["Type"];
		ret->m_headers["Length"] = pack->m_headers["Length"];
		ret->m_data = pack->m_data;
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
	}

	if(pack->m_headers["Type"] == "VSYNC Start")
	{
		ret->m_headers["Type"] = "VSYNC";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
	}
	if(pack->m_headers["Type"] == "HSYNC Start")
	{
		ret->m_headers["Type"] = "HSYNC";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
	}

	else if(pack->m_headers["Type"] == "Null")
	{
		ret->m_headers["Type"] = "Padding";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DEFAULT];
	}

	return ret;
}
