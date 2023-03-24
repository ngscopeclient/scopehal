/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "DPAuxChannelDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DPAuxChannelDecoder::DPAuxChannelDecoder(const string& color)
	: PacketDecoder(color, CAT_SERIAL)
{
	CreateInput("aux");
}

DPAuxChannelDecoder::~DPAuxChannelDecoder()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DPAuxChannelDecoder::GetProtocolName()
{
	return "DisplayPort - Aux Channel";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

bool DPAuxChannelDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

vector<string> DPAuxChannelDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Address");
	ret.push_back("Length");
	ret.push_back("Info");
	return ret;
}

void DPAuxChannelDecoder::Refresh()
{
	ClearPackets();

	//Get the input data
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	size_t len = din->size();
	din->PrepareForCpuAccess();

	//Copy our time scales from the input
	auto cap = new DPAuxWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->PrepareForCpuAccess();

	const int64_t ui_width 		= 1e9;	//4.5e8
	const int64_t ui_halfwidth 	= 5e8;
	const int64_t jitter_tol 	= 2e8;

	const int64_t eye_start = ui_halfwidth - jitter_tol;
	const int64_t eye_end = ui_halfwidth + jitter_tol;

	const int64_t sync_width_max = 3e9;
	const int64_t sync_width_min = 1.75e9;

	bool packetIsRequest = true;

	Packet* pack = nullptr;

	size_t i = 0;
	bool done = false;
	uint32_t request_addr = 0;
	bool last_was_i2c = false;
	while(i < len)
	{
		if(done)
			break;

		//Look for a falling edge (falling edge of the first preamble bit)
		if(!FindFallingEdge(i, din))
		{
			LogTrace("Capture ended before finding another preamble\n");
			break;
		}
		LogTrace("Start of frame\n");

		uint8_t current_byte = 0;
		int bitcount = 0;

		enum
		{
			FRAME_PREAMBLE_0,
			FRAME_PREAMBLE_1,
			FRAME_COMMAND,
			FRAME_ADDR_HI,
			FRAME_ADDR_MID,
			FRAME_ADDR_LO,
			FRAME_PAYLOAD,
			FRAME_LEN,
			FRAME_REPLY,
			FRAME_REPLY_PAD,
			FRAME_END_1,
			FRAME_END_2
		} frame_state = FRAME_PREAMBLE_0;

		//Recover the Manchester bitstream
		bool current_state = false;
		int64_t ui_start = GetOffsetScaled(din, i);
		int64_t symbol_start = i;
		int64_t last_edge = i;
		int64_t last_edge2 = i;
		uint32_t addr_hi = 0;
		LogTrace("[T = %s] Found initial falling edge\n", Unit(Unit::UNIT_FS).PrettyPrint(ui_start).c_str());
		pack = new Packet;
		m_packets.push_back(pack);
		pack->m_offset = ui_start;
		pack->m_len = 0;
		char tmp[32];
		while(i < len)
		{
			//When we get here, i points to the start of our UI
			//Expect an opposite polarity edge at the center of our bit
			//LogDebug("Looking for %d -> %d edge\n", current_state, !current_state);
			if(!FindEdge(i, din, !current_state))
			{
				LogTrace("Capture ended while looking for middle of this bit\n");
				done = true;
				break;
			}

			//If the edge came too soon or too late, possible sync error - restart from this edge
			//If the delta was more than ten UIs, it's a new frame - end this one
			int64_t edgepos = GetOffsetScaled(din, i);
			int64_t delta = edgepos - ui_start;
			if(delta > 10 * ui_width)
			{
				LogTrace("Premature end of frame (middle of a bit)\n");
				i++;
				break;
			}
			if( (delta < eye_start) || (delta > eye_end) )
			{
				//Special action for sync patterns
				bool good = false;
				if((delta > sync_width_min) && (delta < sync_width_max) )
				{
					LogTrace("sync path, state=%d, current=%d\n", frame_state, current_state);

					switch(frame_state)
					{
						//Waiting for high-going sync pulse
						case FRAME_PREAMBLE_0:
							if(current_state)
							{
								//Need to back up by two edges, because last_edge points to a timeout
								//one half-bit into the sync word
								cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_PREAMBLE));
								cap->m_offsets.push_back(symbol_start);
								cap->m_durations.push_back(last_edge2 - symbol_start);
								symbol_start = last_edge2;

								good = true;
								frame_state = FRAME_PREAMBLE_1;
							}
							break;

						//Waiting for low-going sync pulse
						case FRAME_PREAMBLE_1:
							if(!current_state)
							{
								good = true;

								if(packetIsRequest)
									frame_state = FRAME_COMMAND;
								else
									frame_state = FRAME_REPLY;

								//See how long the sync pulse was
								//We nominally want it to be 2us.
								//If it's closer to 2.5us, then the first half-bit of the payload must be included
								//in this pulse
								if(delta > 2.25e9)
									i -= ui_halfwidth / din->m_timescale;

								//Add the symbol
								cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_SYNC));
								cap->m_offsets.push_back(symbol_start);
								cap->m_durations.push_back(i - symbol_start);
								symbol_start = i;

								//reset for payload capture
								current_byte = 0;
								bitcount = 0;
							}
							break;

						case FRAME_PAYLOAD:
							if(current_state)
							{
								good = true;
								frame_state = FRAME_END_1;
							}
							break;

						case FRAME_END_1:
							if(!current_state)
							{
								good = true;
								frame_state = FRAME_END_2;
							}
							break;

						default:
							break;
					}
				}

				ui_start = GetOffsetScaled(din, i);
				i++;
				current_state = !current_state;

				if(!good)
					LogTrace("Edge was in the wrong place (delta=%zu), skipping it and attempting resync\n", delta);
				else if(frame_state == FRAME_PAYLOAD)
					LogTrace("Got valid sync pattern\n");
				else if(frame_state == FRAME_END_1)
				{
					cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_STOP));
					cap->m_offsets.push_back(symbol_start);
					cap->m_durations.push_back(i - symbol_start + 2*ui_width / din->m_timescale);

					//move ahead two UIs to skip end of frame etc
					//TODO: add a "return to differential zero" detector to do this more robustly?
					i += 3 * ui_width / din->m_timescale;

					packetIsRequest = !packetIsRequest;

					//Calculate final packet duration
					pack->m_len = ui_start - pack->m_offset;

					//Decode packet content
					if(!pack->m_data.empty())
						pack->m_headers["Info"] = DecodeRegisterContent(request_addr, pack->m_data);

					break;
				}
				else
					LogTrace("continuing with sync\n");

				last_edge2 = last_edge;
				last_edge = i;
				continue;
			}
			int64_t i_middle = i;
			int64_t ui_middle = GetOffsetScaled(din, i);

			//Edge is in the right spot! Decode it.
			//NOTE: Manchester polarity and bit ordering are inverted from Ethernet
			current_byte = (current_byte << 1) | current_state;
			bitcount ++;

			//Command, reply, and addr hi are only 4 bits long
			bool symbolDone = false;
			if(bitcount == 4)
			{
				switch(frame_state)
				{
					case FRAME_COMMAND:
						cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_COMMAND, current_byte));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						if( (current_byte & 3) == 0)
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
						else
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];

						current_byte = 0;
						bitcount = 0;

						pack->m_headers["Type"] = cap->GetText(cap->m_samples.size()-1);

						//Assume it's a native DP command for now, do I2C later?
						frame_state = FRAME_ADDR_HI;
						last_was_i2c = false;
						symbolDone = true;
						break;

					case FRAME_ADDR_HI:

						addr_hi = current_byte;

						current_byte = 0;
						bitcount = 0;

						frame_state = FRAME_ADDR_MID;
						symbolDone = true;
						break;

					case FRAME_REPLY:
						if(last_was_i2c)
							cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_I2C_REPLY, current_byte));
						else
							cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_AUX_REPLY, current_byte));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						//Push the address from the previous request
						snprintf(tmp, sizeof(tmp), "%06x", request_addr);
						pack->m_headers["Address"] = tmp;

						pack->m_headers["Type"] = cap->GetText(cap->m_samples.size()-1);
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];

						current_byte = 0;
						bitcount = 0;

						frame_state = FRAME_REPLY_PAD;
						symbolDone = true;
						break;

					case FRAME_REPLY_PAD:
						cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_PAD));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						current_byte = 0;
						bitcount = 0;

						frame_state = FRAME_PAYLOAD;
						symbolDone = true;
						break;

					default:
						break;
				}
			}

			//Rest of stuff is all full length bytes
			else if(bitcount == 8)
			{
				switch(frame_state)
				{
					case FRAME_ADDR_MID:
						addr_hi = (addr_hi << 8) | current_byte;
						frame_state = FRAME_ADDR_LO;
						symbolDone = true;
						break;

					case FRAME_ADDR_LO:
						addr_hi = (addr_hi << 8) | current_byte;
						request_addr = addr_hi;

						snprintf(tmp, sizeof(tmp), "%06x", addr_hi);
						pack->m_headers["Address"] = tmp;
						pack->m_headers["Info"] = DecodeRegisterName(addr_hi);

						cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_ADDRESS, addr_hi));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						frame_state = FRAME_LEN;
						symbolDone = true;
						break;

					case FRAME_LEN:
						pack->m_headers["Length"] = to_string(current_byte + 1);
						cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_LEN, current_byte));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						frame_state = FRAME_PAYLOAD;
						symbolDone = true;
						break;

					case FRAME_PAYLOAD:
						pack->m_data.push_back(current_byte);
						cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_DATA, current_byte));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						symbolDone = true;
						break;

					default:
						break;
				}

				current_byte = 0;
				bitcount = 0;
			}

			//See if we have an edge at the end of this bit period
			if(!FindEdge(i, din, current_state))
			{
				LogTrace("Capture ended while looking for end of this bit\n");
				done = true;
				break;
			}
			edgepos = GetOffsetScaled(din, i);
			delta = edgepos - ui_middle;

			//Next edge is way after the end of this bit.
			//It must be the middle of our next bit, deal with it later
			if(delta > eye_end)
			{
				//LogDebug("Next edge is after our end (must be middle of subsequent bit\n");
				current_state = !current_state;

				//Move back until we're about half a UI after the center edge of this bit
				i = i_middle;
				int64_t target = ui_middle + ui_halfwidth;
				while(i < len)
				{
					int64_t pos = GetOffsetScaled(din, i);
					if(pos >= target)
						break;
					else
						i++;
				}
			}

			//Next edge is at the end of this bit.
			//Move to the position of the edge and look for an opposite-polarity one
			else
			{
				//LogDebug("Edge is at end of this bit\n");
				//i already points to the edge, don't touch it
			}

			//Extend the previous symbol to the end of the full Manchester symbol
			if(symbolDone)
			{
				cap->m_durations[cap->m_durations.size()-1] += i - symbol_start;
				symbol_start = i;
			}

			//Either way, i now points to the beginning of the next bit's UI
			ui_start = GetOffsetScaled(din, i);
			last_edge2 = last_edge;
			last_edge = i;
		}
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

string DPAuxChannelDecoder::DecodeRegisterName(uint32_t nreg)
{
	//we dont have HDCP info at the moment, but we know what registers are used
	if( (nreg >= 0x68000) && (nreg <= 0x68fff) )
		return "(unknown, HDCP v1.3)";
	if( (nreg >= 0x69000) && (nreg <= 0x69fff) )
		return "(unknown, HDCP v2.2)";

	//eDP panel self refresh
	if( (nreg >= 0x70) && (nreg <= 0x7f) )
		return "(unknown, eDP self refresh)";

	switch(nreg)
	{
		//see table 2-183, DP 2.0 spec
		case 0x0000: return "DCPD_REV";
		case 0x0001: return "8B10B_MAX_LINK_RATE";
		case 0x0002: return "MAX_LANE_COUNT";
		case 0x0003: return "MAX_DOWNSPREAD";
		case 0x0004: return "NORP/DP_PWR_VOLTAGE_CAP";
		case 0x0005: return "DOWN_STREAM_PORT_PRESENT";
		case 0x0006: return "MAIN_LINK_CHANNEL_CODING_CAP";
		case 0x0007: return "DOWN_STREAM_PORT_COUNT";
		case 0x0008: return "RECEIVE_PORT0_CAP_0";
		case 0x0009: return "RECEIVE_PORT0_CAP_1";
		case 0x000a: return "RECEIVE_PORT1_CAP_0";
		case 0x000b: return "RECEIVE_PORT1_CAP_1";
		case 0x000c: return "I2C capabilities";
		case 0x000d: return "eDP_CONFIGURATION_CAP";
		case 0x000e: return "8B10B_TRAINING_AUX_RD_INTERVAL";
		case 0x000f: return "ADAPTER_CAP";
		case 0x0020: return "SINK_VIDEO_FALLBACK_FORMATS";
		case 0x0021: return "MSTM_CAP";
		case 0x0022: return "NUMBER_OF_AUDIO_ENDPOINTS";
		case 0x0023: return "AV_SYNC_DATA_BLOCK_AV_GRANULARITY";
		case 0x0024: return "AV_SYNC_DATA_BLOCK / AUD_DEC_LAT[7:0]";
		case 0x0025: return "AUD_DEC_LAT[15:8]";
		case 0x0026: return "AUD_PP_LAT[7:0]";
		case 0x0027: return "AUD_PP_LAT[15:8]";
		case 0x0028: return "VID_INTER_LAT[7:0]";
		case 0x0029: return "VID_PROG_LAT[7:0]";
		case 0x002a: return "REP_LAT[7:0]";
		case 0x002b: return "AUD_DEL_INS[7:0]";
		case 0x002c: return "AUD_DEL_INS[15:8]";
		case 0x002d: return "AUD_DEL_INS[23:16]";
		case 0x002e: return "RECEIVER_ADVANCED_LINK_POWER_MANAGEMENT_CAPABILITIES";
		case 0x002f: return "AUX_FRAME_SYNC";
		case 0x0030: return "GUID";	//thru 0x3f
		case 0x0040: return "GUID_2";	//thru 0x4f
		case 0x0054: return "RX_GTC_VALUE[7:0]";
		case 0x0055: return "RX_GTC_VALUE[15:8]";
		case 0x0056: return "RX_GTC_VALUE[23:16]";
		case 0x0057: return "RX_GTC_VALUE[31:24]";
		case 0x0058: return "RX_GC_MSTR_REQ";
		case 0x005a: return "RX_GTC_PHASE_SKEW_OFFSET[7:0]";
		case 0x005b: return "RX_GTC_PHASE_SKEW_OFFSET[15:8]";
		case 0x0060: return "DSC Support";
		case 0x0061: return "DSC Algorithm Revision";
		case 0x0062: return "DSC RX Buffer Block Size";
		case 0x0063: return "DSC RC Buffer Size";
		case 0x0064: return "DSC Slice Capabilities 1";
		case 0x0065: return "DSC Line Buffer Bit Depth";
		case 0x0066: return "DSC Feature Support";
		case 0x0067: return "Max supported bits/pixel";
		case 0x0068: return "Max supported bits/pixel";
		case 0x0069: return "DSC Decoder Pixel Encoding Format Capability";
		case 0x006a: return "DSC Decoder Color Depth Capability";
		case 0x006b: return "DSC Peak Throughput";
		case 0x006c: return "DSC Maximum Slice Width";
		case 0x006d: return "DSC Slice Capabilities 2";
		case 0x006e: return "DSC_MAX_BPP_DELTA_AND_BPP_INCREMENT";
		case 0x006f: return "DSC_MAX_BPP_DELTA_AND_BPP_INCREMENT";
		case 0x0080: return "DPFX_CAP";

		case 0x00b0: return "Panel replay capability supported";

		case 0x0100: return "LINK_BW_SET";
		case 0x0101: return "LANE_COUNT_SET";
		case 0x0102: return "TRAINING_PATTERN_SET";
		case 0x0103: return "TRAINING_LANE0_SET";
		case 0x0104: return "TRAINING_LANE1_SET";
		case 0x0105: return "TRAINING_LANE2_SET";
		case 0x0106: return "TRAINING_LANE3_SET";
		case 0x0107: return "DOWNSPREAD_CTRL";
		case 0x0108: return "MAIN_LINK_CHANNEL_CODING_SET";
		case 0x0109: return "I2C Speed Control/Status Bit Map";
		case 0x010a: return "eDP_CONFIGURATION_SET";
		case 0x010b: return "LINK_QUAL_LANE0_SET";
		case 0x010c: return "LINK_QUAL_LANE1_SET";
		case 0x010d: return "LINK_QUAL_LANE2_SET";
		case 0x010e: return "LINK_QUAL_LANE3_SET";

		case 0x0300: return "Source IEEE_OUI[0]";
		case 0x0301: return "Source IEEE_OUI[1]";
		case 0x0302: return "Source IEEE_OUI[2]";
		case 0x0303: return "Source DEVICE_ID[0]";
		case 0x0304: return "Source DEVICE_ID[1]";
		case 0x0305: return "Source DEVICE_ID[2]";
		case 0x0306: return "Source DEVICE_ID[3]";
		case 0x0307: return "Source DEVICE_ID[4]";
		case 0x0308: return "Source DEVICE_ID[5]";
		case 0x0309: return "Source Hardware Revision";
		case 0x030a: return "Source Firmware/Software Major Revision";
		case 0x030b: return "Source Firmware/Software Minor Revision";

		case 0x0500: return "Branch IEEE_OUI[0]";
		case 0x0501: return "Branch IEEE_OUI[1]";
		case 0x0502: return "Branch IEEE_OUI[2]";
		case 0x0503: return "Branch DEVICE_ID[0]";
		case 0x0504: return "Branch DEVICE_ID[1]";
		case 0x0505: return "Branch DEVICE_ID[2]";
		case 0x0506: return "Branch DEVICE_ID[3]";
		case 0x0507: return "Branch DEVICE_ID[4]";
		case 0x0508: return "Branch DEVICE_ID[5]";
		case 0x0509: return "Branch Hardware Revision";
		case 0x050a: return "Branch Firmware/Software Major Revision";
		case 0x050b: return "Branch Firmware/Software Minor Revision";

		case 0x2000: return "RESERVED";
		case 0x2001: return "RESERVED";
		case 0x2002: return "SINK_COUNT_ESI";
		case 0x2003: return "DEVICE_SERVICE_IRQ_VECTOR_ESI0";
		case 0x2004: return "DEVICE_SERVICE_IRQ_VECTOR_ESI1";
		case 0x2005: return "LINK_SERVICE_IRQ_VECTOR_ESI0";
		case 0x200c: return "LANE0_1_STATUS_ESI";
		case 0x200d: return "LANE2_3_STATUS_ESI";

		case 0x2200: return "DCPD_REV";
		case 0x2201: return "8B10B_MAX_LINK_RATE";
		case 0x2202: return "MAX_LANE_COUNT";
		case 0x2203: return "MAX_DOWNSPREAD";
		case 0x2204: return "NORP/DP_PWR_VOLTAGE_CAP";
		case 0x2205: return "DOWN_STREAM_PORT_PRESENT";
		case 0x2206: return "MAIN_LINK_CHANNEL_CODING_CAP";
		case 0x2207: return "DOWN_STREAM_PORT_COUNT";
		case 0x2208: return "RECEIVE_PORT0_CAP_0";
		case 0x2209: return "RECEIVE_PORT0_CAP_1";
		case 0x220a: return "RECEIVE_PORT1_CAP_0";
		case 0x220b: return "RECEIVE_PORT1_CAP_1";
		case 0x220c: return "I2C capabilities";
		case 0x220d: return "eDP_CONFIGURATION_CAP";
		case 0x220e: return "8B10B_TRAINING_AUX_RD_INTERVAL";
		case 0x220f: return "ADAPTER_CAP";
		case 0x2210: return "DPRX_FEATURE_ENUMERATION_LIST";
		case 0x2211: return "EXTENDED_DPRX_SLEEP_WAKE_TIMEOUT_REQUEST";
		case 0x2212: return "VSC_EXT_VESA_SDP_MAX_CHAINING";
		case 0x2213: return "VSC_EXT_CTA_SDP_MAX_CHAINING";

		case 0xf0000: return "LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV";
		case 0xf0001: return "8B10B_MAX_LINK_RATE_PHY_REPEATER";
		case 0xf0002: return "PHY_REPEATER_CNT";
		case 0xf0003: return "PHY_REPEATER_MODE";
		case 0xf0004: return "MAX_LANE_COUNT_PHY_REPEATER";
		case 0xf0005: return "PHY_REPEATER_EXTENDED_WAKE_TIMEOUT";
		case 0xf0006: return "MAIN_CHANNEL_CODING_PHY_REPEATER";
		case 0xf0007: return "PHY_REPEATER_128B/132B_RATES";

		default:
			return "";
	}
}

string DPAuxChannelDecoder::DecodeRegisterContent(uint32_t start_addr, const vector<uint8_t>& data)
{
	size_t i = 0;
	string ret = "";
	while(i < data.size())
	{
		//Decode known register bitfields
		string out = "";
		size_t fieldsize = 1;
		switch(start_addr)
		{
			//DCPD_REV
			case 0x00000:
				out = string("DCPD r") + to_string(data[i] >> 4) + "." + to_string(data[i] & 0xf);
				break;

			//MAX_LANE_COUNT
			case 0x00002:
				out = string("Max lanes: ") + to_string(data[i] & 0x1f) + "\n";
				if(data[i] & 0x20)
					out += "POST_LT_ADJ_REQ supported\n";
				else
					out += "POST_LT_ADJ_REQ not supported\n";
				if(data[i] & 0x40)
					out += "TPS3 supported\n";
				else
					out += "TPS3 not supported\n";
				if(data[i] & 0x80)
					out += "Enhanced framing sequence supported";
				else
					out += "Enhanced framing sequence not supported";
				break;

			//MAX_DOWNSPREAD
			case 0x00003:
				if(data[i] & 0x1)
					out += "Spread spectrum clocking supported\n";
				else
					out += "Spread spectrum clocking not supported\n";
				if(data[i] & 0x2)
					out += "Stream regeneration supported\n";
				else
					out += "Stream regeneration not supported\n";
				if(data[i] & 0x40)
					out += "AUX transactions not needed for link training\n";
				else
					out += "AUX transactions required for link training\n";
				if(data[i] & 0x80)
					out += "TPS4 supported";
				else
					out += "TPS4 not supported";
				break;

			//8B10B_TRAINING_AUX_RD_INTERVAL
			case 0x0000e:
				if(data[i] & 0x80)
					out = "Extended RX capability field present\n";
				else
					out = "Extended RX capability field not present\n";
				out += "LANEx_CR_DONE polling interval: 100 μs\n";
				switch(data[i] & 0x7f)
				{
					case 0:
						out += "LANEx_CHANNEL_EQ_DONE polling interval: 400 μs";
						break;

					case 1:
						out += "LANEx_CHANNEL_EQ_DONE polling interval: 4 ms";
						break;

					case 2:
						out += "LANEx_CHANNEL_EQ_DONE polling interval: 8 ms";
						break;

					case 3:
						out += "LANEx_CHANNEL_EQ_DONE polling interval: 12 ms";
						break;

					case 4:
						out += "LANEx_CHANNEL_EQ_DONE polling interval: 16 ms";
						break;

					default:
						out += "LANEx_CHANNEL_EQ_DONE polling interval: reserved";
						break;
				}

				break;

			//DCPD_REV (extended)
			case 0x02200:
				out = string("DCPD r") + to_string(data[i] >> 4) + "." + to_string(data[i] & 0xf);
				break;

			//8B10B_MAX_LINK_RATE (extended)
			case 0x2201:
				switch(data[i])
				{
					case 0x06:
						out = "1.62 Gbps/lane (RBR)";
						break;

					case 0x0a:
						out = "2.7 Gbps/lane (HBR)";
						break;

					case 0x14:
						out = "5.4 Gbps/lane (HBR2)";
						break;

					case 0x1e:
						out = "8.1 Gbps/lane (HBR3)";
						break;

					default:
						out = "Unknown rate (reserved)";
						break;
				}
				break;

			//Unknown? Don't print any decode, skip it
			default:
				break;
		}

		//Default to advancing by one byte, but some are larger
		i += fieldsize;
		start_addr += fieldsize;

		//Concatenate lines
		if(!out.empty())
		{
			if(ret.empty())
				ret = out;
			else
				ret += "\n" + out;
		}
	}

	return ret;
}

bool DPAuxChannelDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//Merge reads and writes with their completions
	if( (first->m_headers["Type"] == "DP Read") && (next->m_headers["Type"] == "AUX_ACK") )
		return true;
	if( (first->m_headers["Type"] == "DP Write") && (next->m_headers["Type"] == "AUX_ACK") )
		return true;

	return false;
}

Packet* DPAuxChannelDecoder::CreateMergedHeader(Packet* pack, size_t i)
{
	//Combine DP read with completion
	if(pack->m_headers["Type"] == "DP Read")
	{
		Packet* ret = new Packet;
		ret->m_offset = pack->m_offset;
		ret->m_len = pack->m_len;
		ret->m_headers["Type"] = pack->m_headers["Type"];
		ret->m_headers["Address"] = pack->m_headers["Address"];
		ret->m_headers["Length"] = pack->m_headers["Length"];
		ret->m_headers["Info"] = pack->m_headers["Info"];
		ret->m_displayBackgroundColor = pack->m_displayBackgroundColor;

		//Add data from reply, if available
		if(i+1 < m_packets.size())
		{
			auto next = m_packets[i+1];
			ret->m_data = next->m_data;
			ret->m_len = next->m_offset + next->m_len - pack->m_offset;

			auto info = next->m_headers["Info"];
			if(!info.empty())
				ret->m_headers["Info"] += "\n" + info;
		}

		return ret;
	}

	//Combine DP write with completion
	if(pack->m_headers["Type"] == "DP Write")
	{
		Packet* ret = new Packet;
		ret->m_offset = pack->m_offset;
		ret->m_len = pack->m_len;
		ret->m_headers["Type"] = pack->m_headers["Type"];
		ret->m_headers["Address"] = pack->m_headers["Address"];
		ret->m_headers["Length"] = pack->m_headers["Length"];
		ret->m_headers["Info"] = pack->m_headers["Info"];
		ret->m_displayBackgroundColor = pack->m_displayBackgroundColor;
		ret->m_data = pack->m_data;

		//Extend to reply
		if(i+1 < m_packets.size())
		{
			auto next = m_packets[i+1];
			ret->m_len = next->m_offset + next->m_len - pack->m_offset;
		}

		return ret;
	}

	return NULL;
}

bool DPAuxChannelDecoder::FindFallingEdge(size_t& i, UniformAnalogWaveform* cap)
{
	size_t j = i;
	size_t len = cap->m_samples.size();
	while(j < len)
	{
		if(cap->m_samples[j] < -0.125)
		{
			i = j;
			return true;
		}
		j++;
	}

	return false;	//not found
}

bool DPAuxChannelDecoder::FindRisingEdge(size_t& i, UniformAnalogWaveform* cap)
{
	size_t j = i;
	size_t len = cap->m_samples.size();
	while(j < len)
	{
		if(cap->m_samples[j] > 0.125)
		{
			i = j;
			return true;
		}
		j++;
	}

	return false;	//not found
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DPAuxWaveform

string DPAuxWaveform::GetColor(size_t i)
{
	const DPAuxSymbol& s = m_samples[i];

	switch(s.m_stype)
	{
		case DPAuxSymbol::TYPE_ERROR:
			return StandardColors::colors[StandardColors::COLOR_ERROR];

		case DPAuxSymbol::TYPE_PREAMBLE:
		case DPAuxSymbol::TYPE_SYNC:
		case DPAuxSymbol::TYPE_STOP:
		case DPAuxSymbol::TYPE_PAD:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case DPAuxSymbol::TYPE_COMMAND:
		case DPAuxSymbol::TYPE_AUX_REPLY:
		case DPAuxSymbol::TYPE_I2C_REPLY:
		case DPAuxSymbol::TYPE_LEN:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case DPAuxSymbol::TYPE_ADDRESS:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case DPAuxSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		default:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];
	}
}

string DPAuxWaveform::GetText(size_t i)
{
	const DPAuxSymbol& s = m_samples[i];

	char tmp[32];
	switch(s.m_stype)
	{
		case DPAuxSymbol::TYPE_ERROR:
			return "ERR";

		case DPAuxSymbol::TYPE_PREAMBLE:
			return "PREAMBLE";

		case DPAuxSymbol::TYPE_PAD:
			return "PAD";

		case DPAuxSymbol::TYPE_SYNC:
			return "SYNC";

		case DPAuxSymbol::TYPE_STOP:
			return "STOP";

		case DPAuxSymbol::TYPE_AUX_REPLY:
			switch(s.m_data & 3)
			{
				case 0:
					return "AUX_ACK";

				case 1:
					return "AUX_NACK";

				case 2:
					return "AUX_DEFER";

				case 3:
				default:
					return "RESERVED";
			}
		case DPAuxSymbol::TYPE_I2C_REPLY:
			switch((s.m_data >> 2) & 3)
			{
				case 0:
					return "I2C_ACK";

				case 1:
					return "I2C_NACK";

				case 2:
					return "I2C_DEFER";

				case 3:
				default:
					return "RESERVED";
			}
			break;

		case DPAuxSymbol::TYPE_COMMAND:

			//DP transaction
			if(s.m_data & 0x8)
			{
				switch(s.m_data & 0x7)
				{
					case 0:
						return "DP Write";

					case 1:
						return "DP Read";

					default:
						return "DP Reserved";
				}
			}

			//DP over I2C
			else
			{
				string ret = "I2C ";
				if(s.m_data & 0x4)
					ret += "MOT ";

				switch(s.m_data & 0x3)
				{
					case 0:
						ret += "Write";
						break;

					case 1:
						ret + "Read";
						break;

					case 2:
						ret += "WSUR";	//Write-Status-Update-Request
						break;

					case 3:
						ret += "RSVD";
						break;
				}
				return ret;
			}
			break;

		case DPAuxSymbol::TYPE_LEN:
			snprintf(tmp, sizeof(tmp), "Len: %d", s.m_data + 1);	//length is offset by one since len=0 makes no sense
			break;

		case DPAuxSymbol::TYPE_ADDRESS:
			snprintf(tmp, sizeof(tmp), "Addr: %06x", s.m_data);
			break;

		case DPAuxSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			break;
	}
	return string(tmp);
}

