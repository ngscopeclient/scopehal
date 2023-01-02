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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of PCIeLinkTrainingDecoder
 */
#include "../scopehal/scopehal.h"
#include "../scopehal/Filter.h"
#include "IBM8b10bDecoder.h"
#include "PCIeLinkTrainingDecoder.h"


using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIeLinkTrainingDecoder::PCIeLinkTrainingDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	ClearStreams();
	AddProtocolStream("packets");
	AddProtocolStream("states");
	CreateInput("lane");
}

PCIeLinkTrainingDecoder::~PCIeLinkTrainingDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIeLinkTrainingDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<IBM8b10bWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string PCIeLinkTrainingDecoder::GetProtocolName()
{
	return "PCIe Link Training";
}

vector<string> PCIeLinkTrainingDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Link");
	ret.push_back("Lane");
	ret.push_back("Num FTS");
	ret.push_back("Rates");
	ret.push_back("Flags");
	return ret;
}

bool PCIeLinkTrainingDecoder::GetShowDataColumn()
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIeLinkTrainingDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();

	//Create the main capture
	//Output is time aligned with the input
	auto cap = new PCIeLinkTrainingWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->PrepareForCpuAccess();

	//Second output capture for states
	auto scap = new PCIeLTSSMWaveform;
	scap->m_timescale = din->m_timescale;
	scap->m_startTimestamp = din->m_startTimestamp;
	scap->m_startFemtoseconds = din->m_startFemtoseconds;
	scap->m_triggerPhase = din->m_triggerPhase;
	scap->PrepareForCpuAccess();

	//Find the first comma in our lane and use as a starting point
	size_t len = din->m_samples.size();
	size_t end = len - 15;
	size_t i = 0;
	for(; i<len-3; i++)
	{
		if(din->m_samples[i].m_control && (din->m_samples[i].m_data == 0xbc) )
			break;
	}

	PCIeLTSSMSymbol::SymbolType lstate = PCIeLTSSMSymbol::TYPE_DETECT;

	//Add a Detect symbol from time zero to the first 8b10b symbol
	scap->m_offsets.push_back(0);
	scap->m_durations.push_back(0);
	scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_DETECT));

	//Main decode loop
	for(; i<end; i++)
	{
		//If we see a K28.3 we're entering electrical idle
		if(din->m_samples[i].m_control && (din->m_samples[i].m_data == 0x7c) )
		{
			//If in Recovery.Speed transition to Recovery.RcvrLock
			if(lstate == PCIeLTSSMSymbol::TYPE_RECOVERY_SPEED)
			{
				//Extend previous state to start of this symbol
				size_t nout = scap->m_offsets.size() - 1;
				scap->m_durations[nout] = din->m_offsets[i] - scap->m_offsets[nout];

				//Enter Recovery.RcvrLock state
				lstate = PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRLOCK;
				scap->m_offsets.push_back(din->m_offsets[i]);
				scap->m_durations.push_back(din->m_durations[i]);
				scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRLOCK));
			}
			continue;
		}

		//If we see a K28.7 we're exiting electrical idle
		if(din->m_samples[i].m_control && (din->m_samples[i].m_data == 0xfc) )
		{
			//Skip all subsequent K28.7 symbols
			while(i < end)
			{
				if(din->m_samples[i].m_control && (din->m_samples[i].m_data == 0xfc) )
					i++;
				else
					break;
			}

			//Next symbol is expected to be a D10.2. If so, skip it.
			if(!din->m_samples[i].m_control && (din->m_samples[i].m_data == 0x4a) )
				continue;
		}

		//All training sets start with a comma. If we see anything else, ignore it.
		if(!din->m_samples[i].m_control || (din->m_samples[i].m_data != 0xbc) )
		{
			//If in Configuration or RcvrConfig state, this means we're now in L0
			if( (lstate == PCIeLTSSMSymbol::TYPE_CONFIGURATION) ||
				(lstate == PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRCFG) )
			{
				lstate = PCIeLTSSMSymbol::TYPE_L0;

				scap->m_offsets.push_back(din->m_offsets[i]);
				scap->m_durations.push_back(din->m_durations[i]);
				scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_L0));
			}

			//if in L0 state, extend
			if(lstate == PCIeLTSSMSymbol::TYPE_L0)
			{
				size_t nout = scap->m_offsets.size() - 1;
				scap->m_durations[nout] = din->m_offsets[i] + din->m_durations[i] - scap->m_offsets[nout];
			}

			continue;
		}

		//Discard SKIP ordered sets (K28.5 K28.0 K28.0 K28.0)
		if(i+3 < end)
		{
			if(
				(din->m_samples[i+1].m_control && (din->m_samples[i+1].m_data == 0x1c) ) &&
				(din->m_samples[i+2].m_control && (din->m_samples[i+2].m_data == 0x1c) ) &&
				(din->m_samples[i+3].m_control && (din->m_samples[i+3].m_data == 0x1c) ) )
			{
				i += 3;
				continue;
			}
		}

		//Link ID must be K23.7 PAD or a D character
		//If we see any other K characters there, reject it
		if( din->m_samples[i+1].m_control && (din->m_samples[i+1].m_data != 0xf7) )
			continue;

		//Lane ID must be K23.7 or data character with value <= 31
		if(din->m_samples[i+2].m_control && din->m_samples[i+2].m_data != 0xf7)
			continue;
		if(!din->m_samples[i+2].m_control && (din->m_samples[i+2].m_data > 31) )
			continue;

		//Check if it's a TS1 or TS2 set
		bool hitTS1 = true;
		bool hitTS2 = true;

		for(size_t k=0; k<6; k++)
		{
			if(din->m_samples[i+10+k].m_control)
			{
				hitTS1 = false;
				hitTS2 = false;
				break;
			}

			if(din->m_samples[i+10+k].m_data != 0x4a)
				hitTS1 = false;
			if(din->m_samples[i+10+k].m_data != 0x45)
				hitTS2 = false;
		}

		//If not a training set, skip it
		if(!hitTS1 && !hitTS2)
			continue;

		Packet* pack = new Packet;
		m_packets.push_back(pack);
		pack->m_offset = din->m_offsets[i] * din->m_timescale + din->m_triggerPhase;
		pack->m_len = (din->m_offsets[i+15] + din->m_durations[i+15] - din->m_offsets[i]) * din->m_timescale;

		pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];

		//Add header symbol
		if(hitTS1)
		{
			cap->m_offsets.push_back(din->m_offsets[i]);
			cap->m_durations.push_back(din->m_durations[i]);
			cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_HEADER, 1));

			pack->m_headers["Type"] = "TS1";
		}
		else
		{
			cap->m_offsets.push_back(din->m_offsets[i]);
			cap->m_durations.push_back(din->m_durations[i]);
			cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_HEADER, 2));

			pack->m_headers["Type"] = "TS2";
		}

		//Link number
		cap->m_offsets.push_back(din->m_offsets[i+1]);
		cap->m_durations.push_back(din->m_durations[i+1]);
		auto linkid = din->m_samples[i+1].m_data;
		cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_LINK_NUMBER, linkid));
		if(linkid == 0xf7)
			pack->m_headers["Link"] = "Unassigned";
		else
			pack->m_headers["Link"] = to_string(linkid);

		//Lane number
		cap->m_offsets.push_back(din->m_offsets[i+2]);
		cap->m_durations.push_back(din->m_durations[i+2]);
		auto laneid = din->m_samples[i+2].m_data;
		cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_LANE_NUMBER, laneid));
		if(laneid == 0xf7)
			pack->m_headers["Lane"] = "Unassigned";
		else
			pack->m_headers["Lane"] = to_string(laneid);

		//Num FTS
		auto numFTS = din->m_samples[i+3].m_data;
		cap->m_offsets.push_back(din->m_offsets[i+3]);
		cap->m_durations.push_back(din->m_durations[i+3]);
		cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_NUM_FTS, numFTS));
		pack->m_headers["Num FTS"] = to_string(numFTS);

		//Rate ID
		cap->m_offsets.push_back(din->m_offsets[i+4]);
		cap->m_durations.push_back(din->m_durations[i+4]);
		auto rates = din->m_samples[i+4].m_data;
		cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_RATE_ID, rates));
		string srates;
		if(rates & 2)
			srates += "2.5G ";
		if(rates & 4)
			srates += "5G ";
		if(rates & 8)
			srates += "8G ";
		if(rates & 0x80)
		{
			srates += "SpeedChange";
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
		}
		pack->m_headers["Rates"] = srates;

		//Training control
		cap->m_offsets.push_back(din->m_offsets[i+5]);
		cap->m_durations.push_back(din->m_durations[i+5]);
		auto flags = din->m_samples[i+5].m_data;
		cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_TRAIN_CTL, flags));
		string sflags;
		if(flags & 1)
			sflags += "Hot reset ";
		if(flags & 2)
			sflags += "Disable link ";
		if(flags & 4)
			sflags += "Loopback ";
		if(flags & 8)
			sflags += "Disable scrambling ";
		if(flags & 0x10)
			sflags += "Compliance Receive ";
		if(sflags == "")
			sflags = "None";
		pack->m_headers["Flags"] = sflags;

		//TS ID
		cap->m_offsets.push_back(din->m_offsets[i+6]);
		cap->m_durations.push_back(din->m_offsets[i+15] + din->m_durations[i+15] - din->m_offsets[i+6]);
		cap->m_samples.push_back(PCIeLinkTrainingSymbol(PCIeLinkTrainingSymbol::TYPE_TS_ID,
			din->m_samples[i+6].m_data));

		switch(lstate)
		{
			//If we're in L0 and see a training set, we're now in recovery
			case PCIeLTSSMSymbol::TYPE_L0:
				lstate = PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRLOCK;

				scap->m_offsets.push_back(din->m_offsets[i]);
				scap->m_durations.push_back(din->m_durations[i]);
				scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRLOCK));
				break;

			case PCIeLTSSMSymbol::TYPE_DETECT:

				//Add a Detect symbol from time zero to the first TS1
				if(hitTS1 && (din->m_samples[i+1].m_data == 0xf7) )
				{
					size_t nout = scap->m_offsets.size() - 1;
					scap->m_durations[nout] = din->m_offsets[i] - scap->m_offsets[nout];

					lstate = PCIeLTSSMSymbol::TYPE_POLLING_ACTIVE;

					scap->m_offsets.push_back(din->m_offsets[i]);
					scap->m_durations.push_back(din->m_durations[i]);
					scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_POLLING_ACTIVE));
				}
				break;

			case PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRLOCK:
				//If we have the speed change bit, we're now in Recovery.Speed
				if(rates & 0x80)
				{
					lstate = PCIeLTSSMSymbol::TYPE_RECOVERY_SPEED;

					scap->m_offsets.push_back(din->m_offsets[i]);
					scap->m_durations.push_back(din->m_durations[i]);
					scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_RECOVERY_SPEED));
				}

				//Otherwise, got to Recovery.RcvrCfg
				else
				{
					lstate = PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRCFG;

					scap->m_offsets.push_back(din->m_offsets[i]);
					scap->m_durations.push_back(din->m_durations[i]);
					scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRCFG));
				}
				break;

			case PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRCFG:
				{
					//Extend
					size_t nout = scap->m_offsets.size() - 1;
					scap->m_durations[nout] = din->m_offsets[i] + din->m_durations[i] - scap->m_offsets[nout];
				}
				break;

			case PCIeLTSSMSymbol::TYPE_RECOVERY_SPEED:
				break;

			case PCIeLTSSMSymbol::TYPE_POLLING_ACTIVE:

				//If we're sending TS2s we're in Polling.Configuration now
				if(hitTS2)
				{
					lstate = PCIeLTSSMSymbol::TYPE_POLLING_CONFIGURATION;

					scap->m_offsets.push_back(din->m_offsets[i]);
					scap->m_durations.push_back(din->m_durations[i]);
					scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_POLLING_CONFIGURATION));
				}

				//Extend the current state
				else
				{
					size_t nout = scap->m_offsets.size() - 1;
					scap->m_durations[nout] = din->m_offsets[i] + din->m_durations[i] - scap->m_offsets[nout];
				}

				break;

			case PCIeLTSSMSymbol::TYPE_POLLING_CONFIGURATION:

				//If we're sending TS1s we're in Configuration now
				if(hitTS1)
				{
					lstate = PCIeLTSSMSymbol::TYPE_CONFIGURATION;

					scap->m_offsets.push_back(din->m_offsets[i]);
					scap->m_durations.push_back(din->m_durations[i]);
					scap->m_samples.push_back(PCIeLTSSMSymbol(PCIeLTSSMSymbol::TYPE_CONFIGURATION));
				}

				//Still extending
				else
				{
					size_t nout = scap->m_offsets.size() - 1;
					scap->m_durations[nout] = din->m_offsets[i] + din->m_durations[i] - scap->m_offsets[nout];
				}

				break;

			case PCIeLTSSMSymbol::TYPE_CONFIGURATION:
				{
					//Extend
					size_t nout = scap->m_offsets.size() - 1;
					scap->m_durations[nout] = din->m_offsets[i] + din->m_durations[i] - scap->m_offsets[nout];
				}

			default:
				break;
		}

		//Skip the rest of the set
		i += 15;
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();

	SetData(scap, 1);
	scap->MarkModifiedFromCpu();
}

bool PCIeLinkTrainingDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//If all headers are the same, it's mergeable
	if(first->m_headers == next->m_headers)
		return true;

	return false;
}

Packet* PCIeLinkTrainingDecoder::CreateMergedHeader(Packet* pack, size_t i)
{
	//Copy everything
	Packet* ret = new Packet;
	ret->m_offset = pack->m_offset;
	ret->m_len = pack->m_len;
	ret->m_headers = pack->m_headers;
	ret->m_displayBackgroundColor = pack->m_displayBackgroundColor;

	//Extend length
	for(; i<m_packets.size(); i++)
	{
		if(CanMerge(pack, nullptr, m_packets[i]))
			ret->m_len = (m_packets[i]->m_offset + m_packets[i]->m_len) - pack->m_offset;
		else
			break;
	}

	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PCIeLinkTrainingWaveform

string PCIeLinkTrainingWaveform::GetColor(size_t i)
{
	const PCIeLinkTrainingSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeLinkTrainingSymbol::TYPE_HEADER:
		case PCIeLinkTrainingSymbol::TYPE_NUM_FTS:
		case PCIeLinkTrainingSymbol::TYPE_RATE_ID:
		case PCIeLinkTrainingSymbol::TYPE_TRAIN_CTL:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case PCIeLinkTrainingSymbol::TYPE_TS_ID:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case PCIeLinkTrainingSymbol::TYPE_LINK_NUMBER:
		case PCIeLinkTrainingSymbol::TYPE_LANE_NUMBER:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case PCIeLinkTrainingSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string PCIeLinkTrainingWaveform::GetText(size_t i)
{
	char tmp[32];

	const PCIeLinkTrainingSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeLinkTrainingSymbol::TYPE_HEADER:
			if(s.m_data == 1)
				return "TS1";
			else
				return "TS2";

		case PCIeLinkTrainingSymbol::TYPE_TS_ID:
			if(s.m_data == 0x4a)
				return "TS1";
			else
				return "TS2";

		case PCIeLinkTrainingSymbol::TYPE_LINK_NUMBER:
			if(s.m_data == 0xf7)
				return "Link: Unassigned";
			else
			{
				snprintf(tmp, sizeof(tmp), "Link: %d", s.m_data);
				return tmp;
			}

		case PCIeLinkTrainingSymbol::TYPE_LANE_NUMBER:
			if(s.m_data == 0xf7)
				return "Lane: Unassigned";
			else
			{
				snprintf(tmp, sizeof(tmp), "Lane: %d", s.m_data);
				return tmp;
			}

		case PCIeLinkTrainingSymbol::TYPE_NUM_FTS:
			snprintf(tmp, sizeof(tmp), "Need %d FTS", s.m_data);
			return tmp;

		case PCIeLinkTrainingSymbol::TYPE_TRAIN_CTL:
			{
				string ret;
				if(s.m_data & 1)
					ret += "Hot reset ";
				if(s.m_data & 2)
					ret += "Disable link ";
				if(s.m_data & 4)
					ret += "Loopback ";
				if(s.m_data & 8)
					ret += "Disable scrambling ";
				if(s.m_data & 0x10)
					ret += "Compliance Receive ";

				if(ret == "")
					ret = "No flags";

				return ret;
			}
			break;

		case PCIeLinkTrainingSymbol::TYPE_RATE_ID:
			{
				string ret;
				if(s.m_data & 2)
					ret += "2.5 GT/s ";
				if(s.m_data & 4)
					ret += "5 GT/s ";
				if(s.m_data & 8)
					ret += "8 GT/s ";
				if(s.m_data & 0x80)
					ret += "Speed change";
				return ret;
			}

		case PCIeLinkTrainingSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PCIeLTSSMWaveform

string PCIeLTSSMWaveform::GetColor(size_t i)
{
	const PCIeLTSSMSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeLTSSMSymbol::TYPE_DETECT:
			return StandardColors::colors[StandardColors::COLOR_IDLE];

		case PCIeLTSSMSymbol::TYPE_POLLING_ACTIVE:
		case PCIeLTSSMSymbol::TYPE_POLLING_CONFIGURATION:
		case PCIeLTSSMSymbol::TYPE_CONFIGURATION:
		case PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRLOCK:
		case PCIeLTSSMSymbol::TYPE_RECOVERY_SPEED:
		case PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRCFG:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case PCIeLTSSMSymbol::TYPE_L0:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string PCIeLTSSMWaveform::GetText(size_t i)
{
	const PCIeLTSSMSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeLTSSMSymbol::TYPE_DETECT:
			return "Detect";

		case PCIeLTSSMSymbol::TYPE_POLLING_ACTIVE:
			return "Polling.Active";

		case PCIeLTSSMSymbol::TYPE_POLLING_CONFIGURATION:
			return "Polling.Configuration";

		case PCIeLTSSMSymbol::TYPE_CONFIGURATION:
			return "Configuration";

		case PCIeLTSSMSymbol::TYPE_L0:
			return "L0";

		case PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRLOCK:
			return "Recovery.RcvrLock";
		case PCIeLTSSMSymbol::TYPE_RECOVERY_SPEED:
			return "Recovery.Speed";
		case PCIeLTSSMSymbol::TYPE_RECOVERY_RCVRCFG:
			return "Recovery.RcvrCfg";

		default:
			return "ERROR";
	}
}
