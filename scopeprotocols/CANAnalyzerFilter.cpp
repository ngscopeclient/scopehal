/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "CANAnalyzerFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CANAnalyzerFilter::CANAnalyzerFilter(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CANAnalyzerFilter::GetProtocolName()
{
	return "CAN Analyzer";
}

vector<string> CANAnalyzerFilter::GetHeaders()
{
	vector<string> ret;
	ret.push_back("ID");
	ret.push_back("Mode");
	ret.push_back("Format");
	ret.push_back("Type");
	ret.push_back("Ack");
	ret.push_back("Len");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

Filter::DataLocation CANAnalyzerFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

bool CANAnalyzerFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<CANWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

void CANAnalyzerFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	ClearErrors();
	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}

	auto din = dynamic_cast<CANWaveform*>(GetInputWaveform(0));
	auto len = din->size();

	//copy input to output
	auto cap = SetupEmptyWaveform<CANWaveform>(din, 0, true);
	cap->m_offsets.CopyFrom(din->m_offsets);
	cap->m_durations.CopyFrom(din->m_durations);
	cap->m_samples.CopyFrom(din->m_samples);
	cap->PrepareForCpuAccess();

	ClearPackets();

	enum
	{
		STATE_IDLE,
		STATE_DATA,
		STATE_GARBAGE
	} state = STATE_IDLE;

	Packet* pack = nullptr;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];

		switch(state)
		{
			case STATE_IDLE:

				if( (s.m_stype == CANSymbol::TYPE_ID) && pack)
				{
					if(s.m_data & 0x80000000)
					{
						pack->m_headers["Format"] = "EXT";
						pack->m_headers["ID"] = to_string_hex(s.m_data & 0x3fffffff, true, 8);
					}
					else
					{
						pack->m_headers["Format"] = "BASE";
						pack->m_headers["ID"] = to_string_hex(s.m_data, true, 3);
					}

					if(s.m_data & 0x20000000)
					{
						pack->m_headers["Format"] = "ERR";
						pack->m_headers["ID"] = "";

						pack->m_len =
							din->m_triggerPhase +
							din->m_timescale * (din->m_offsets[i] + din->m_durations[i]) -
							pack->m_offset;

						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
						state = STATE_GARBAGE;
					}

				}

				if( (s.m_stype == CANSymbol::TYPE_DLC) && pack)
				{
					pack->m_headers["Len"] = to_string(s.m_data);
					state = STATE_DATA;
				}

				break;

			case STATE_DATA:

				if( (s.m_stype == CANSymbol::TYPE_DATA) && pack)
				{
					pack->m_data.push_back(s.m_data);

					//Extend duration
					pack->m_len =
						din->m_triggerPhase +
						din->m_timescale * (din->m_offsets[i] + din->m_durations[i]) -
						pack->m_offset;
				}

				break;

			default:
				break;
		}

		//Start a new packet
		if(s.m_stype == CANSymbol::TYPE_SOF)
		{
			pack = new Packet;
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
			pack->m_offset = din->m_triggerPhase + din->m_timescale * din->m_offsets[i];
			m_packets.push_back(pack);

			//TODO: FD / RTR support etc?
			pack->m_headers["Mode"] = "CAN";

			state = STATE_IDLE;
		}
	}
}
