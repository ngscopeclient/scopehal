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
	@brief Implementation of I2CDecoder
 */

#include "../scopehal/scopehal.h"
#include "I2CDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

I2CDecoder::I2CDecoder(const string& color)
	: Filter(color, CAT_BUS)
{
	AddProtocolStream("data");
	CreateInput("sda");
	CreateInput("scl");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool I2CDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string I2CDecoder::GetProtocolName()
{
	return "I2C";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void I2CDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto sda = GetDigitalInputWaveform(0);
	auto scl = GetDigitalInputWaveform(1);

	//Create the capture
	auto cap = new I2CWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = sda->m_startTimestamp;
	cap->m_startFemtoseconds = sda->m_startFemtoseconds;
	cap->m_triggerPhase = 0;

	//Loop over the data and look for transactions
	//For now, assume equal sample rate
	bool				last_scl = true;
	bool 				last_sda = true;
	size_t				symbol_start	= 0;
	I2CSymbol::stype	current_type = I2CSymbol::TYPE_ERROR;
	uint8_t				current_byte = 0;
	uint8_t				bitcount = 0;
	bool				last_was_start	= 0;
	size_t len = sda->m_samples.size();
	len = min(len, scl->m_samples.size());
	for(size_t i=0; i<len; i++)
	{
		bool cur_sda = sda->m_samples[i];
		bool cur_scl = scl->m_samples[i];

		//SDA falling with SCL high is beginning of a start condition
		if(!cur_sda && last_sda && cur_scl)
		{
			LogTrace("found i2c start at time %zu\n", (size_t)sda->m_offsets[i]);

			//If we're following an ACK, this is a restart
			if(current_type == I2CSymbol::TYPE_DATA)
				current_type = I2CSymbol::TYPE_RESTART;
			else
			{
				symbol_start = i;
				current_type = I2CSymbol::TYPE_START;
			}
		}

		//End a start bit when SDA goes high if the first data bit is a 1
		//Otherwise end on a falling clock edge
		else if( ((current_type == I2CSymbol::TYPE_START) || (current_type == I2CSymbol::TYPE_RESTART)) &&
				(cur_sda || !cur_scl) )
		{
			auto tstart = sda->m_offsets[symbol_start];
			cap->m_offsets.push_back(tstart * sda->m_timescale + sda->m_triggerPhase);
			cap->m_durations.push_back( (sda->m_offsets[i] - tstart) * sda->m_timescale );
			cap->m_samples.push_back(I2CSymbol(current_type, 0));

			last_was_start	= true;
			current_type = I2CSymbol::TYPE_DATA;
			symbol_start = i;
			bitcount = 0;
			current_byte = 0;
		}


		//SDA rising with SCL high is a stop condition
		else if(cur_sda && !last_sda && cur_scl)
		{
			LogTrace("found i2c stop at time %zu\n", (size_t)sda->m_offsets[i]);

			auto tstart = sda->m_offsets[symbol_start];
			cap->m_offsets.push_back(tstart * sda->m_timescale + sda->m_triggerPhase);
			cap->m_durations.push_back((sda->m_offsets[i] - tstart) * sda->m_timescale);
			cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_STOP, 0));

			last_was_start	= false;

			symbol_start = i;
		}

		//On a rising SCL edge, end the current bit
		else if(cur_scl && !last_scl)
		{
			if(current_type == I2CSymbol::TYPE_DATA)
			{
				//Save the current data bit
				bitcount ++;
				current_byte = (current_byte << 1);
				if(cur_sda)
					current_byte |= 1;

				//Add a sample if the byte is over
				if(bitcount == 8)
				{
					auto tstart = sda->m_offsets[symbol_start];
					cap->m_offsets.push_back(tstart * sda->m_timescale + sda->m_triggerPhase);
					cap->m_durations.push_back((sda->m_offsets[i] - tstart) * sda->m_timescale);
					if(last_was_start)
						cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_ADDRESS, current_byte));
					else
						cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_DATA, current_byte));

					last_was_start	= false;

					bitcount = 0;
					current_byte = 0;
					symbol_start = i;

					current_type = I2CSymbol::TYPE_ACK;
				}
			}

			//ACK/NAK
			else if(current_type == I2CSymbol::TYPE_ACK)
			{
				auto tstart = sda->m_offsets[symbol_start];
				cap->m_offsets.push_back(tstart * sda->m_timescale + sda->m_triggerPhase);
				cap->m_durations.push_back((sda->m_offsets[i] - tstart) * sda->m_timescale);
				cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_ACK, cur_sda));

				last_was_start	= false;

				symbol_start = i;
				current_type = I2CSymbol::TYPE_DATA;
			}
		}

		//Save old state of both pins
		last_sda = cur_sda;
		last_scl = cur_scl;
	}

	SetData(cap, 0);
}

Gdk::Color I2CWaveform::GetColor(size_t i)
{
	const I2CSymbol& s = m_samples[i];

	switch(s.m_stype)
	{
		case I2CSymbol::TYPE_ERROR:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
		case I2CSymbol::TYPE_ADDRESS:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];
		case I2CSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case I2CSymbol::TYPE_ACK:
			if(s.m_data)
				return StandardColors::colors[StandardColors::COLOR_IDLE];
			else
				return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];

		default:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];
	}
}

string I2CWaveform::GetText(size_t i)
{
	const I2CSymbol& s = m_samples[i];

	char tmp[32];
	switch(s.m_stype)
	{
		case I2CSymbol::TYPE_NONE:
		case I2CSymbol::TYPE_ERROR:
			snprintf(tmp, sizeof(tmp), "ERR");
			break;
		case I2CSymbol::TYPE_START:
			snprintf(tmp, sizeof(tmp), "START");
			break;
		case I2CSymbol::TYPE_RESTART:
			snprintf(tmp, sizeof(tmp), "RESTART");
			break;
		case I2CSymbol::TYPE_STOP:
			snprintf(tmp, sizeof(tmp), "STOP");
			break;
		case I2CSymbol::TYPE_ACK:
			if(s.m_data)
				snprintf(tmp, sizeof(tmp), "NAK");
			else
				snprintf(tmp, sizeof(tmp), "ACK");
			break;
		case I2CSymbol::TYPE_ADDRESS:
			if(s.m_data & 1)
				snprintf(tmp, sizeof(tmp), "R:%02x", s.m_data & 0xfe);
			else
				snprintf(tmp, sizeof(tmp), "W:%02x", s.m_data & 0xfe);
			break;
		case I2CSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			break;
	}
	return string(tmp);
}
