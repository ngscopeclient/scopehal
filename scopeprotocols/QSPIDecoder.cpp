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
	@brief Implementation of QSPIDecoder
 */

#include "../scopehal/scopehal.h"
#include "QSPIDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

QSPIDecoder::QSPIDecoder(const string& color)
	: SPIDecoder(color)
{
	//Remove the x1 SPI inputs
	m_inputs.clear();
	m_signalNames.clear();

	CreateInput("clk");
	CreateInput("cs#");
	CreateInput("dq3");
	CreateInput("dq2");
	CreateInput("dq1");
	CreateInput("dq0");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

string QSPIDecoder::GetProtocolName()
{
	return "Quad SPI";
}

bool QSPIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 6) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void QSPIDecoder::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto clk = GetDigitalInputWaveform(0);
	auto csn = GetDigitalInputWaveform(1);
	auto data3 = GetDigitalInputWaveform(2);
	auto data2 = GetDigitalInputWaveform(3);
	auto data1 = GetDigitalInputWaveform(4);
	auto data0 = GetDigitalInputWaveform(5);

	//Create the capture
	auto cap = new SPIWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	cap->m_triggerPhase = 0;

	//TODO: packets based on CS# pulses

	//Loop over the data and look for transactions
	enum
	{
		STATE_IDLE,
		STATE_DESELECTED,
		STATE_SELECTED_CLKLO,
		STATE_SELECTED_CLKHI
	} state = STATE_IDLE;

	bool high_nibble		= true;
	int64_t bytestart		= 0;
	uint8_t current_byte	= 0;
	bool first_byte			= false;
	size_t last_bytelen 	= 0;

	size_t clklen = clk->m_samples.size();
	size_t cslen = csn->m_samples.size();
	size_t datalen[4] =
	{
		data0->m_samples.size(),
		data1->m_samples.size(),
		data2->m_samples.size(),
		data3->m_samples.size()
	};

	size_t ics			= 0;
	size_t iclk			= 0;
	size_t idata[4]		= {0};

	int64_t timestamp	= 0;

	while(true)
	{
		bool cur_cs = csn->m_samples[ics];
		bool cur_clk = clk->m_samples[iclk];
		uint8_t cur_data =
			(data3->m_samples[idata[3]] ? 0x8 : 0) |
			(data2->m_samples[idata[2]] ? 0x4 : 0) |
			(data1->m_samples[idata[1]] ? 0x2 : 0) |
			(data0->m_samples[idata[0]] ? 0x1 : 0);

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
					high_nibble = true;
					bytestart = timestamp;
					first_byte = true;
				}
				break;

			//wait for rising edge of clk
			case STATE_SELECTED_CLKLO:
				if(cur_clk)
				{
					state = STATE_SELECTED_CLKHI;

					//High nibble
					if(high_nibble)
					{
						//Add a "chip selected" event
						if(first_byte)
						{
							cap->m_offsets.push_back(bytestart);
							cap->m_durations.push_back(timestamp - bytestart);
							cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_SELECT, 0));
						}

						//Generate the byte, then start the next one
						else
						{
							cap->m_offsets.push_back(bytestart);
							last_bytelen = timestamp - bytestart;
							cap->m_durations.push_back(last_bytelen);
							cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DATA, current_byte));
						}

						current_byte = (cur_data << 4);
						bytestart = timestamp;
						first_byte = false;
					}

					//Low nibble? Save it
					else
						current_byte |= cur_data;

					high_nibble = !high_nibble;
				}

				//end of packet
				//TODO: error if a byte is truncated
				else if(cur_cs)
				{
					//Push the last in-progress byte
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(last_bytelen);
					cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DATA, current_byte));

					bytestart += last_bytelen;
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
		int64_t next_cs = GetNextEventTimestampScaled(csn, ics, cslen, timestamp);
		int64_t next_clk = GetNextEventTimestampScaled(clk, iclk, clklen, timestamp);

		//If we can't move forward, stop (don't bother looking for glitches on data)
		int64_t next_timestamp = min(next_clk, next_cs);
		if(next_timestamp == timestamp)
			break;

		//All good, move on
		timestamp = next_timestamp;
		AdvanceToTimestampScaled(csn, ics, cslen, timestamp);
		AdvanceToTimestampScaled(clk, iclk, clklen, timestamp);
		AdvanceToTimestampScaled(data0, idata[0], datalen[0], timestamp);
		AdvanceToTimestampScaled(data1, idata[1], datalen[1], timestamp);
		AdvanceToTimestampScaled(data2, idata[2], datalen[2], timestamp);
		AdvanceToTimestampScaled(data3, idata[3], datalen[3], timestamp);
	}

	SetData(cap, 0);
}
