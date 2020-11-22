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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of PCIeDataLinkDecoder
 */
#include "../scopehal/scopehal.h"
#include "../scopehal/Filter.h"
#include "PCIeGen2LogicalDecoder.h"
#include "PCIeDataLinkDecoder.h"


using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIeDataLinkDecoder::PCIeDataLinkDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	//Set up channels
	CreateInput("logical");
}

PCIeDataLinkDecoder::~PCIeDataLinkDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIeDataLinkDecoder::NeedsConfig()
{
	//No config needed
	return false;
}

bool PCIeDataLinkDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<PCIeLogicalWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string PCIeDataLinkDecoder::GetProtocolName()
{
	return "PCIe Data Link";
}

void PCIeDataLinkDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "PCIEDataLink(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIeDataLinkDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}
	auto data = dynamic_cast<PCIeLogicalWaveform*>(GetInputWaveform(0));

	//Create the capture
	auto cap = new PCIeDataLinkWaveform;
	cap->m_timescale = data->m_timescale;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startPicoseconds = data->m_startPicoseconds;

	enum
	{
		STATE_IDLE,

		STATE_DLLP_TYPE,
		STATE_DLLP_DATA1,
		STATE_DLLP_DATA2,
		STATE_DLLP_DATA3,
		STATE_DLLP_CRC1,
		STATE_DLLP_CRC2,
		STATE_END

	} state = STATE_IDLE;

	size_t len = data->m_samples.size();

	uint8_t dllp_type = 0;
	uint8_t dllp_data[3] = {0};

	for(size_t i=0; i<len; i++)
	{
		auto sym = data->m_samples[i];
		int64_t off = data->m_offsets[i];
		int64_t dur = data->m_durations[i];
		int64_t end = off + dur;

		size_t ilast = cap->m_samples.size() - 1;

		switch(state)
		{
			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Wait for a packet to start

			case STATE_IDLE:

				//Ignore everything but start of a packet;
				if(sym.m_type == PCIeLogicalSymbol::TYPE_START_DLLP)
					state = STATE_DLLP_TYPE;

				break;	//end STATE_IDLE

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// DLLP path

			case STATE_DLLP_TYPE:

				//If it's not data, we probably don't have scrambler sync yet. Abort.
				if(sym.m_type != PCIeLogicalSymbol::TYPE_PAYLOAD_DATA)
					state = STATE_IDLE;
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeDataLinkSymbol(PCIeDataLinkSymbol::TYPE_DLLP_TYPE, sym.m_data));

					dllp_type = sym.m_data;
					state = STATE_DLLP_DATA1;
				}
				break;	//end STATE_DLLP_TYPE

			case STATE_DLLP_DATA1:

				if(sym.m_type != PCIeLogicalSymbol::TYPE_PAYLOAD_DATA)
					state = STATE_IDLE;

				else
				{
					//First byte of data goes in the first DLLP data
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeDataLinkSymbol(PCIeDataLinkSymbol::TYPE_DLLP_DATA1, sym.m_data));

					dllp_data[0] = sym.m_data;

					state = STATE_DLLP_DATA2;
				}

				break;	//end STATE_DLLP_DATA1

			case STATE_DLLP_DATA2:

				if(sym.m_type != PCIeLogicalSymbol::TYPE_PAYLOAD_DATA)
				{
					cap->m_samples[ilast].m_type = PCIeDataLinkSymbol::TYPE_ERROR;
					state = STATE_IDLE;
				}

				else
				{
					dllp_data[1] = sym.m_data;

					switch(dllp_type)
					{
						//If this is an ACK/NAK DLLP, save the sequence number.
						//Throw away the first byte of data (reserved/ignored)
						case PCIeDataLinkSymbol::DLLP_TYPE_ACK:
						case PCIeDataLinkSymbol::DLLP_TYPE_NAK:
							cap->m_samples[ilast].m_data = sym.m_data;
							cap->m_durations[ilast] = end - cap->m_offsets[ilast];
							break;

						//Default to making a new symbol
						default:
							cap->m_offsets.push_back(off);
							cap->m_durations.push_back(dur);
							cap->m_samples.push_back(PCIeDataLinkSymbol(
								PCIeDataLinkSymbol::TYPE_DLLP_DATA2, sym.m_data));
							break;
					}

					state = STATE_DLLP_DATA3;
				}

				break;	//end STATE_DLLP_DATA2

			case STATE_DLLP_DATA3:

				if(sym.m_type != PCIeLogicalSymbol::TYPE_PAYLOAD_DATA)
				{
					cap->m_samples[ilast].m_type = PCIeDataLinkSymbol::TYPE_ERROR;
					state = STATE_IDLE;
				}

				else
				{
					dllp_data[2] = sym.m_data;

					switch(dllp_type)
					{
						//If this is an ACK/NAK DLLP, extend the existing sequence number.
						case PCIeDataLinkSymbol::DLLP_TYPE_ACK:
						case PCIeDataLinkSymbol::DLLP_TYPE_NAK:
							cap->m_samples[ilast].m_data = (cap->m_samples[ilast].m_data << 8) | sym.m_data;
							cap->m_durations[ilast] = end - cap->m_offsets[ilast];
							break;

						//Default to making a new symbol
						default:
							cap->m_offsets.push_back(off);
							cap->m_durations.push_back(dur);
							cap->m_samples.push_back(PCIeDataLinkSymbol(
								PCIeDataLinkSymbol::TYPE_DLLP_DATA2, sym.m_data));
							break;
					}

					state = STATE_DLLP_CRC1;
				}

				break;	//end STATE_DLLP_DATA3

			case STATE_DLLP_CRC1:

				if(sym.m_type != PCIeLogicalSymbol::TYPE_PAYLOAD_DATA)
				{
					cap->m_samples[ilast].m_type = PCIeDataLinkSymbol::TYPE_ERROR;
					state = STATE_IDLE;
				}

				else
				{
					//Create the CRC symbol
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeDataLinkSymbol(
						PCIeDataLinkSymbol::TYPE_DLLP_CRC_OK, sym.m_data));

					state = STATE_DLLP_CRC2;
				}

				break;	//end STATE_DLLP_CRC1

			case STATE_DLLP_CRC2:

				if(sym.m_type != PCIeLogicalSymbol::TYPE_PAYLOAD_DATA)
				{
					cap->m_samples[ilast].m_type = PCIeDataLinkSymbol::TYPE_ERROR;
					state = STATE_IDLE;
				}

				else
				{
					//Update the CRC
					uint16_t expected_crc = (cap->m_samples[ilast].m_data << 8) | sym.m_data;
					cap->m_samples[ilast].m_data = expected_crc;
					cap->m_durations[ilast] = end - cap->m_offsets[ilast];

					//Verify it
					uint16_t actual_crc = CalculateDllpCRC(dllp_type, dllp_data);
					LogDebug("DLLP: expected %04x, calculated %04x\n", expected_crc, actual_crc);
					if(expected_crc != actual_crc)
						cap->m_samples[ilast].m_type = PCIeDataLinkSymbol::TYPE_DLLP_CRC_BAD;

					state = STATE_END;
				}

				break;	//end STATE_DLLP_CRC2

			case STATE_END:
				if(sym.m_type != PCIeLogicalSymbol::TYPE_END)
					cap->m_samples[ilast].m_type = PCIeDataLinkSymbol::TYPE_ERROR;
				state = STATE_IDLE;
				break;	//end STATE_END
		}
	}

	SetData(cap, 0);
}

/**
	@brief PCIe DLLP CRC

	See PCIe Base Spec v2.0, figure 3-11
 */
uint16_t PCIeDataLinkDecoder::CalculateDllpCRC(uint8_t type, uint8_t* data)
{
	uint8_t crc_in[4] = { type, data[0], data[1], data[2] };

	uint16_t poly = 0x100b;
	uint16_t crc = 0xffff;

	for(int n=0; n<4; n++)
	{
		uint8_t d = crc_in[n];
		for(int i=0; i<8; i++)
		{
			bool b = ( (crc >> 15) ^ (d >> i) ) & 1;

			crc <<= 1;

			if(b)
				crc ^= poly;
		}
	}

	//Bitswap CRC
	uint16_t crc_bitswap = 0;
	for(int i=0; i<8; i++)
	{
		if( (crc >> i) & 1)
			crc_bitswap |= 1 << (7-i);
		if( (crc >> (i+8)) & 1)
			crc_bitswap |= 1 << (15-i);
	}

	return ~crc_bitswap;
}

Gdk::Color PCIeDataLinkDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<PCIeDataLinkWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];

		switch(s.m_type)
		{
			case PCIeDataLinkSymbol::TYPE_DLLP_TYPE:
				return m_standardColors[COLOR_ADDRESS];

			case PCIeDataLinkSymbol::TYPE_DLLP_DATA1:
			case PCIeDataLinkSymbol::TYPE_DLLP_DATA2:
				return m_standardColors[COLOR_DATA];

			case PCIeDataLinkSymbol::TYPE_DLLP_CRC_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];

			case PCIeDataLinkSymbol::TYPE_DLLP_CRC_BAD:
				return m_standardColors[COLOR_CHECKSUM_BAD];

			case PCIeDataLinkSymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string PCIeDataLinkDecoder::GetText(int i)
{
	char tmp[32];

	auto capture = dynamic_cast<PCIeDataLinkWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];

		switch(s.m_type)
		{
			case PCIeDataLinkSymbol::TYPE_DLLP_TYPE:
				switch(s.m_data)
				{
					case PCIeDataLinkSymbol::DLLP_TYPE_ACK:							return "ACK";
					case PCIeDataLinkSymbol::DLLP_TYPE_NAK:							return "NAK";
					case PCIeDataLinkSymbol::DLLP_TYPE_PM_ENTER_L1:					return "PM_Enter_L1";
					case PCIeDataLinkSymbol::DLLP_TYPE_PM_ENTER_L23:				return "PM_Enter_L23";
					case PCIeDataLinkSymbol::DLLP_TYPE_PM_ACTIVE_STATE_REQUEST_L1:	return "PM_Active_State_Request_L1";
					case PCIeDataLinkSymbol::DLLP_TYPE_PM_REQUEST_ACK:				return "PM_Request_Ack";
					case PCIeDataLinkSymbol::DLLP_TYPE_VENDOR_SPECIFIC:				return "Vendor Specific";

					default:
						break;
				}

				switch(s.m_data & 0xf8)
				{
					case 0x40:
						return string("InitFC1-P VC") + to_string(s.m_data & 7);
					case 0x50:
						return string("InitFC1-NP VC") + to_string(s.m_data & 7);
					case 0x60:
						return string("InitFC1-Cpl VC") + to_string(s.m_data & 7);
					case 0xc0:
						return string("InitFC2-P VC") + to_string(s.m_data & 7);
					case 0xd0:
						return string("InitFC2-NP VC") + to_string(s.m_data & 7);
					case 0xe0:
						return string("InitFC2-Cpl VC") + to_string(s.m_data & 7);
					case 0x80:
						return string("UpdateFC-P VC") + to_string(s.m_data & 7);
					case 0x90:
						return string("UpdateFC-NP VC") + to_string(s.m_data & 7);
					case 0xa0:
						return string("UpdateFC-Cpl VC") + to_string(s.m_data & 7);
				}

				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				return string("Reserved ") + tmp;

			case PCIeDataLinkSymbol::TYPE_DLLP_DATA1:

				//TODO
				snprintf(tmp, sizeof(tmp), "D1 %02x", s.m_data);
				return tmp;

				break;

			case PCIeDataLinkSymbol::TYPE_DLLP_DATA2:

				//TODO
				snprintf(tmp, sizeof(tmp), "D2 %02x", s.m_data);
				return tmp;

				break;

			case PCIeDataLinkSymbol::TYPE_DLLP_DATA3:

				//TODO
				snprintf(tmp, sizeof(tmp), "D3 %02x", s.m_data);
				return tmp;

				break;

			case PCIeDataLinkSymbol::TYPE_DLLP_CRC_OK:
			case PCIeDataLinkSymbol::TYPE_DLLP_CRC_BAD:
				snprintf(tmp, sizeof(tmp), "CRC: %04x", s.m_data);
				return tmp;

			case PCIeDataLinkSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}
