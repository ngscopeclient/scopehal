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
#include "EthernetProtocolDecoder.h"
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
	ret.push_back("Direction");
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

	size_t i = 0;
	bool done = false;
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
								frame_state = FRAME_COMMAND;

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

			//Command and addr hi are only 4 bits long
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

						current_byte = 0;
						bitcount = 0;

						//Assume it's a native DP command for now, do I2C later?
						frame_state = FRAME_ADDR_HI;
						symbolDone = true;
						break;

					case FRAME_ADDR_HI:

						addr_hi = current_byte;

						current_byte = 0;
						bitcount = 0;

						frame_state = FRAME_ADDR_MID;
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
						cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_ADDRESS, (addr_hi << 8) | current_byte));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						frame_state = FRAME_LEN;
						symbolDone = true;
						break;

					case FRAME_LEN:
						cap->m_samples.push_back(DPAuxSymbol(DPAuxSymbol::TYPE_LEN, current_byte));
						cap->m_offsets.push_back(symbol_start);
						cap->m_durations.push_back(i - symbol_start);
						symbol_start = i;

						frame_state = FRAME_PAYLOAD;
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
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case DPAuxSymbol::TYPE_COMMAND:
		case DPAuxSymbol::TYPE_LEN:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case DPAuxSymbol::TYPE_ADDRESS:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		/*
		case DPAuxSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];
		*/

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

		case DPAuxSymbol::TYPE_SYNC:
			return "SYNC";

		case DPAuxSymbol::TYPE_STOP:
			return "STOP";

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
			snprintf(tmp, sizeof(tmp), "Len: %d", s.m_data);
			break;

		case DPAuxSymbol::TYPE_ADDRESS:
			snprintf(tmp, sizeof(tmp), "Addr: %06x", s.m_data);
			break;
			/*
		case DPAuxSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			break;
			*/
	}
	return string(tmp);
}

