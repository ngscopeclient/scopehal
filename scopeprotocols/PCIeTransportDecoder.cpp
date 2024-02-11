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
	@brief Implementation of PCIeTransportDecoder
 */
#include "../scopehal/scopehal.h"
#include "PCIeDataLinkDecoder.h"
#include "PCIeTransportDecoder.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIeTransportDecoder::PCIeTransportDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	//Set up channels
	CreateInput("link");
}

PCIeTransportDecoder::~PCIeTransportDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIeTransportDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<PCIeDataLinkWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string PCIeTransportDecoder::GetProtocolName()
{
	return "PCIe Transport";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIeTransportDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}
	auto data = dynamic_cast<PCIeDataLinkWaveform*>(GetInputWaveform(0));
	data->PrepareForCpuAccess();

	//Create the capture
	auto cap = new PCIeTransportWaveform;
	cap->m_timescale = data->m_timescale;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startFemtoseconds = data->m_startFemtoseconds;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	enum
	{
		STATE_IDLE,
		STATE_HEADER_0,
		STATE_HEADER_1,
		STATE_HEADER_2,
		STATE_HEADER_3,

		STATE_MEMORY_0,
		STATE_MEMORY_1,
		STATE_MEMORY_3,
		STATE_BYTE_ENABLES,
		STATE_ADDRESS_0,
		STATE_ADDRESS_1,

		STATE_COMPLETION_0,
		STATE_COMPLETION_1,
		STATE_COMPLETION_2,
		STATE_COMPLETION_3,
		STATE_COMPLETION_4,
		STATE_COMPLETION_5,
		STATE_COMPLETION_6,
		STATE_COMPLETION_7,

		STATE_DATA,

	} state = STATE_IDLE;

	size_t len = data->m_samples.size();

	Packet* pack = NULL;

	enum TLPFormat
	{
		TLP_FORMAT_3W_NODATA	= 0,
		TLP_FORMAT_4W_NODATA	= 1,
		TLP_FORMAT_3W_DATA		= 2,
		TLP_FORMAT_4W_DATA 		= 3
	} tlp_format = TLP_FORMAT_3W_NODATA;

	bool format_4word 			= false;
	bool has_data 				= false;
	int traffic_class			= 0;
	bool digest_present			= false;
	bool poisoned 				= false;
	bool relaxed_ordering		= false;
	bool no_snoop				= false;
	size_t packet_len			= 0;
	uint16_t requester_id		= 0;
	uint16_t completer_id		= 0;
	uint8_t tag					= 0;
	uint64_t mem_addr			= 0;
	size_t nbyte				= 0;
	uint8_t completion_status	= 0;
	uint16_t byte_count			= 0;

	char tmp[32];

	PCIeTransportSymbol::TlpType type = PCIeTransportSymbol::TYPE_INVALID;

	//Address types (PCIe 2.0 base spec table 2-5)
	/*
	enum AddressType
	{
		ADDRESS_TYPE_DEFAULT				= 0,
		ADDRESS_TYPE_TRANSLATION_REQUEST	= 1,
		ADDRESS_TYPE_TRANSLATED				= 2
	} address_type;
	*/

	for(size_t i=0; i<len; i++)
	{
		auto sym = data->m_samples[i];
		int64_t off = data->m_offsets[i];
		int64_t dur = data->m_durations[i];
		int64_t halfdur = dur/2;
		int64_t end = off + dur;
		size_t ilast = cap->m_samples.size() - 1;

		switch(state)
		{
			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Wait for a packet to start

			case STATE_IDLE:

				//Ignore everything but start of a TLP
				if(sym.m_type == PCIeDataLinkSymbol::TYPE_TLP_SEQUENCE)
				{
					//Create the packet
					pack = new Packet;
					m_packets.push_back(pack);
					pack->m_offset = off * cap->m_timescale;
					pack->m_len = 0;
					pack->m_headers["Seq"] = to_string(sym.m_data);

					state = STATE_HEADER_0;
				}

				break;	//end STATE_IDLE

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Common TLP headers (PCIe 2.0 base spec figure 2-4, section 2.2.1)

			case STATE_HEADER_0:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}

				else
				{
					//Extract format (PCIe 2.0 base spec table 2-2)
					tlp_format = static_cast<TLPFormat>(sym.m_data >> 5);
					format_4word = (tlp_format == TLP_FORMAT_4W_NODATA) || (tlp_format == TLP_FORMAT_4W_DATA);
					has_data = (tlp_format == TLP_FORMAT_3W_DATA) || (tlp_format == TLP_FORMAT_4W_DATA);

					//Type is a bit complicated, because it depends on both type and format fields
					//PCIe 2.0 base spec table 2-3
					type = PCIeTransportSymbol::TYPE_INVALID;
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					switch(sym.m_data & 0x1f)
					{
						case 0:
							if(!has_data)
							{
								type = PCIeTransportSymbol::TYPE_MEM_RD;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							else
							{
								type = PCIeTransportSymbol::TYPE_MEM_WR;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							}
							break;

						case 1:
							if(!has_data)
							{
								type = PCIeTransportSymbol::TYPE_MEM_RD_LK;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							break;

						case 2:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_IO_RD;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_IO_WR;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							break;

						//Type 3 appears unallocated, not mentioned in the spec

						case 4:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_RD_0;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_WR_0;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							break;

						case 5:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_RD_1;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_WR_1;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							break;

						//Type 0x1b is deprecated

						case 10:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION_DATA;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							break;

						case 11:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_ERROR;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_DATA;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							break;
					}

					//TODO: support Msg / MSgD

					//Add the type symbol
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_TLP_TYPE, type));

					pack->m_headers["Type"] = cap->GetText(cap->m_samples.size()-1);

					state = STATE_HEADER_1;
				}

				break;	//end STATE_HEADER_0

			//This one is easy. Traffic class plus a bunch of reserved fields
			case STATE_HEADER_1:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}

				else
				{
					traffic_class = (sym.m_data >> 4) & 7;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_TRAFFIC_CLASS, traffic_class));

					pack->m_headers["TC"] = to_string(traffic_class);

					state = STATE_HEADER_2;
				}
				break;	//end STATE_HEADER_1

			//7 TLP digest present
			//6	poisoned (corrupted, discard)
			//5:4 attributes
			//3:2 = address type
			//1:0 = high 2 bits of length
			case STATE_HEADER_2:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}

				else
				{
					digest_present = (sym.m_data & PCIeTransportSymbol::FLAG_DIGEST_PRESENT) != 0;
					poisoned = (sym.m_data & PCIeTransportSymbol::FLAG_POISONED) != 0;
					relaxed_ordering = (sym.m_data & PCIeTransportSymbol::FLAG_RELAXED_ORDERING) != 0;
					no_snoop = (sym.m_data & PCIeTransportSymbol::FLAG_NO_SNOOP) != 0;
					//address_type = static_cast<AddressType>( (sym.m_data >> 2) & 3);
					packet_len = (sym.m_data & 3) << 8;

					if(poisoned)
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_FLAGS, sym.m_data));

					string flags;
					if(digest_present)
						flags += "TD ";
					if(poisoned)
						flags += "EP ";
					if(relaxed_ordering)
						flags += "RLX ";
					if(no_snoop)
						flags += "NS";
					pack->m_headers["Flags"] = flags;

					state = STATE_HEADER_3;
				}

				break;	//end STATE_HEADER_2

			//Low byte of length
			case STATE_HEADER_3:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}

				else
				{
					//Length is 32-bit words, plus special case 0 is 1024 words (see PCIe 2.0 base spec table 2-4)
					packet_len |= sym.m_data;
					if(packet_len == 0)
						packet_len = 1024;

					//If the message has no payload, force length to zero for payload size counting
					//(according to spec, actual value is reserved)
					if(!has_data)
						packet_len = 0;
					else
						pack->m_headers["Length"] = to_string(packet_len * 4);

					//Add the length symbol
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_LENGTH, packet_len));

					//What happens next depends on the TLP format

					switch(type)
					{
						//Memory, IO, or config access?
						case PCIeTransportSymbol::TYPE_MEM_RD:
						case PCIeTransportSymbol::TYPE_MEM_RD_LK:
						case PCIeTransportSymbol::TYPE_MEM_WR:
						case PCIeTransportSymbol::TYPE_IO_RD:
						case PCIeTransportSymbol::TYPE_IO_WR:
						case PCIeTransportSymbol::TYPE_CFG_RD_0:
						case PCIeTransportSymbol::TYPE_CFG_WR_0:
						case PCIeTransportSymbol::TYPE_CFG_RD_1:
						case PCIeTransportSymbol::TYPE_CFG_WR_1:
							state = STATE_MEMORY_0;
							break;

						case PCIeTransportSymbol::TYPE_COMPLETION:
						case PCIeTransportSymbol::TYPE_COMPLETION_DATA:
						case PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_ERROR:
						case PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_DATA:
							state = STATE_COMPLETION_0;
							break;

						//Give up on anything else
						default:
							state = STATE_IDLE;
					}
				}
				break;	//end STATE_HEADER_3

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Memory, I/O, and Configuration requests (PCIe 2.0 base spec section 2.2.7)

			case STATE_MEMORY_0:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					requester_id = (sym.m_data << 8);

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_REQUESTER_ID, requester_id));

					state = STATE_MEMORY_1;
				}
				break; //end STATE_MEMORY_0

			case STATE_MEMORY_1:
				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					requester_id |= sym.m_data;

					cap->m_durations[ilast] = end - cap->m_offsets[ilast];
					cap->m_samples[ilast].m_data = requester_id;

					pack->m_headers["Requester"] = FormatID(requester_id);

					state = STATE_MEMORY_3;
				}
				break;	//end STATE_MEMORY_1

			case STATE_MEMORY_3:
				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					tag = sym.m_data;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_TAG, tag));

					pack->m_headers["Tag"] = to_string(tag);

					state = STATE_BYTE_ENABLES;
				}
				break;	//end STATE_MEMORY_3

			case STATE_BYTE_ENABLES:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(halfdur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_LAST_BYTE_ENABLE,
						sym.m_data >> 4));

					cap->m_offsets.push_back(off + halfdur);
					cap->m_durations.push_back(dur - halfdur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_FIRST_BYTE_ENABLE,
						sym.m_data & 0xf));

					string first = "";
					string last = "";
					for(int j=3; j>=0; j--)
					{
						if(sym.m_data & (1 << j) )
							first += to_string(j);
						if(sym.m_data & (0x10 << j) )
							last += to_string(j);
					}

					pack->m_headers["First"] = first;
					pack->m_headers["Last"] = first;

					state = STATE_ADDRESS_0;
					nbyte = 0;
					mem_addr = 0;
				}

				break;	//end STATE_BYTE_ENABLES

			//32-bit address, or high half of a 64 bit
			case STATE_ADDRESS_0:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					mem_addr = (mem_addr << 8) | sym.m_data;

					//Create the initial symbol
					if(nbyte == 0)
					{
						cap->m_offsets.push_back(off);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ADDRESS_X32, 0));
					}

					nbyte ++;

					if(nbyte == 4)
					{
						cap->m_durations[ilast] = end - cap->m_offsets[ilast];
						cap->m_samples[ilast].m_data = mem_addr;

						if(format_4word)
							state = STATE_ADDRESS_1;
						else
						{
							snprintf(tmp, sizeof(tmp), "%08" PRIx64, mem_addr);
							pack->m_headers["Addr"] = tmp;

							nbyte = 0;
							state = STATE_DATA;
						}
					}
				}

				break;	//end STATE_ADDRESS_0

			//Low half of a 64-bit address
			case STATE_ADDRESS_1:
				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					mem_addr = (mem_addr << 8) | sym.m_data;
					nbyte ++;

					if(nbyte == 8)
					{
						cap->m_durations[ilast] = end - cap->m_offsets[ilast];
						cap->m_samples[ilast].m_data = mem_addr;
						cap->m_samples[ilast].m_type = PCIeTransportSymbol::TYPE_ADDRESS_X64;

						snprintf(tmp, sizeof(tmp), "%016" PRIx64, mem_addr);
						pack->m_headers["Addr"] = tmp;

						nbyte = 0;
						state = STATE_DATA;
					}
				}
				break;

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Completion packets

			//Completer ID
			case STATE_COMPLETION_0:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					completer_id = sym.m_data << 8;

					//Create the initial symbol
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_COMPLETER_ID, 0));

					state = STATE_COMPLETION_1;
				}

				break; //end STATE_COMPLETION_0

			case STATE_COMPLETION_1:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					completer_id |= sym.m_data;

					//Save the final ID
					cap->m_durations[ilast] = end - cap->m_offsets[ilast];
					cap->m_samples[ilast].m_data = completer_id;

					pack->m_headers["Completer"] = FormatID(completer_id);

					state = STATE_COMPLETION_2;
				}

				break; //end STATE_COMPLETION_1

			//Status and high half of byte count
			case STATE_COMPLETION_2:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					completion_status = (sym.m_data >> 5);

					//Create the initial symbol
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_COMPLETION_STATUS,
						completion_status));

					switch(completion_status)
					{
						case 0:
							pack->m_headers["Status"] = "SC";
							break;

						case 1:
							pack->m_headers["Status"] = "UR";
							break;

						case 2:
							pack->m_headers["Status"] = "CRS";
							break;

						case 4:
							pack->m_headers["Status"] = "CA";
							break;

						default:
							pack->m_headers["Status"] = "Invalid";
							break;
					}

					byte_count = (sym.m_data & 0xf) << 8;

					state = STATE_COMPLETION_3;
				}

				break;	//end STATE_COMPLETION_2

			case STATE_COMPLETION_3:
				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					byte_count |= sym.m_data;

					//Save the final ID
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_BYTE_COUNT, byte_count));

					pack->m_headers["Count"] = to_string(byte_count);

					state = STATE_COMPLETION_4;
				}
				break;	//end STATE_COMPLETION_3

			case STATE_COMPLETION_4:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					requester_id = (sym.m_data << 8);

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_REQUESTER_ID, requester_id));

					state = STATE_COMPLETION_5;
				}

				break; //end STATE_COMPLETION_4

			case STATE_COMPLETION_5:
				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					requester_id |= sym.m_data;

					cap->m_durations[ilast] = end - cap->m_offsets[ilast];
					cap->m_samples[ilast].m_data = requester_id;

					pack->m_headers["Requester"] = FormatID(requester_id);

					state = STATE_COMPLETION_6;
				}
				break;	//end STATE_COMPLETION_5

			case STATE_COMPLETION_6:
				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					tag = sym.m_data;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_TAG, tag));

					pack->m_headers["Tag"] = to_string(tag);

					state = STATE_COMPLETION_7;
				}
				break;	//end STATE_COMPLETION_6

			case STATE_COMPLETION_7:
				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}
				else
				{
					state = STATE_DATA;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ADDRESS_X32,
						sym.m_data & 0x7f));

					snprintf(tmp, sizeof(tmp), "   ...%02x", sym.m_data & 0x7f);
					pack->m_headers["Addr"] = tmp;
				}
				break;	//end STATE_COMPLETION_7

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// TLP payload data

			case STATE_DATA:

				//Update packet length
				pack->m_len = (end * cap->m_timescale) - pack->m_offset;

				if(sym.m_type == PCIeDataLinkSymbol::TYPE_TLP_CRC_OK)
				{
					//TODO: verify length wasn't truncated
					//TODO: verify TLP end to end CRC if present
					state = STATE_IDLE;
				}

				else if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					state = STATE_IDLE;
				}

				//TODO: complain if we have more data than we should have
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_DATA, sym.m_data));

					pack->m_data.push_back(sym.m_data);
				}

				break;
		}
	}

	cap->MarkModifiedFromCpu();
}

std::string PCIeTransportWaveform::GetColor(size_t i)
{
	auto s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeTransportSymbol::TYPE_TLP_TYPE:
		case PCIeTransportSymbol::TYPE_TRAFFIC_CLASS:
		case PCIeTransportSymbol::TYPE_LENGTH:
		case PCIeTransportSymbol::TYPE_BYTE_COUNT:
		case PCIeTransportSymbol::TYPE_TAG:
		case PCIeTransportSymbol::TYPE_FIRST_BYTE_ENABLE:
		case PCIeTransportSymbol::TYPE_LAST_BYTE_ENABLE:
		case PCIeTransportSymbol::TYPE_COMPLETION_STATUS:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case PCIeTransportSymbol::TYPE_FLAGS:
			if(s.m_data & PCIeTransportSymbol::FLAG_POISONED)
				return StandardColors::colors[StandardColors::COLOR_ERROR];
			else
				return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case PCIeTransportSymbol::TYPE_REQUESTER_ID:
		case PCIeTransportSymbol::TYPE_COMPLETER_ID:
		case PCIeTransportSymbol::TYPE_ADDRESS_X32:
		case PCIeTransportSymbol::TYPE_ADDRESS_X64:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case PCIeTransportSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case PCIeTransportSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string PCIeTransportWaveform::GetText(size_t i)
{
	char tmp[64];

	auto s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeTransportSymbol::TYPE_TLP_TYPE:
			switch(s.m_data)
			{
				case PCIeTransportSymbol::TYPE_MEM_RD:			return "Mem read";
				case PCIeTransportSymbol::TYPE_MEM_RD_LK:		return "Mem read locked";
				case PCIeTransportSymbol::TYPE_MEM_WR:			return "Mem write";
				case PCIeTransportSymbol::TYPE_IO_RD:			return "IO read";
				case PCIeTransportSymbol::TYPE_IO_WR:			return "IO write";
				case PCIeTransportSymbol::TYPE_CFG_RD_0:		return "Cfg read 0";
				case PCIeTransportSymbol::TYPE_CFG_WR_0:		return "Cfg write 0";
				case PCIeTransportSymbol::TYPE_CFG_RD_1:		return "Cfg read 1";
				case PCIeTransportSymbol::TYPE_CFG_WR_1:		return "Cfg write 1";

				case PCIeTransportSymbol::TYPE_MSG:
				case PCIeTransportSymbol::TYPE_MSG_DATA:
					return "Message";

				case PCIeTransportSymbol::TYPE_COMPLETION:
				case PCIeTransportSymbol::TYPE_COMPLETION_DATA:
					return "Completion";

				case PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_ERROR:
				case PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_DATA:
					return "Completion locked";

				case PCIeTransportSymbol::TYPE_INVALID:
				default:
					return "ERROR";
			}

		case PCIeTransportSymbol::TYPE_TRAFFIC_CLASS:
			return string("TC: ") + to_string(s.m_data);

		case PCIeTransportSymbol::TYPE_REQUESTER_ID:
			return string("Requester: ") + PCIeTransportDecoder::FormatID(s.m_data);

		case PCIeTransportSymbol::TYPE_COMPLETER_ID:
			return string("Completer: ") + PCIeTransportDecoder::FormatID(s.m_data);

		case PCIeTransportSymbol::TYPE_ADDRESS_X32:
			snprintf(tmp, sizeof(tmp), "Address: %08" PRIx64, s.m_data);
			return tmp;

		case PCIeTransportSymbol::TYPE_ADDRESS_X64:
			snprintf(tmp, sizeof(tmp), "Address: %016" PRIx64, s.m_data);
			return tmp;

		case PCIeTransportSymbol::TYPE_TAG:
			snprintf(tmp, sizeof(tmp), "Tag: %02" PRIx64, s.m_data);
			return tmp;

		case PCIeTransportSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02" PRIx64, s.m_data);
			return tmp;

		case PCIeTransportSymbol::TYPE_FLAGS:
			{
				string ret;
				if(s.m_data & PCIeTransportSymbol::FLAG_DIGEST_PRESENT)
					ret += "DP ";
				if(s.m_data & PCIeTransportSymbol::FLAG_POISONED)
					ret += "Poison ";
				if(s.m_data & PCIeTransportSymbol::FLAG_RELAXED_ORDERING)
					ret += "Relaxed ";
				if(s.m_data & PCIeTransportSymbol::FLAG_NO_SNOOP)
					ret += "No snoop ";
				if(ret == "")
					ret = "No flags";
				return ret;
			}

		case PCIeTransportSymbol::TYPE_LENGTH:
			return string("Len: ") + to_string(s.m_data * 4);

		case PCIeTransportSymbol::TYPE_BYTE_COUNT:
			return string("Bytes: ") + to_string(s.m_data);

		case PCIeTransportSymbol::TYPE_LAST_BYTE_ENABLE:
			{
				if(s.m_data == 0)
					return "Last: none";

				string ret = "Last: bytes ";
				for(int j=3; j>=0; j--)
				{
					if(s.m_data & (1 << j) )
						ret += to_string(j);
				}
				return ret;
			};

		case PCIeTransportSymbol::TYPE_FIRST_BYTE_ENABLE:
			{
				string ret = "First: bytes ";
				for(int j=3; j>=0; j--)
				{
					if(s.m_data & (1 << j) )
						ret += to_string(j);
				}
				return ret;
			};

		case PCIeTransportSymbol::TYPE_COMPLETION_STATUS:
			switch(s.m_data)
			{
				case 0:
					return "Status: SC";
				case 1:
					return "Status: UR";
				case 2:
					return "Status: CRS";
				case 4:
					return "Status: CA";
				default:
					return "Status: Invalid";
			}
			break;

		case PCIeTransportSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}

vector<string> PCIeTransportDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Seq");
	ret.push_back("TC");
	ret.push_back("Type");
	ret.push_back("Addr");
	ret.push_back("Flags");
	ret.push_back("Requester");
	ret.push_back("Completer");
	ret.push_back("Tag");
	ret.push_back("First");
	ret.push_back("Last");
	ret.push_back("Status");
	ret.push_back("Count");

	ret.push_back("Length");
	return ret;
}

string PCIeTransportDecoder::FormatID(uint16_t id)
{
	char tmp[16];
	snprintf(tmp, sizeof(tmp), "%02x:%x.%d",
		id >> 8,
		(id >> 3) & 0x1f,
		id & 0x7);
	return tmp;
}
