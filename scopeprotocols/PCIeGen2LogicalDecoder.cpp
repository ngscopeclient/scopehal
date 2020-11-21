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
	@brief Implementation of PCIeGen2LogicalDecoder
 */
#include "../scopehal/scopehal.h"
#include "../scopehal/Filter.h"
#include "IBM8b10bDecoder.h"
#include "PCIeGen2LogicalDecoder.h"


using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIeGen2LogicalDecoder::PCIeGen2LogicalDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	//Set up channels
	CreateInput("data");
}

PCIeGen2LogicalDecoder::~PCIeGen2LogicalDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIeGen2LogicalDecoder::NeedsConfig()
{
	//No config for now (might need it when we have multiple lanes etc)
	return false;
}

bool PCIeGen2LogicalDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<IBM8b10bWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string PCIeGen2LogicalDecoder::GetProtocolName()
{
	return "PCIe Gen 1/2 Logical";
}

void PCIeGen2LogicalDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "PCIE2Logical(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIeGen2LogicalDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}
	auto data = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(0));

	//Create the capture
	auto cap = new PCIeLogicalWaveform;
	cap->m_timescale = data->m_timescale;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startPicoseconds = data->m_startPicoseconds;

	size_t len = data->m_samples.size();
	bool synced = false;
	uint16_t scrambler = 0;
	bool in_packet = false;
	for(size_t i=0; i<len; i++)
	{
		auto sym = data->m_samples[i];
		int64_t off = data->m_offsets[i];
		int64_t dur = data->m_durations[i];
		int64_t end = off + dur;

		//Figure out previous symbol type
		size_t outlen = cap->m_samples.size();
		size_t ilast = outlen - 1;
		bool last_was_skip = false;
		bool last_was_idle = false;
		bool last_was_no_scramble = false;
		if(outlen)
		{
			last_was_skip = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_SKIP);
			last_was_idle = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_LOGICAL_IDLE);
			last_was_no_scramble = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_NO_SCRAMBLER);
		}

		//Update the scrambler UNLESS we have a SKP character K28.0 (k.1c)
		uint8_t scrambler_out = 0;
		if(sym.m_control && (sym.m_data == 0x1c) )
		{}
		else
		{
			for(int j=0; j<8; j++)
			{
				bool b = (scrambler & 0x8000) ? true : false;
				scrambler_out >>= 1;

				if(b)
				{
					scrambler_out |= 0x80;
					scrambler ^= 0x1c;
				}
				scrambler = (scrambler << 1) | b;
			}
		}

		//Control characters
		if(sym.m_control)
		{
			switch(sym.m_data)
			{
				//K28.5 COM
				case 0xbc:
					scrambler = 0xffff;
					synced = true;
					break;

				//K28.0 SKP
				case 0x1c:
					{
						//Prefer to extend an existing symbol
						if(last_was_skip)
							cap->m_durations[ilast] = end - cap->m_offsets[outlen-1];

						//Nope, need to make a new symbol
						else
						{
							//If we had a gap from a COM character, stretch rearwards into it
							int64_t start = off;
							if(outlen)
								start = cap->m_offsets[ilast] + cap->m_durations[ilast];

							cap->m_offsets.push_back(start);
							cap->m_durations.push_back(end - start);
							cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_SKIP));
						}
					}
					break;

				//K28.2 SDP
				case 0x5c:
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_START_DLLP));
					in_packet = true;
					break;

				//K27.7 STP
				case 0xfb:
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_START_TLP));
					in_packet = true;
					break;

				//K29.7 END
				case 0xfd:
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_END));
					in_packet = false;
					break;

			}
		}

		//Upper layer payload
		else if(synced)
		{
			//Payload data
			if(in_packet)
			{
				cap->m_offsets.push_back(off);
				cap->m_durations.push_back(dur);
				cap->m_samples.push_back(PCIeLogicalSymbol(
					PCIeLogicalSymbol::TYPE_PAYLOAD_DATA, sym.m_data ^ scrambler_out));
			}

			//Logical idle
			else if( (sym.m_data ^ scrambler_out) == 0)
			{
				//Prefer to extend an existing symbol
				if(last_was_idle)
					cap->m_durations[ilast] = end - cap->m_offsets[ilast];

				//Nope, need to make a new symbol
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_LOGICAL_IDLE));
				}
			}

			//Garbage: data not inside packet framing
			else
			{
				cap->m_offsets.push_back(off);
				cap->m_durations.push_back(dur);
				cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_ERROR));
			}
		}

		//If we get a Dx.x character and aren't synced, create a "no scrambler" symbol on the output
		else
		{
			//Prefer to extend an existing symbol
			if(last_was_no_scramble)
				cap->m_durations[ilast] = end - cap->m_offsets[ilast];

			else
			{
				cap->m_offsets.push_back(off);
				cap->m_durations.push_back(end - off);
				cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_NO_SCRAMBLER));
			}
		}
	}

	SetData(cap, 0);
}

Gdk::Color PCIeGen2LogicalDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<PCIeLogicalWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const PCIeLogicalSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case PCIeLogicalSymbol::TYPE_NO_SCRAMBLER:
			case PCIeLogicalSymbol::TYPE_LOGICAL_IDLE:
			case PCIeLogicalSymbol::TYPE_SKIP:
				return m_standardColors[COLOR_IDLE];

			case PCIeLogicalSymbol::TYPE_START_TLP:
			case PCIeLogicalSymbol::TYPE_START_DLLP:
			case PCIeLogicalSymbol::TYPE_END:
				return m_standardColors[COLOR_CONTROL];

			case PCIeLogicalSymbol::TYPE_PAYLOAD_DATA:
				return m_standardColors[COLOR_DATA];

			case PCIeLogicalSymbol::TYPE_END_BAD:
			case PCIeLogicalSymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string PCIeGen2LogicalDecoder::GetText(int i)
{
	char tmp[16];

	auto capture = dynamic_cast<PCIeLogicalWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const PCIeLogicalSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case PCIeLogicalSymbol::TYPE_NO_SCRAMBLER:
				return "Scrambler desynced";

			case PCIeLogicalSymbol::TYPE_LOGICAL_IDLE:
				return "Logical Idle";

			case PCIeLogicalSymbol::TYPE_SKIP:
				return "Skip";

			case PCIeLogicalSymbol::TYPE_START_TLP:
				return "TLP";

			case PCIeLogicalSymbol::TYPE_START_DLLP:
				return "DLLP";

			case PCIeLogicalSymbol::TYPE_END:
				return "End";

			case PCIeLogicalSymbol::TYPE_PAYLOAD_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				return tmp;

			case PCIeLogicalSymbol::TYPE_END_BAD:
				return "End Bad";

			case PCIeLogicalSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}
