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
#include "J1939SourceMatchFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

J1939SourceMatchFilter::J1939SourceMatchFilter(const string& color)
	: PacketDecoder(color, CAT_BUS)
	, m_sourceAddr("Source address")
{
	CreateInput("j1939");

	m_parameters[m_sourceAddr] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_sourceAddr].SetIntVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool J1939SourceMatchFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<J1939PDUWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

vector<string> J1939SourceMatchFilter::GetHeaders()
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

string J1939SourceMatchFilter::GetProtocolName()
{
	return "J1939 Source Match";
}

Filter::DataLocation J1939SourceMatchFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void J1939SourceMatchFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("J1939SourceMatchFilter::Refresh");
	#endif

	ClearPackets();

	//Make sure we've got valid inputs
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
	din->PrepareForCpuAccess();
	auto len = din->size();

	//Create the capture
	auto cap = SetupEmptyWaveform<J1939PDUWaveform>(din, 0);
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_PGN,
		STATE_SOURCE,
		STATE_DATA,
		STATE_GARBAGE
	} state = STATE_IDLE;

	//Find the target
	auto target = m_parameters[m_sourceAddr].GetIntVal();
	auto starget = to_string(target);

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

	//Process the J1939 packet stream
	size_t nstart = 0;
	for(size_t i=0; i<len; i++)
	{
		auto& s = din->m_samples[i];

		switch(state)
		{
			case STATE_IDLE:
				break;

			//Expect a PGN, if we get anything else drop the packet
			case STATE_PGN:
				if(s.m_stype == J1939PDUSymbol::TYPE_PGN)
				{
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
				}
				else if( (s.m_stype == J1939PDUSymbol::TYPE_SRC) && (s.m_data == target) )
				{
					//Copy the sample to the output
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(din->m_samples[i]);

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

			//Wait for the PGN
			state = STATE_PGN;
		}

	}

	//Done updating
	cap->MarkModifiedFromCpu();
}
