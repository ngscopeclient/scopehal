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
#include "J1939PDUDecoder.h"
#include "J1939BitmaskDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

J1939BitmaskDecoder::J1939BitmaskDecoder(const string& color)
	: Filter(color, CAT_BUS)
	, m_initValue("Initial Value")
	, m_pgn("PGN")
	, m_bitmask("Pattern Bitmask")
	, m_pattern("Pattern Target")
{
	AddDigitalStream("data");

	CreateInput("j1939");

	m_parameters[m_initValue] = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_initValue].SetIntVal(0);

	m_parameters[m_pgn] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_pgn].SetIntVal(0);

	m_parameters[m_bitmask] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_parameters[m_bitmask].SetIntVal(0);

	m_parameters[m_pattern] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_parameters[m_pattern].SetIntVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool J1939BitmaskDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<J1939PDUWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string J1939BitmaskDecoder::GetProtocolName()
{
	return "J1939 Bitmask";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void J1939BitmaskDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	auto din = dynamic_cast<J1939PDUWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}
	auto len = din->size();

	//Make output waveform
	auto cap = SetupEmptySparseDigitalOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_DATA,
	} state = STATE_IDLE;

	//Initial sample at time zero
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(0);
	cap->m_samples.push_back(static_cast<bool>(m_parameters[m_initValue].GetIntVal()));

	int64_t mask = m_parameters[m_bitmask].GetIntVal();
	int64_t pattern = m_parameters[m_pattern].GetIntVal();
	auto targetaddr = m_parameters[m_pgn].GetIntVal() ;

	//TODO: support >8 byte packetds
	int64_t framestart = 0;
	int64_t payload = 0;
	for(size_t i=0; i<len; i++)
	{
		auto& s = din->m_samples[i];

		switch(state)
		{
			//Look for a PGN (ignore anything else)
			case STATE_IDLE:
				if(s.m_stype == J1939PDUSymbol::TYPE_PGN)
				{
					//ID match?
					if(targetaddr == s.m_data)
					{
						framestart = din->m_offsets[i] * din->m_timescale;
						payload = 0;
						state = STATE_DATA;
					}

					//otherwise ignore the frame, not interesting
				}
				break;

			//Read the actual data bytes, MSB first
			case STATE_DATA:
				if(s.m_stype == J1939PDUSymbol::TYPE_DATA)
				{
					//Grab the data byte
					payload = (payload << 8) | s.m_data;

					//Extend the previous sample to the start of this frame
					size_t nlast = cap->m_offsets.size() - 1;
					cap->m_durations[nlast] = framestart - cap->m_offsets[nlast];
				}

				//Starting a new frame? This one is over, add the new sample
				else if(s.m_stype == J1939PDUSymbol::TYPE_PRI)
				{
					//Check the bitmask and add a new sample
					cap->m_offsets.push_back(framestart);
					cap->m_durations.push_back(0);

					if( (payload & mask) == pattern )
						cap->m_samples.push_back(true);
					else
						cap->m_samples.push_back(false);

					LogDebug("payload = %016lx\n", payload);
					state = STATE_IDLE;
				}

				//Ignore anything else
				break;

			default:
				break;
		}

		//If we see a SOF previous frame was truncated, reset
		if(s.m_stype == J1939PDUSymbol::TYPE_PRI)
			state = STATE_IDLE;
	}

	//Extend the last sample to the end of the capture
	size_t nlast = cap->m_offsets.size() - 1;
	cap->m_durations[nlast] = (din->m_offsets[len-1] * din->m_timescale) - cap->m_offsets[nlast];

	//Add three padding samples (do we still have this rendering bug??)
	int64_t tlast = cap->m_offsets[nlast];
	bool vlast = cap->m_samples[nlast];
	for(size_t i=0; i<2; i++)
	{
		cap->m_offsets.push_back(tlast + i);
		cap->m_durations.push_back(1);
		cap->m_samples.push_back(vlast);
	}

	//Done updating
	cap->MarkModifiedFromCpu();
}
