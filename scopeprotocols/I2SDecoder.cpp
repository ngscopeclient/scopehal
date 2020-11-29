/***********************************************************************************************************************
*                                                                                                                      *
* Copyright (c) 2020 Matthew Germanowski                                                                               *
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
	@author Matthew Germanowski
	@brief Implementation of I2SDecoder
 */

#include "../scopehal/scopehal.h"
#include "I2SDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

I2SDecoder::I2SDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	//Set up channels
	CreateInput("SCK");
	CreateInput("WS");
	CreateInput("SD");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool I2SDecoder::NeedsConfig()
{
	return true;
}

bool I2SDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 3) &&
		(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) &&
		(stream.m_channel->GetWidth() == 1)
		)
	{
		return true;
	}

	return false;
}

string I2SDecoder::GetProtocolName()
{
	return "I2S";
}

void I2SDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "I2S(%s, %s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str(),
		GetInputDisplayName(2).c_str()
		);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic
void I2SDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto sck = GetDigitalInputWaveform(0);
	auto ws = GetDigitalInputWaveform(1);
	auto sd = GetDigitalInputWaveform(2);

	//Create the capture
	auto cap = new I2SWaveform;
	cap->m_timescale = sck->m_timescale;
	cap->m_startTimestamp = sck->m_startTimestamp;
	cap->m_startFemtoseconds = sck->m_startFemtoseconds;

	//Loop over the data and look for transactions
	//For now, assume equal sample rate
	bool				last_sck = sck->m_samples[0];
	bool				last_ws = ws->m_samples[0];
	size_t			symbol_start = 0;
	uint32_t			current_word = 0;
	uint8_t			bitcount = 0;
	size_t len = sck->m_samples.size();
	len = min(len, ws->m_samples.size());
	len = min(len, sd->m_samples.size());
	for(size_t i=0; i<len; i++) {
		if (!(sck->m_samples[i] && !last_sck)) {
			last_sck = sck->m_samples[i];
			continue;
		} else {
			last_sck = sck->m_samples[i];
		}


		if (ws->m_samples[i] != last_ws) {
			auto tstart = sck->m_offsets[symbol_start];
			cap->m_offsets.push_back(tstart);
			cap->m_durations.push_back(sck->m_offsets[i] - tstart);
			cap->m_samples.push_back(I2SSymbol(current_word, bitcount, last_ws));

			symbol_start = i;
			last_ws = ws->m_samples[i];
			current_word = 0;
			bitcount = 0;
		}

		if (sd->m_samples[i]) {
			current_word = current_word*2+1;
		} else {
			current_word = current_word*2;
		}
		bitcount++;
	}
	SetData(cap, 0);
}

Gdk::Color I2SDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<I2SWaveform*>(GetData(0));
	if(capture != NULL)
	{
		return m_standardColors[COLOR_DATA];
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string I2SDecoder::GetText(int i)
{
	auto capture = dynamic_cast<I2SWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const I2SSymbol& s = capture->m_samples[i];
		char tmp[32];
		memset(tmp, 0, 32);
		if (s.m_right) {
			snprintf(tmp, sizeof(tmp), "R %08x", s.m_data);
		} else {
			snprintf(tmp, sizeof(tmp), "L %08x", s.m_data);
		}
		return string(tmp);
	}
	return "";
}
