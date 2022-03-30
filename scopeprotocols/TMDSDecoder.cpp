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
	@brief Implementation of TMDSDecoder
 */

#include "../scopehal/scopehal.h"
#include "TMDSDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TMDSDecoder::TMDSDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("data");
	CreateInput("clk");

	m_lanename = "Lane number";
	m_parameters[m_lanename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_lanename].SetIntVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TMDSDecoder::NeedsConfig()
{
	//baud rate has to be set
	return true;
}

bool TMDSDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;

	return false;
}

string TMDSDecoder::GetProtocolName()
{
	return "8b/10b (TMDS)";
}

void TMDSDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "TMDS(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TMDSDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetDigitalInputWaveform(0);
	auto clkin = GetDigitalInputWaveform(1);

	//Create the capture
	auto cap = new TMDSWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	//Record the value of the data stream at each clock edge
	DigitalWaveform sampdata;
	SampleOnAnyEdges(din, clkin, sampdata);

	/*
		Look for preamble data. We need this to synchronize. (HDMI 1.4 spec section 5.4.2)

		TMDS sends the LSB first.
		Since element 0 of a C++ array is leftmost, this table has bit ordering mirrored from the spec.
	 */
	static const bool control_codes[4][10] =
	{
		{ 0, 0, 1, 0, 1, 0, 1, 0, 1, 1 },
		{ 1, 1, 0, 1, 0, 1, 0, 1, 0, 0 },
		{ 0, 0, 1, 0, 1, 0, 1, 0, 1, 0 },
		{ 1, 1, 0, 1, 0, 1, 0, 1, 0, 1 }
	};

	size_t max_preambles = 0;
	size_t max_offset = 0;
	for(size_t offset=0; offset < 10; offset ++)
	{
		size_t num_preambles[4] = {0};
		for(size_t i=0; i<sampdata.m_samples.size() - 20; i += 10)
		{
			//Look for control code "j" at phase "offset", position "i" within the data stream
			for(size_t j=0; j<4; j++)
			{
				bool match = true;
				for(size_t k=0; k<10; k++)
				{
					if(sampdata.m_samples[i+offset+k] != control_codes[j][k])
						match = false;
				}
				if(match)
				{
					num_preambles[j] ++;
					break;
				}
			}
		}

		for(size_t j=0; j<4; j++)
		{
			if(num_preambles[j] > max_preambles)
			{
				max_preambles = num_preambles[j];
				max_offset = offset;
			}
		}
	}

	int lane = m_parameters[m_lanename].GetIntVal();

	//HDMI Video guard band (HDMI 1.4 spec 5.2.2.1)
	static const bool video_guard[3][10] =
	{
		{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 1 },
		{ 1, 1, 0, 0, 1, 1, 0, 0, 1, 0 },		//also used for data guard band, 5.2.3.3
		{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 1 },
	};

	//TODO: TERC4 (5.4.3)

	enum
	{
		TYPE_DATA,
		TYPE_PREAMBLE,
		TYPE_GUARD
	} last_symbol_type = TYPE_DATA;

	//Decode the actual data
	size_t sampmax = sampdata.m_samples.size()-11;
	for(size_t i=max_offset; i<sampmax; i+= 10)
	{
		bool match = true;

		//Check for control codes at any point in the sequence
		for(size_t j=0; j<4; j++)
		{
			match = true;
			for(size_t k=0; k<10; k++)
			{
				if(sampdata.m_samples[i+k] != control_codes[j][k])
					match = false;
			}

			if(match)
			{
				cap->m_offsets.push_back(sampdata.m_offsets[i]);
				cap->m_durations.push_back(sampdata.m_offsets[i+10] - sampdata.m_offsets[i]);
				cap->m_samples.push_back(TMDSSymbol(TMDSSymbol::TMDS_TYPE_CONTROL, j));

				last_symbol_type = TYPE_PREAMBLE;
				break;
			}
		}

		if(match)
			continue;

		//Check for HDMI video/control leading guard band
		if( (last_symbol_type == TYPE_PREAMBLE) || (last_symbol_type == TYPE_GUARD) )
		{
			match = true;
			for(size_t k=0; k<10; k++)
			{
				if(sampdata.m_samples[i+k] != video_guard[lane][k])
					match = false;
			}

			if(match)
			{
				cap->m_offsets.push_back(sampdata.m_offsets[i]);
				cap->m_durations.push_back(sampdata.m_offsets[i+10] - sampdata.m_offsets[i]);
				cap->m_samples.push_back(TMDSSymbol(TMDSSymbol::TMDS_TYPE_GUARD, 0));
				//last_symbol_type = TYPE_GUARD;
				break;
			}
		}

		if(match)
			continue;

		//Whatever is left is assumed to be video data
		bool d9 = sampdata.m_samples[i+9];
		bool d8 = sampdata.m_samples[i+8];

		uint8_t d = sampdata.m_samples[i+0] |
					(sampdata.m_samples[i+1] << 1) |
					(sampdata.m_samples[i+2] << 2) |
					(sampdata.m_samples[i+3] << 3) |
					(sampdata.m_samples[i+4] << 4) |
					(sampdata.m_samples[i+5] << 5) |
					(sampdata.m_samples[i+6] << 6) |
					(sampdata.m_samples[i+7] << 7);

		if(d9)
			d ^= 0xff;

		if(d8)
			d ^= (d << 1);
		else
			d ^= (d << 1) ^ 0xfe;

		cap->m_offsets.push_back(sampdata.m_offsets[i]);
		cap->m_durations.push_back(sampdata.m_offsets[i+10] - sampdata.m_offsets[i]);
		cap->m_samples.push_back(TMDSSymbol(TMDSSymbol::TMDS_TYPE_DATA, d));
		last_symbol_type = TYPE_DATA;
	}

	SetData(cap, 0);
}

Gdk::Color TMDSDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<TMDSWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const TMDSSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case TMDSSymbol::TMDS_TYPE_CONTROL:
				return m_standardColors[COLOR_CONTROL];

			case TMDSSymbol::TMDS_TYPE_GUARD:
				return m_standardColors[COLOR_PREAMBLE];

			case TMDSSymbol::TMDS_TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case TMDSSymbol::TMDS_TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string TMDSDecoder::GetText(int i)
{
	auto capture = dynamic_cast<TMDSWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const TMDSSymbol& s = capture->m_samples[i];

		char tmp[32];
		switch(s.m_type)
		{
			case TMDSSymbol::TMDS_TYPE_CONTROL:
				snprintf(tmp, sizeof(tmp), "CTL%d", s.m_data);
				break;

			case TMDSSymbol::TMDS_TYPE_GUARD:
				return "GB";

			case TMDSSymbol::TMDS_TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				break;

			case TMDSSymbol::TMDS_TYPE_ERROR:
			default:
				return "ERROR";

		}
		return string(tmp);
	}
	return "";
}
