/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
#include "CANDecoder.h"
#include "J1939PDUDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

J1939PDUDecoder::J1939PDUDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	CreateInput("can");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool J1939PDUDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<CANWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

vector<string> J1939PDUDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Priority");
	ret.push_back("PGN");
	ret.push_back("EDP");
	ret.push_back("DP");
	ret.push_back("Format");
	ret.push_back("Group ext");
	ret.push_back("Dest");
	ret.push_back("Source");
	ret.push_back("Length");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string J1939PDUDecoder::GetProtocolName()
{
	return "J1939 PDU";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void J1939PDUDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	auto din = dynamic_cast<CANWaveform*>(GetInputWaveform(0));
	auto len = din->size();
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}

	//Create the capture
	auto cap = new J1939PDUWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = 0;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	enum
	{
		STATE_IDLE,
		STATE_DLC,
		STATE_DATA
	} state = STATE_IDLE;

	//Process the CAN packet stream
	size_t bytesleft = 0;
	Packet* pack = nullptr;
	for(size_t i=0; i<len; i++)
	{
		auto& s = din->m_samples[i];

		int64_t tstart = din->m_offsets[i] * din->m_timescale + din->m_triggerPhase;
		int64_t tend = tstart + din->m_durations[i] * din->m_timescale;

		switch(state)
		{
			//Look for a CAN ID (ignore anything else)
			case STATE_IDLE:
				if(s.m_stype == CANSymbol::TYPE_ID)
				{
					//Start a new packet
					pack = new Packet;
					pack->m_offset = tstart;
					pack->m_len = 0;
					m_packets.push_back(pack);

					auto p = s.m_data >> 26;
					auto edp = (s.m_data >> 25) & 1;
					auto dp = (s.m_data >> 24) & 1;
					auto pf = (s.m_data >> 16) & 0xff;
					auto ps = (s.m_data >> 8) & 0xff;
					auto sa = (s.m_data) & 0xff;

					//PGN format (J1939-21 5.1.2)
					auto pgn = (edp << 18) | (dp << 17) | (pf << 8);

					//Crack headers into time domain format
					int64_t delta = tend - tstart;
					int64_t prilen = delta / 10;
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(prilen);
					cap->m_samples.push_back(J1939PDUSymbol(J1939PDUSymbol::TYPE_PRI, p));

					if(pf < 240)
					{
						pack->m_headers["Type"] = "PDU1";
						pack->m_headers["Dest"] = to_string(ps);
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];

						//PGN does not include PS
						int64_t pgnlen = 3 * prilen;
						cap->m_offsets.push_back(tstart + prilen);
						cap->m_durations.push_back(pgnlen);
						cap->m_samples.push_back(J1939PDUSymbol(J1939PDUSymbol::TYPE_PGN, pgn));

						//PS is dest addr
						int64_t pslen = 2 * prilen;
						cap->m_offsets.push_back(tstart + prilen + pgnlen);
						cap->m_durations.push_back(pslen);
						cap->m_samples.push_back(J1939PDUSymbol(J1939PDUSymbol::TYPE_DEST, ps));
					}
					else
					{
						pack->m_headers["Type"] = "PDU2";
						pack->m_headers["Group ext"] = to_string(ps);
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];

						//PGN includes PS
						pgn |= ps;

						int64_t pgnlen = 5 * prilen;
						cap->m_offsets.push_back(tstart + prilen);
						cap->m_durations.push_back(pgnlen);
						cap->m_samples.push_back(J1939PDUSymbol(J1939PDUSymbol::TYPE_PGN, pgn));
					}

					//Source address
					cap->m_offsets.push_back(tstart + 6*prilen);
					cap->m_durations.push_back(4*prilen);
					cap->m_samples.push_back(J1939PDUSymbol(J1939PDUSymbol::TYPE_SRC, sa));

					pack->m_headers["Priority"] = to_string(p);
					pack->m_headers["EDP"] = to_string(edp);
					pack->m_headers["DP"] = to_string(dp);
					pack->m_headers["Format"] = to_string(pf);
					pack->m_headers["Source"] = to_string(sa);
					pack->m_headers["PGN"] = to_string(pgn);

					state = STATE_DLC;
				}
				break;

			//Look for the DLC so we know how many bytes to read
			case STATE_DLC:
				if(s.m_stype == CANSymbol::TYPE_DLC)
				{
					bytesleft = s.m_data;
					state = STATE_DATA;
				}

				break;

			//Read the actual data bytes, MSB first
			case STATE_DATA:
				if(s.m_stype == CANSymbol::TYPE_DATA)
				{
					pack->m_data.push_back(s.m_data);

					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(tend - tstart);
					cap->m_samples.push_back(J1939PDUSymbol(J1939PDUSymbol::TYPE_DATA, s.m_data));

					//Are we done with the frame?
					bytesleft --;
					if(bytesleft == 0)
					{
						state = STATE_IDLE;
						pack->m_len = tend - pack->m_offset;
					}
				}

				//Discard anything else
				else
					state = STATE_IDLE;
				break;

				//TODO: if CRC is bad, discard the in-progress packet and any samples generated by it

			default:
				break;
		}

		//If we see a SOF previous frame was truncated, reset
		if(s.m_stype == CANSymbol::TYPE_SOF)
			state = STATE_IDLE;
	}

	//Done updating
	cap->MarkModifiedFromCpu();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// J1939PDUWaveform

string J1939PDUWaveform::GetColor(size_t i)
{
	const J1939PDUSymbol& s = m_samples[i];

	switch(s.m_stype)
	{
		case J1939PDUSymbol::TYPE_PRI:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case J1939PDUSymbol::TYPE_PGN:
		case J1939PDUSymbol::TYPE_DEST:
		case J1939PDUSymbol::TYPE_SRC:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case J1939PDUSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string J1939PDUWaveform::GetText(size_t i)
{
	const J1939PDUSymbol& s = m_samples[i];

	char tmp[32];
	switch(s.m_stype)
	{
		case J1939PDUSymbol::TYPE_PRI:
			return string("Pri: ") + to_string(s.m_data);

		case J1939PDUSymbol::TYPE_PGN:
			return string("PGN: ") + to_string(s.m_data);

		case J1939PDUSymbol::TYPE_DEST:
			return string("Dest: ") + to_string(s.m_data);

		case J1939PDUSymbol::TYPE_SRC:
			return string("Src: ") + to_string(s.m_data);

		case J1939PDUSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			break;

		default:
			return "ERROR";
	}
	return string(tmp);
}
