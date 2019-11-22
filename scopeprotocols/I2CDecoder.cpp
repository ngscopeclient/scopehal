/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "I2CRenderer.h"
#include "I2CDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

I2CDecoder::I2CDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("sda");
	m_channels.push_back(NULL);

	m_signalNames.push_back("scl");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool I2CDecoder::NeedsConfig()
{
	return true;
}

ChannelRenderer* I2CDecoder::CreateRenderer()
{
	return new I2CRenderer(this);
}

bool I2CDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string I2CDecoder::GetProtocolName()
{
	return "I2C";
}

void I2CDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "I2C(%s, %s)", m_channels[0]->m_displayname.c_str(), m_channels[1]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void I2CDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	DigitalCapture* sda = dynamic_cast<DigitalCapture*>(m_channels[0]->GetData());
	DigitalCapture* scl = dynamic_cast<DigitalCapture*>(m_channels[1]->GetData());
	if( (sda == NULL) || (scl == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Create the capture
	I2CCapture* cap = new I2CCapture;
	cap->m_timescale = sda->m_timescale;
	cap->m_startTimestamp = sda->m_startTimestamp;
	cap->m_startPicoseconds = sda->m_startPicoseconds;

	//Loop over the data and look for transactions
	//For now, assume equal sample rate
	bool				last_scl = true;
	bool 				last_sda = true;
	size_t				symbol_start	= 0;
	I2CSymbol::stype	current_type = I2CSymbol::TYPE_ERROR;
	uint8_t				current_byte = 0;
	uint8_t				bitcount = 0;
	bool				last_was_start	= 0;
	for(size_t i=0; i<sda->m_samples.size(); i++)
	{
		bool cur_sda = sda->m_samples[i].m_sample;
		bool cur_scl = scl->m_samples[i].m_sample;

		//SDA falling with SCL high is beginning of a start condition
		if(!cur_sda && last_sda && cur_scl)
		{
			LogDebug("found i2c start at time %zu\n", sda->m_samples[i].m_offset);

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
			cap->m_samples.push_back(I2CSample(
				sda->m_samples[symbol_start].m_offset,
				sda->m_samples[i].m_offset - symbol_start,
				I2CSymbol(current_type, 0)));

			last_was_start	= true;
			current_type = I2CSymbol::TYPE_DATA;
			symbol_start = i;
			bitcount = 0;
			current_byte = 0;
		}


		//SDA rising with SCL high is a stop condition
		else if(cur_sda && !last_sda && cur_scl)
		{
			LogDebug("found i2c stop at time %zu\n", sda->m_samples[i].m_offset);

			cap->m_samples.push_back(I2CSample(
				sda->m_samples[symbol_start].m_offset,
				sda->m_samples[i].m_offset - symbol_start,
				I2CSymbol(I2CSymbol::TYPE_STOP, 0)));
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
					if(last_was_start)
					{
						cap->m_samples.push_back(I2CSample(
							sda->m_samples[symbol_start].m_offset,
							sda->m_samples[i].m_offset - symbol_start,
							I2CSymbol(I2CSymbol::TYPE_ADDRESS, current_byte)));
					}
					else
					{
						cap->m_samples.push_back(I2CSample(
							sda->m_samples[symbol_start].m_offset,
							sda->m_samples[i].m_offset - symbol_start,
							I2CSymbol(I2CSymbol::TYPE_DATA, current_byte)));
					}
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
				cap->m_samples.push_back(I2CSample(
					sda->m_samples[symbol_start].m_offset,
					sda->m_samples[i].m_offset - symbol_start,
					I2CSymbol(I2CSymbol::TYPE_ACK, cur_sda)));
				last_was_start	= false;

				symbol_start = i;
				current_type = I2CSymbol::TYPE_DATA;
			}
		}

		//Save old state of both pins
		last_sda = cur_sda;
		last_scl = cur_scl;
	}

	SetData(cap);
}
