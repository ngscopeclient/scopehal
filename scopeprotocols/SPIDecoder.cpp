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
	@brief Implementation of SPIDecoder
 */

#include "../scopehal/scopehal.h"
#include "SPIDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SPIDecoder::SPIDecoder(const string& color)
	: Filter(color, CAT_BUS)
{
	AddProtocolStream("data");
	CreateInput("clk");
	CreateInput("cs#");
	CreateInput("data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SPIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 3) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string SPIDecoder::GetProtocolName()
{
	return "SPI";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SPIDecoder::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto clk = GetInputWaveform(0);
	auto csn = GetInputWaveform(1);
	auto data = GetInputWaveform(2);

	clk->PrepareForCpuAccess();
	csn->PrepareForCpuAccess();
	data->PrepareForCpuAccess();

	auto sclk = dynamic_cast<SparseDigitalWaveform*>(clk);
	auto uclk = dynamic_cast<UniformDigitalWaveform*>(clk);
	auto scsn = dynamic_cast<SparseDigitalWaveform*>(csn);
	auto ucsn = dynamic_cast<UniformDigitalWaveform*>(csn);
	auto sdata = dynamic_cast<SparseDigitalWaveform*>(data);
	auto udata = dynamic_cast<UniformDigitalWaveform*>(data);

	//Create the capture
	auto cap = new SPIWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	//TODO: different cpha/cpol modes

	//TODO: packets based on CS# pulses?

	//Loop over the data and look for transactions
	enum
	{
		STATE_IDLE,
		STATE_DESELECTED,
		STATE_SELECTED_CLKLO,
		STATE_SELECTED_CLKHI
	} state = STATE_IDLE;

	uint8_t	current_byte	= 0;
	uint8_t	bitcount 		= 0;
	int64_t bytestart		= 0;
	bool first				= false;

	size_t ics			= 0;
	size_t iclk			= 0;
	size_t idata		= 0;

	int64_t timestamp	= 0;

	size_t clklen = clk->size();
	size_t cslen = csn->size();
	size_t datalen = data->size();

	while(true)
	{
		//Get the current samples
		bool cur_cs = GetValue(scsn, ucsn, ics);
		bool cur_clk = GetValue(sclk, uclk, iclk);
		bool cur_data = GetValue(sdata, udata, idata);

		switch(state)
		{
			//Just started the decode, wait for CS# to go high (and don't attempt to decode a partial packet)
			case STATE_IDLE:
				if(cur_cs)
					state = STATE_DESELECTED;
				break;

			//wait for falling edge of CS#
			case STATE_DESELECTED:
				if(!cur_cs)
				{
					state = STATE_SELECTED_CLKLO;
					current_byte = 0;
					bitcount = 0;
					bytestart = timestamp;
					first = true;
				}
				break;

			//wait for rising edge of clk
			case STATE_SELECTED_CLKLO:
				if(cur_clk)
				{
					if(bitcount == 0)
					{
						//Add a "chip selected" event
						if(first)
						{
							cap->m_offsets.push_back(bytestart);
							cap->m_durations.push_back(timestamp - bytestart);
							cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_SELECT, 0));
							first = false;
						}

						//Extend the last byte until this edge
						else if(!cap->m_samples.empty())
						{
							size_t ilast = cap->m_samples.size()-1;
							if(cap->m_samples[ilast].m_stype == SPISymbol::TYPE_DATA)
								cap->m_durations[ilast] = timestamp - cap->m_offsets[ilast];
						}

						bytestart = timestamp;
					}

					state = STATE_SELECTED_CLKHI;

					//TODO: selectable msb/lsb first direction
					bitcount ++;
					if(cur_data)
						current_byte = 1 | (current_byte << 1);
					else
						current_byte = (current_byte << 1);

					if(bitcount == 8)
					{
						cap->m_offsets.push_back(bytestart);
						cap->m_durations.push_back(timestamp - bytestart);
						cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DATA, current_byte));

						bitcount = 0;
						current_byte = 0;
						bytestart = timestamp;
					}
				}

				//end of packet
				//TODO: error if a byte is truncated
				else if(cur_cs)
				{
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DESELECT, 0));

					bytestart = timestamp;
					state = STATE_DESELECTED;
				}
				break;

			//wait for falling edge of clk
			case STATE_SELECTED_CLKHI:
				if(!cur_clk)
					state = STATE_SELECTED_CLKLO;

				//end of packet
				//TODO: error if a byte is truncated
				else if(cur_cs)
				{
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DESELECT, 0));

					bytestart = timestamp;
					state = STATE_DESELECTED;
				}

				break;
		}

		//Get timestamps of next event on each channel
		int64_t next_cs = GetNextEventTimestampScaled(scsn, ucsn, ics, cslen, timestamp);
		int64_t next_clk = GetNextEventTimestampScaled(sclk, uclk, iclk, clklen, timestamp);

		//If we can't move forward, stop (don't bother looking for glitches on data)
		int64_t next_timestamp = min(next_clk, next_cs);
		if(next_timestamp == timestamp)
			break;

		//All good, move on
		timestamp = next_timestamp;
		AdvanceToTimestampScaled(scsn, ucsn, ics, cslen, timestamp);
		AdvanceToTimestampScaled(sclk, uclk, iclk, clklen, timestamp);
		AdvanceToTimestampScaled(sdata, udata, idata, datalen, timestamp);
	}

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
}

std::string SPIWaveform::GetColor(size_t i)
{
	const SPISymbol& s = m_samples[i];
	switch(s.m_stype)
	{
		case SPISymbol::TYPE_SELECT:
		case SPISymbol::TYPE_DESELECT:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case SPISymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case SPISymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string SPIWaveform::GetText(size_t i)
{
	const SPISymbol& s = m_samples[i];
	char tmp[32];
	switch(s.m_stype)
	{
		case SPISymbol::TYPE_SELECT:
			return "SELECT";
		case SPISymbol::TYPE_DESELECT:
			return "DESELECT";
		case SPISymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return string(tmp);
		case SPISymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}
