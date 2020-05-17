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
	@brief Implementation of SPIDecoder
 */

#include "../scopehal/scopehal.h"
#include "SPIDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SPIDecoder::SPIDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	//Set up channels
	m_signalNames.push_back("clk");
	m_channels.push_back(NULL);

	m_signalNames.push_back("cs#");
	m_channels.push_back(NULL);

	m_signalNames.push_back("data");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SPIDecoder::NeedsConfig()
{
	return true;
}

bool SPIDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i < 3) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string SPIDecoder::GetProtocolName()
{
	return "SPI";
}

void SPIDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "SPI(%s)",	m_channels[2]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SPIDecoder::Refresh()
{
	//Get the input data
	for(int i=0; i<2; i++)
	{
		if(m_channels[i] == NULL)
		{
			SetData(NULL);
			return;
		}
	}
	auto clk = dynamic_cast<DigitalWaveform*>(m_channels[0]->GetData());
	auto csn = dynamic_cast<DigitalWaveform*>(m_channels[1]->GetData());
	auto data = dynamic_cast<DigitalWaveform*>(m_channels[2]->GetData());
	if( (clk == NULL) || (csn == NULL) || (data == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Create the capture
	auto cap = new SPIWaveform;
	cap->m_timescale = clk->m_timescale;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startPicoseconds = clk->m_startPicoseconds;

	//TODO: different cpha/cpol modes

	//TODO: packets based on CS# pulses

	//Loop over the data and look for transactions
	//For now, assume equal sample rate

	enum
	{
		STATE_DESELECTED,
		STATE_SELECTED_CLKLO,
		STATE_SELECTED_CLKHI
	} state = STATE_DESELECTED;

	uint8_t	current_byte	= 0;
	uint8_t	bitcount 		= 0;
	int64_t bytestart		= 0;
	bool first				= false;

	size_t len = clk->m_samples.size();
	len = min(len, csn->m_samples.size());
	len = min(len, data->m_samples.size());
	for(size_t i=0; i<len; i++)
	{
		bool cur_cs = csn->m_samples[i];
		bool cur_clk = clk->m_samples[i];
		bool cur_data = data->m_samples[i];

		switch(state)
		{
			//wait for falling edge of CS#
			case STATE_DESELECTED:
				if(!cur_cs)
				{
					state = STATE_SELECTED_CLKLO;
					current_byte = 0;
					bitcount = 0;
					bytestart = clk->m_offsets[i];
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
							cap->m_offsets.push_back(clk->m_offsets[i] - bytestart);
							cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_SELECT, 0));
							first = false;
						}
						bytestart = clk->m_offsets[i];
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
						cap->m_offsets.push_back(clk->m_offsets[i] - bytestart);
						cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DATA, current_byte));

						bitcount = 0;
						current_byte = 0;
						bytestart = clk->m_offsets[i];
					}
				}

				//end of packet
				//TODO: error if a byte is truncated
				else if(cur_cs)
				{
					cap->m_offsets.push_back(bytestart);
					cap->m_offsets.push_back(clk->m_offsets[i] - bytestart);
					cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DESELECT, 0));

					bytestart = clk->m_offsets[i];
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
					cap->m_offsets.push_back(clk->m_offsets[i] - bytestart);
					cap->m_samples.push_back(SPISymbol(SPISymbol::TYPE_DESELECT, 0));

					bytestart = clk->m_offsets[i];
					state = STATE_DESELECTED;
				}

				break;
		}
	}

	SetData(cap);
}

Gdk::Color SPIDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<SPIWaveform*>(GetData());
	if(capture != NULL)
	{
		const SPISymbol& s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case SPISymbol::TYPE_SELECT:
			case SPISymbol::TYPE_DESELECT:
				return m_standardColors[COLOR_CONTROL];

			case SPISymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case SPISymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}
	return m_standardColors[COLOR_ERROR];
}

string SPIDecoder::GetText(int i)
{
	auto capture = dynamic_cast<SPIWaveform*>(GetData());
	if(capture != NULL)
	{
		const SPISymbol& s = capture->m_samples[i];
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
	return "";
}
