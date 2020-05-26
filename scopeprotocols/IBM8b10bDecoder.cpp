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
	@brief Implementation of IBM8b10bDecoder
 */

#include "../scopehal/scopehal.h"
#include "IBM8b10bDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IBM8b10bDecoder::IBM8b10bDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("data");
	m_channels.push_back(NULL);

	m_signalNames.push_back("clk");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IBM8b10bDecoder::NeedsConfig()
{
	//baud rate has to be set
	return true;
}

bool IBM8b10bDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string IBM8b10bDecoder::GetProtocolName()
{
	return "8b/10b (IBM)";
}

void IBM8b10bDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "8b10b(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IBM8b10bDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<DigitalWaveform*>(m_channels[0]->GetData());
	auto clkin = dynamic_cast<DigitalWaveform*>(m_channels[1]->GetData());
	if( (din == NULL) || (clkin == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Create the capture
	auto cap = new IBM8b10bWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	//Record the value of the data stream at each clock edge
	//TODO: allow single rate clocks too?
	DigitalWaveform data;
	SampleOnAnyEdges(din, clkin, data);

	//Look for commas in the data stream
	//TODO: make this more efficient?
	size_t max_commas = 0;
	size_t max_offset = 0;
	for(size_t offset=0; offset < 10; offset ++)
	{
		size_t num_commas = 0;
		size_t dlen = data.m_samples.size() - 20;
		for(size_t i=0; i<dlen; i += 10)
		{
			//Check if we have a comma (five identical bits) anywhere in the data stream
			//Commas are always at positions 2...6 within the symbol (left-right bit ordering)
			bool comma = true;
			for(int j=3; j<=6; j++)
			{
				if(data.m_samples[i+offset+j] != data.m_samples[i+offset+2])
				{
					comma = false;
					break;
				}
			}
			if(comma)
				num_commas ++;
		}
		if(num_commas > max_commas)
		{
			max_commas = num_commas;
			max_offset = offset;
		}
		//LogDebug("Found %zu commas at offset %zu\n", num_commas, offset);
	}

	//Decode the actual data
	bool first = true;
	int last_disp = -1;
	size_t dlen = data.m_samples.size() - 11;
	for(size_t i=max_offset; i<dlen; i+= 10)
	{
		//5b/6b decode

		uint8_t code6 =
			(data.m_samples[i+0] ? 32 : 0) |
			(data.m_samples[i+1] ? 16 : 0) |
			(data.m_samples[i+2] ? 8 : 0) |
			(data.m_samples[i+3] ? 4 : 0) |
			(data.m_samples[i+4] ? 2 : 0) |
			(data.m_samples[i+5] ? 1 : 0);

		static const int code5_table[64] =
		{
			 0,  0,  0,  0,  0, 23,  8,  7,	//00-07
			 0, 27,  4, 20, 24, 12, 28, 28, //08-0f
			 0, 29,  2, 18, 31, 10, 26, 15, //10-17
			 0,  6, 22, 16, 14,  1, 30,  0,	//18-1f
			 0, 30, 1,  17, 16,  9, 25,  0,	//20-27
			15,  5, 21, 31, 13,  2, 29,  0,	//28-2f
			28,  3, 19, 24, 11,  4, 27,  0,	//30-37
			 7,  8, 23,  0,  0,  0,  0,  0  //38-3f
		};

		static const int disp5_table[64] =
		{
			 0,  0,  0, 0,  0, -2, -2, 0,	//00-07
			 0, -2, -2, 0, -2,  0,  0, 2,	//08-0f
			 0, -2, -2, 0, -2,  0,  0, 2,	//10-17
			-2,  0,  0, 2,  0,  2,  2, 0,	//18-1f
			 0, -2, -2, 0, -2,  0,  0, 2,	//20-27
			-2,  0,  0, 2,  0,  2,  2, 0,	//28-2f
			-2,  0,  0, 2,  0,  2,  2, 0,	//30-37
			 0,  2,  2, 0,  0,  0,  0, 0 	//38-3f
		};

		static const bool err5_table[64] =
		{
			 true,  true,  true,  true,  true, false, false, false,	//00-07
			 true, false, false, false, false, false, false, false, //08-0f
			 true, false, false, false, false, false, false, false, //10-17
			false, false, false, false, false, false, false,  true,	//18-1f
			 true, false, false, false, false, false, false, false,	//20-27
			false, false, false, false, false, false, false,  true,	//28-2f
			false, false, false, false, false, false, false,  true,	//30-37
			false, false, false,  true,  true,  true,  true,  true  //38-3f
		};

		static const bool ctl5_table[64] =
		{
			false, false, false, false, false, false, false, false,	//00-07
			false, false, false, false, false, false, false, true,  //08-0f
			false, false, false, false, false, false, false, false, //10-17
			false, false, false, false, false, false, false, false,	//18-1f
			false, false, false, false, false, false, false, false,	//20-27
			false, false, false, false, false, false, false, false,	//28-2f
			true,  false, false, false, false, false, false, false,	//30-37
			false, false, false, false, false, false, false, false  //38-3f
		};

		int code5 = code5_table[code6];
		int disp5 = disp5_table[code6];
		bool err5 = err5_table[code6];
		bool ctl5 = ctl5_table[code6];

		//3b/4b decode
		uint8_t code4 =
			(data.m_samples[i+6] ? 8 : 0) |
			(data.m_samples[i+7] ? 4 : 0) |
			(data.m_samples[i+8] ? 2 : 0) |
			(data.m_samples[i+9] ? 1 : 0);

		static const bool err3_ctl_table[16] =
		{
			 true,  true, false, false, false, false, false, false,
			false, false, false, false, false, false,  true,  true
		};

		static const int code3_pos_ctl_table[16] =	//if disp5 positive
		{
			0, 0, 4, 3, 0, 2, 6, 7,
			7, 1, 5, 0, 3, 4, 0, 0,
		};

		static const int code3_neg_ctl_table[16] =	//if disp5 negative
		{
			0, 0, 4, 3, 0, 5, 1, 7,
			7, 6, 2, 0, 3, 4, 0, 0
		};

		static const int disp3_ctl_table[16] =
		{
			 0, 0, -2, 0, -2, 0, 0, 2,
			-2, 0,  0, 2,  0, 2, 0, 0
		};

		static const bool err3_table[16] =
		{
			 true,  true, false, false, false, false, false, false,
			false, false, false, false, false, false, false,  true
		};

		static const int code3_table[16] =
		{
			0, 0, 4, 3, 0, 2, 6, 7,
			7, 1, 5, 0, 3, 4, 7, 0
		};

		static const int disp3_table[16] =
		{
			 0, 0, -2, 0, -2, 0, 0, 2,
			-2, 0,  0, 2,  0, 2, 2, 0
		};

		int code3 = false;
		int disp3 = 0;
		int err3 = false;
		if(ctl5)
		{
			if(disp5)
				code3 = code3_pos_ctl_table[code4];
			else
				code3 = code3_neg_ctl_table[code4];
			disp3 = disp3_ctl_table[code4];
			err3 = err3_ctl_table[code4];
		}
		else
		{
			code3 = code3_table[code4];
			disp3 = disp3_table[code4];
			err3 = err3_table[code4];
		}

		//Disparity tracking
		int total_disp = disp3 + disp5;
		if(first)
		{
			if(total_disp < 0)
				last_disp = 1;
			else
				last_disp = -1;
			first = false;
		}

		bool disperr = false;
		if(total_disp > 0 && last_disp > 0)
		{
			disperr = true;
			last_disp = 1;
		}
		else if(total_disp < 0 && last_disp < 0)
		{
			disperr = true;
			last_disp = -1;
		}
		else
			last_disp += total_disp;

		cap->m_offsets.push_back(data.m_offsets[i]);
		cap->m_durations.push_back(data.m_offsets[i+10] - data.m_offsets[i]);
		cap->m_samples.push_back(IBM8b10bSymbol(ctl5, err5 || err3 || disperr, (code3 << 5) | code5));
	}

	SetData(cap);
}

Gdk::Color IBM8b10bDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<IBM8b10bWaveform*>(GetData());
	if(capture != NULL)
	{
		const IBM8b10bSymbol& s = capture->m_samples[i];

		if(s.m_error)
			return m_standardColors[COLOR_ERROR];
		else if(s.m_control)
			return m_standardColors[COLOR_CONTROL];
		else
			return m_standardColors[COLOR_DATA];
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string IBM8b10bDecoder::GetText(int i)
{
	auto capture = dynamic_cast<IBM8b10bWaveform*>(GetData());
	if(capture != NULL)
	{
		const IBM8b10bSymbol& s = capture->m_samples[i];

		unsigned int right = s.m_data >> 5;
		unsigned int left = s.m_data & 0x1F;

		char tmp[32];
		if(s.m_error)
			snprintf(tmp, sizeof(tmp), "ERROR");
		else if(s.m_control)
			snprintf(tmp, sizeof(tmp), "K%d.%d", left, right);
		else
			snprintf(tmp, sizeof(tmp), "D%d.%d", left, right);
		return string(tmp);
	}
	return "";
}

