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
#include "J1939TransportDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

J1939TransportDecoder::J1939TransportDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	CreateInput("j1939");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool J1939TransportDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<J1939PDUWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

vector<string> J1939TransportDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Priority");
	ret.push_back("PGN");
	ret.push_back("PGN Name");
	ret.push_back("EDP");
	ret.push_back("DP");
	ret.push_back("Format");
	ret.push_back("Group ext");
	ret.push_back("Dest");
	ret.push_back("Source");
	ret.push_back("Length");
	ret.push_back("Info");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string J1939TransportDecoder::GetProtocolName()
{
	return "J1939 Transport";
}

Filter::DataLocation J1939TransportDecoder::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void J1939TransportDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("J1939TransportDecoder::Refresh");
	#endif

	ClearPackets();

	//Make sure we've got valid inputs
	ClearErrors();
	auto din = dynamic_cast<J1939PDUWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");
		else
			AddErrorMessage("Invalid input", "Expected a J1939 PDU waveform");

		SetData(nullptr, 0);
		return;
	}
	auto len = din->size();

	//Create the capture
	auto cap = SetupEmptyWaveform<J1939PDUWaveform>(din, 0);
	din->PrepareForCpuAccess();
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_PGN,
		STATE_SOURCE,
		STATE_DATA,
		STATE_GARBAGE
	} state = STATE_IDLE;

	/*
	//Filter the packet stream separately from the timeline stream
	auto& srcPackets = dynamic_cast<PacketDecoder*>(GetInput(0).m_channel)->GetPackets();
	for(auto p : srcPackets)
	{
		if(p->m_headers["Source"] == starget)
		{
			auto np = new Packet;
			*np = *p;
			m_packets.push_back(np);
		}
	}
	*/

	//In-progress transport layer packets
	[[maybe_unused]] Packet* workingPacketsBySourceAddress[256];
	for(int i=0; i<256; i++)
		workingPacketsBySourceAddress[i] = {nullptr};

	//TODO: generate packet output for non-transport-coded packets

	//Process the J1939 packet stream
	size_t nstart = 0;
	uint32_t currentPGN = 0;
	uint8_t currentSrc = 0;
	uint8_t currentDst = 0;
	vector<uint8_t> currentPacketBytes;
	int64_t currentPacketStart = 0;
	for(size_t i=0; i<len; i++)
	{
		auto& s = din->m_samples[i];
		int64_t tstart = din->m_offsets[i] * din->m_timescale + din->m_triggerPhase;
		//int64_t tend = tstart + din->m_durations[i] * din->m_timescale;

		switch(state)
		{
			case STATE_IDLE:
				break;

			//Expect a PGN, if we get anything else drop the packet
			case STATE_PGN:
				if(s.m_stype == J1939PDUSymbol::TYPE_PGN)
				{
					currentPGN = s.m_data;

					//Copy the sample to the output
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(din->m_samples[i]);

					//Wait for the addresses
					state = STATE_SOURCE;
				}
				else
				{
					cap->m_offsets.resize(nstart);
					cap->m_durations.resize(nstart);
					cap->m_samples.resize(nstart);
					state = STATE_GARBAGE;
				}
				break;


			case STATE_SOURCE:
				if(s.m_stype == J1939PDUSymbol::TYPE_DEST)
				{
					//Copy the sample to the output
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(din->m_samples[i]);

					//Still waiting for source address
					currentDst = s.m_data;
				}
				else if(s.m_stype == J1939PDUSymbol::TYPE_SRC)
				{
					//Copy the sample to the output
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(din->m_samples[i]);

					currentSrc = s.m_data;

					//Process data
					state = STATE_DATA;
				}
				else
				{
					cap->m_offsets.resize(nstart);
					cap->m_durations.resize(nstart);
					cap->m_samples.resize(nstart);
					state = STATE_GARBAGE;
				}
				break;

			case STATE_DATA:

				if(s.m_stype == J1939PDUSymbol::TYPE_DATA)
				{
					//Copy the sample to the output
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(din->m_samples[i]);

					//Save the byte
					currentPacketBytes.push_back(s.m_data);

					//Handle transport layer
					if( (currentPGN == 60416) && (currentPacketBytes.size() == 8) )
					{
						//BAM TP.CM
						if(currentPacketBytes[0] == 32)
						{
							//Start a new output packet
							auto pack = new Packet;
							pack->m_offset = currentPacketStart;
							pack->m_len = 0;
							m_packets.push_back(pack);

							uint16_t plen = currentPacketBytes[1] | (currentPacketBytes[2] << 8);
							uint32_t pgn =
								currentPacketBytes[5] |
								(currentPacketBytes[6] << 8) |
								(currentPacketBytes[7] << 16);
							pack->m_headers["Length"] = to_string(plen);
							pack->m_headers["PGN"] = to_string(pgn);
							pack->m_headers["Format"] = to_string(currentPacketBytes[6]);
							if(currentPacketBytes[6] >= 240)
								pack->m_headers["Group ext"] = to_string(currentPacketBytes[5]);
							pack->m_headers["Source"] = to_string(currentSrc);
							pack->m_headers["Dest"] = to_string(currentDst);	//should always be 0xff
							workingPacketsBySourceAddress[currentSrc] = pack;

							pack->m_headers["Type"] = "BAM TP";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
						}

						//TODO: decode CM
						else
						{
							LogWarning("Don't know how to decode PGN 60416 other than BAM TP.CM\n");
						}
					}

					if(currentPGN == 60160)
					{
						//TODO: detect end-of-packet and synthesize time domain packet events
						auto pack = workingPacketsBySourceAddress[currentSrc];
						if(pack)
						{
							//for now assume sequence number is correct and packet arrived in order
							if(currentPacketBytes.size() > 1)
								pack->m_data.push_back(s.m_data);
						}
					}
				}
				break;

			default:
				break;
		}

		//When we see a PRI start a new packet and save the info from it
		if(s.m_stype == J1939PDUSymbol::TYPE_PRI)
		{
			//Copy the sample to the output
			nstart = cap->m_samples.size();
			cap->m_offsets.push_back(din->m_offsets[i]);
			cap->m_durations.push_back(din->m_durations[i]);
			cap->m_samples.push_back(din->m_samples[i]);

			//Reset state
			currentPacketBytes.clear();
			currentPGN = 0;
			currentPacketStart = tstart;

			//Wait for the PGN
			state = STATE_PGN;
		}

	}

	//Done updating
	cap->MarkModifiedFromCpu();
}
