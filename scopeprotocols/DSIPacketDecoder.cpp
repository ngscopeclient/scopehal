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

#include "../scopehal/scopehal.h"
#include "DSIPacketDecoder.h"
#include "DPhyDataDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DSIPacketDecoder::DSIPacketDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
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

double DSIPacketDecoder::GetVoltageRange()
{
	return m_inputs[0].m_channel->GetVoltageRange();
}

string DSIPacketDecoder::GetProtocolName()
{
	return "MIPI DSI Packet";
}

bool DSIPacketDecoder::IsOverlay()
{
	return true;
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

	//Loop over the DSI events and process stuff
	auto cap = new DSIWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
	size_t current_len = 0;
	size_t bytes_read = 0;
	uint8_t current_type = 0;
	uint8_t current_vc = 0;
	uint16_t expected_checksum = 0;
	uint16_t current_checksum = 0;
	int64_t tstart = 0;
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

					switch(s.m_data & 0x3f)
					{
						//Type codes for long packets
						case 0x09:
						case 0x19:
						case 0x29:
						case 0x39:
						case 0x0e:
						case 0x1e:
						case 0x2e:
						case 0x3e:
							state = STATE_LONG_LEN_LO;
							cap->m_offsets.push_back(off + halfdur);
							cap->m_durations.push_back(dur - halfdur);
							cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_IDENTIFIER, current_type));
							break;

						//Type codes for short packets
						case 0x01:
						case 0x11:
						case 0x21:
						case 0x31:
						case 0x08:
						case 0x02:
						case 0x12:
						case 0x22:
						case 0x32:
						case 0x03:
						case 0x13:
						case 0x23:
						case 0x04:
						case 0x14:
						case 0x24:
						case 0x05:
						case 0x15:
						case 0x06:
						case 0x37:
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
					if( (current_checksum == BitReverse(expected_checksum)) || (current_checksum == 0x0000) )
						cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_CHECKSUM_OK, current_checksum));
					else
						cap->m_samples.push_back(DSISymbol(DSISymbol::TYPE_CHECKSUM_BAD, current_checksum));

					//Packet is over now
					state = STATE_HEADER;
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

	SetData(cap, 0);
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
					case 0x01:	return "VSYNC Start";
					case 0x11:	return "VSYNC End";
					case 0x21:	return "HSYNC Start";
					case 0x31:	return "HSYNC End";
					case 0x08:	return "End of TX";
					case 0x02:	return "CM Off";
					case 0x12:	return "CM On";
					case 0x22:	return "Shut Down";
					case 0x32:	return "Turn On";
					case 0x03:
					case 0x13:
					case 0x23:
					case 0x29:
						return "Generic Write";
					case 0x04:
					case 0x14:
					case 0x24:
						return "Generic Read";
					case 0x05:
					case 0x15:
					case 0x39:
						return "DCS Write";
					case 0x06:	return "DCS Read";
					case 0x37:	return "Set Max Return Size";
					case 0x09:	return "Null";
					case 0x19:	return "Blank";
					case 0x0e:	return "RGB565";
					case 0x1e:	return "RGB666";
					case 0x2e:	return "RGB666 Loose";
					case 0x3e:	return "RGB888";

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
				snprintf(tmp, sizeof(tmp), "Check %02x", s.m_data);
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
	uint16_t poly = 0x1021;
	for(int i=0; i<8; i++)
	{
		bool b = (data >> (7-i)) & 1;
		bool c = (crc & 0x8000);
		crc <<= 1;
		if(b ^ c)
			crc ^= poly;
	}

	return crc;
}

/**
	@brief MIPI seems to send the CRC bit-reversed from the normal order. Flip it
 */
uint16_t DSIPacketDecoder::BitReverse(uint16_t crc)
{
	uint16_t crc_flipped = 0;
	for(int i=0; i<16; i++)
	{
		if(crc & (1 << i))
			crc_flipped |= (1 << (15-i));
	}

	return crc_flipped;
}
