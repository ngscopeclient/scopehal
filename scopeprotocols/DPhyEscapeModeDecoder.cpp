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
#include "DPhyEscapeModeDecoder.h"
#include "DPhySymbolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DPhyEscapeModeDecoder::DPhyEscapeModeDecoder(const string& color)
	: PacketDecoder(color, CAT_SERIAL)
{
	AddProtocolStream("data");
	CreateInput("Data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DPhyEscapeModeDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<DPhySymbolDecoder*>(stream.m_channel) != nullptr) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DPhyEscapeModeDecoder::GetProtocolName()
{
	return "MIPI D-PHY Escape Mode";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

vector<string> DPhyEscapeModeDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Operation");
	return ret;
}

void DPhyEscapeModeDecoder::Refresh()
{
	ClearPackets();

	//Sanity check
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}
	auto data = dynamic_cast<DPhySymbolWaveform*>(GetInputWaveform(0));

	//Create output waveform
	auto cap = new DPhyEscapeModeWaveform;
	cap->m_timescale = data->m_timescale;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startFemtoseconds = data->m_startFemtoseconds;
	cap->m_triggerPhase = data->m_triggerPhase;
	SetData(cap, 0);

	Packet* pack = nullptr;

	enum
	{
		STATE_UNKNOWN,
		STATE_IDLE,
		STATE_ESCAPE_ENTRY_0,
		STATE_ESCAPE_ENTRY_1,
		STATE_ESCAPE_ENTRY_2,
		STATE_ESCAPE_ENTRY_3,
		STATE_ESCAPE_ENTRY_4,
		STATE_ENTRY_COMMAND_0,
		STATE_ENTRY_COMMAND_1,
		STATE_LP_DATA_0,
		STATE_LP_DATA_1
	} state = STATE_UNKNOWN;

	auto len = data->m_samples.size();
	int64_t start = 0;
	uint8_t tmp = 0;
	uint8_t count = 0;
	int64_t packstart = 0;
	for(size_t i=0; i<len; i++)
	{
		auto sym = data->m_samples[i];

		//Ignore any HS line states as they frequently occur as noise around transitions between LP states
		if( (sym.m_type == DPhySymbol::STATE_HS0) || (sym.m_type == DPhySymbol::STATE_HS1) )
			continue;

		//Spaced one-hot coding
		//LP-00 is gap between bits
		//LP-01 = zero bit
		//LP-10 = one bit
		bool bvalid = false;
		uint8_t bvalue;
		if(sym.m_type == DPhySymbol::STATE_LP01)
		{
			bvalid = true;
			bvalue = 0;
		}
		else if(sym.m_type == DPhySymbol::STATE_LP10)
		{
			bvalid = true;
			bvalue = 1;
		}

		//LP states are potentially interesting
		switch(state)
		{
			//Idle, waiting for an escape mode entry
			case STATE_IDLE:
				if(sym.m_type == DPhySymbol::STATE_LP10)
				{
					start = data->m_offsets[i];
					packstart = start;
					state = STATE_ESCAPE_ENTRY_0;
				}
				else
					state = STATE_UNKNOWN;
				break;

			//Beginning an escape sequence, expect LP-00 next
			case STATE_ESCAPE_ENTRY_0:
				if(sym.m_type == DPhySymbol::STATE_LP00)
					state = STATE_ESCAPE_ENTRY_1;
				else
					state = STATE_UNKNOWN;
				break;

			//Continuing escape sequence, expect LP-01 next
			case STATE_ESCAPE_ENTRY_1:
				if(sym.m_type == DPhySymbol::STATE_LP01)
					state = STATE_ESCAPE_ENTRY_2;
				else
					state = STATE_UNKNOWN;
				break;

			//Continuing escape sequence, expect LP-00 next
			case STATE_ESCAPE_ENTRY_2:
				if(sym.m_type == DPhySymbol::STATE_LP00)
				{
					state = STATE_ENTRY_COMMAND_0;

					//Add a symbol for the entry sequence
					int64_t end = data->m_offsets[i] + data->m_durations[i];
					cap->m_offsets.push_back(start);
					cap->m_durations.push_back(end - start);
					cap->m_samples.push_back(DPhyEscapeModeSymbol(DPhyEscapeModeSymbol::TYPE_ESCAPE_ENTRY));

					//Make the packet
					pack = new Packet;
					pack->m_offset = packstart * data->m_timescale + data->m_triggerPhase;
					pack->m_len = 0;
					m_packets.push_back(pack);

					//Prepare for the entry command
					start = end;
					tmp = 0;
					count = 0;
				}
				else
					state = STATE_UNKNOWN;
				break;	//end STATE_ESCAPE_ENTRY_2

			//Entry command: waiting for a data bit to start
			case STATE_ENTRY_COMMAND_0:
				if(bvalid)
				{
					count ++;
					tmp = (tmp << 1) | bvalue;

					state = STATE_ENTRY_COMMAND_1;
				}
				break;	//end STATE_ENTRY_COMMAND_0

			//Entry command: waiting for a data bit to end
			case STATE_ENTRY_COMMAND_1:

				if(sym.m_type == DPhySymbol::STATE_LP00)
				{
					//More bits to read?
					if(count < 8)
						state = STATE_ENTRY_COMMAND_0;

					//End of command
					else
					{
						//Add a symbol for the entry sequence
						int64_t end = data->m_offsets[i] + data->m_durations[i];
						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(end - start);
						cap->m_samples.push_back(DPhyEscapeModeSymbol(DPhyEscapeModeSymbol::TYPE_ENTRY_COMMAND, tmp));

						pack->m_headers["Operation"] = cap->GetText(cap->m_offsets.size() - 1);

						//Low power data?
						if(tmp == 0xe1)
						{
							state = STATE_LP_DATA_0;
							start = end;
							tmp = 0;
							count = 0;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
						}

						//Ignore anything else for now
						else
						{
							state = STATE_UNKNOWN;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DEFAULT];
						}
					}
				}

				break;	//end STATE_ENTRY_COMMAND_1

			//LP-mode data: waiting for a data bit to start
			case STATE_LP_DATA_0:
				if(bvalid)
				{
					count ++;
					tmp = (tmp << 1) | bvalue;

					state = STATE_LP_DATA_1;
				}
				break;	//end STATE_LP_DATA_0

			//LP-mode data: waiting for a data bit to end
			case STATE_LP_DATA_1:

				if(sym.m_type == DPhySymbol::STATE_LP00)
				{
					//More bits to read?
					if(count < 8)
						state = STATE_LP_DATA_0;

					//End of command
					else
					{
						//Add a symbol for the data sequence
						int64_t end = data->m_offsets[i] + data->m_durations[i];
						cap->m_offsets.push_back(start);
						cap->m_durations.push_back(end - start);
						cap->m_samples.push_back(DPhyEscapeModeSymbol(DPhyEscapeModeSymbol::TYPE_ESCAPE_DATA, tmp));

						//Update packet
						pack->m_data.push_back(tmp);
						pack->m_len = (end * data->m_timescale) + data->m_triggerPhase - pack->m_offset;

						//Reset for the next byte
						start = end;
						tmp = 0;
						count = 0;
						state = STATE_LP_DATA_0;
					}
				}

				break;	//end STATE_LP_DATA_1

			case STATE_UNKNOWN:
			default:
				break;
		}

		//LP-11 resets us to idle from any state
		if(sym.m_type == DPhySymbol::STATE_LP11)
			state = STATE_IDLE;
	}
}

std::string DPhyEscapeModeWaveform::GetColor(size_t i)
{
	const DPhyEscapeModeSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case DPhyEscapeModeSymbol::TYPE_ESCAPE_ENTRY:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case DPhyEscapeModeSymbol::TYPE_ENTRY_COMMAND:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case DPhyEscapeModeSymbol::TYPE_ESCAPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case DPhyEscapeModeSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string DPhyEscapeModeWaveform::GetText(size_t i)
{
	char tmp[32];
	const DPhyEscapeModeSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case DPhyEscapeModeSymbol::TYPE_ESCAPE_ENTRY:
			return "Escape Entry";

		case DPhyEscapeModeSymbol::TYPE_ENTRY_COMMAND:
			switch(s.m_data)
			{
				case 0xe1:
					return "Low Power Data";
				case 0x1e:
					return "Ultra-Low Power";
				case 0x9f:
					return "Undefined-1";
				case 0xde:
					return "Undefined-2";
				case 0x62:
					return "Reset-Trigger";
				case 0x5d:
					return "HS Test Mode";
				case 0x21:
					return "Unknown-4";
				case 0xa0:
					return "Unknown-5";
				default:
					snprintf(tmp, sizeof(tmp), "Invalid (%02x)", s.m_data);
					return tmp;
			}

		case DPhyEscapeModeSymbol::TYPE_ESCAPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return tmp;

		case DPhyEscapeModeSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}

