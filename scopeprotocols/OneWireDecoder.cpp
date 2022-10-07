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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of OneWireDecoder
 */

#include "../scopehal/scopehal.h"
#include "OneWireDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OneWireDecoder::OneWireDecoder(const string& color)
	: Filter(color, CAT_BUS)
{
	AddProtocolStream("data");
	CreateInput("data");
}

OneWireDecoder::~OneWireDecoder()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool OneWireDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;
	return false;
}

string OneWireDecoder::GetProtocolName()
{
	return "1-Wire";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void OneWireDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Set up the output waveform
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();

	auto sdin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto udin = dynamic_cast<UniformDigitalWaveform*>(din);

	auto cap = new OneWireWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	//Get timestamps and durations of all low-going pulses
	vector<int64_t> starts;
	vector<int64_t> lens;
	size_t len = din->size();
	bool last = true;
	int64_t tstart = 0;
	for(size_t i=0; i<len; i++)
	{
		bool v = GetValue(sdin, udin, i);

		//High? See if a pulse ended
		if(v)
		{
			if(!last)
			{
				starts.push_back(tstart);
				lens.push_back(::GetOffset(sdin, udin, i) + GetDuration(sdin, udin, i) - tstart);
			}
		}

		//Low? See if a pulse started
		else
		{
			if(last)
				tstart = ::GetOffset(sdin, udin, i);
		}

		last = v;
	}

	enum
	{
		STATE_IDLE,
		STATE_DETECT,
		STATE_DATA
	} state = STATE_IDLE;

	len = starts.size();
	int bitcount = 0;
	uint8_t current_byte = 0;
	for(size_t i=0; i<len; i++)
	{
		//Get the length of this pulse in us
		float pulselen = lens[i] * din->m_timescale * 1e-9;

		int64_t tend = starts[i] + lens[i];

		//Time since end of last pulse in us
		float delta = starts[i];
		if(i >= 1)
			delta -= (starts[i-1] + lens[i-1]);
		delta *= 1e-9;

		switch(state)
		{
			//Wait for a reset pulse
			case STATE_IDLE:

				//If pulse length is only a little bit too short, flag it as an error but keep decoding
				if( (pulselen < 480) && (pulselen > 450) )
				{
					cap->m_offsets.push_back(starts[i]);
					cap->m_durations.push_back(lens[i]);
					cap->m_samples.push_back(OneWireSymbol(OneWireSymbol::TYPE_RESET, 1));

					state = STATE_DETECT;
				}

				//Proper reset
				else if(pulselen >= 480)
				{
					cap->m_offsets.push_back(starts[i]);
					cap->m_durations.push_back(lens[i]);
					cap->m_samples.push_back(OneWireSymbol(OneWireSymbol::TYPE_RESET, 0));

					state = STATE_DETECT;
				}

				//Garbage
				else
				{
					cap->m_offsets.push_back(starts[i]);
					cap->m_durations.push_back(lens[i]);
					cap->m_samples.push_back(OneWireSymbol(OneWireSymbol::TYPE_ERROR, 0));
				}

				break;

			//Expect a presence detect pulse, at least 60us long, within 60us of the reset
			case STATE_DETECT:

				//If the next pulse was >60us after the previous one, restart from this pulse.
				if(delta > 60)
				{
					i --;
					state = STATE_IDLE;
					continue;
				}

				//If not at least 60us long, report error and bail
				if(pulselen < 60)
				{
					cap->m_offsets.push_back(starts[i]);
					cap->m_durations.push_back(lens[i]);
					cap->m_samples.push_back(OneWireSymbol(OneWireSymbol::TYPE_ERROR, 0));

					state = STATE_IDLE;
				}

				//Valid presence detect
				else
				{
					cap->m_offsets.push_back(starts[i]);
					cap->m_durations.push_back(lens[i]);
					cap->m_samples.push_back(OneWireSymbol(OneWireSymbol::TYPE_PRESENCE, 0));

					state = STATE_DATA;

					bitcount = 0;
					current_byte = 0;
				}

				break;

			//Read a byte of data (LSB first)
			case STATE_DATA:

				if(bitcount == 0)
					tstart = starts[i];

				//If we see a reset pulse, abort and go back to idle
				if(pulselen > 450)
				{
					i --;
					state = STATE_IDLE;
					continue;
				}

				//Read the bit if it's valid
				if(pulselen < 15)
				{
					current_byte >>= 1;
					current_byte |= 0x80;
				}
				else if(pulselen > 60)
					current_byte >>= 1;

				//Invalid pulse length
				else
				{
					cap->m_offsets.push_back(starts[i]);
					cap->m_durations.push_back(lens[i]);
					cap->m_samples.push_back(OneWireSymbol(OneWireSymbol::TYPE_ERROR, 0));

					state = STATE_IDLE;
				}

				bitcount ++;

				//Last bit? Finish the byte
				if(bitcount == 8)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(tend - tstart);
					cap->m_samples.push_back(OneWireSymbol(OneWireSymbol::TYPE_DATA, current_byte));

					bitcount = 0;
					current_byte = 0;
				}

				break;

			default:
				break;
		}
	}

	cap->MarkModifiedFromCpu();
}

std::string OneWireWaveform::GetColor(size_t i)
{
	const OneWireSymbol& s = m_samples[i];

	switch(s.m_stype)
	{
		case OneWireSymbol::TYPE_RESET:
			if(s.m_data == 1)
				return StandardColors::colors[StandardColors::COLOR_ERROR];
			else
				return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case OneWireSymbol::TYPE_PRESENCE:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case OneWireSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case OneWireSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string OneWireWaveform::GetText(size_t i)
{
	char tmp[32];

	const OneWireSymbol& s = m_samples[i];

	switch(s.m_stype)
	{
		case OneWireSymbol::TYPE_RESET:
			if(s.m_data == 1)
				return "RESET (too short)";
			else
				return "RESET";

		case OneWireSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return string(tmp);

		case OneWireSymbol::TYPE_PRESENCE:
			return "PRESENT";

		case OneWireSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}
