/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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

IBM8b10bDecoder::IBM8b10bDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
	, m_displayformat("Display Format")
	, m_commaSearchWindow("Comma Search Window")
{
	AddProtocolStream("data");
	CreateInput("data");
	CreateInput("clk");

	m_parameters[m_displayformat] = MakeIBM8b10bDisplayFormatParameter();

	m_parameters[m_commaSearchWindow] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_UI));
	m_parameters[m_commaSearchWindow].SetIntVal(20000);
}

FilterParameter IBM8b10bDecoder::MakeIBM8b10bDisplayFormatParameter()
{
	auto f = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	f.AddEnumValue("Dotted (K28.5 D21.5)", FORMAT_DOTTED);
	f.AddEnumValue("Hex (K.bc b5)", FORMAT_HEX);
	f.SetIntVal(FORMAT_DOTTED);

	return f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IBM8b10bDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string IBM8b10bDecoder::GetProtocolName()
{
	return "8b/10b (IBM)";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IBM8b10bDecoder::Refresh()
{
	LogTrace("IBM8b10bDecoder::Refresh\n");
	LogIndenter li;

	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	auto clkin = GetInputWaveform(1);
	din->PrepareForCpuAccess();
	clkin->PrepareForCpuAccess();

	//Create the capture
	auto cap = new IBM8b10bWaveform(m_parameters[m_displayformat]);
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	//Record the value of the data stream at each clock edge
	//TODO: allow single rate clocks too?
	SparseDigitalWaveform data;
	SampleOnAnyEdgesBase(din, clkin, data);
	data.PrepareForCpuAccess();

	//Preallocate output buffer
	cap->Reserve(data.m_samples.size() / 10);

	//Decode the actual data
	int last_disp = -1;
	bool first = true;
	size_t nsamples = data.m_samples.size();
	if(nsamples < 11)
	{
		SetData(nullptr, 0);
		return;
	}
	size_t dlen = nsamples - 11;
	int64_t lastSymbolLength = 0;
	int64_t lastSymbolEnd = 0;
	int64_t lastSymbolStart = 0;
	for(size_t i=0; i<dlen;i+=10)
	{
		//Re-synchronize at start of waveform or if squelch is reopening
		//If we have a gap
		if(i == 0)
			first = true;
		if( (data.m_offsets[i] - lastSymbolEnd) > 3*lastSymbolLength)
			first = true;
		if(first)
		{
			LogTrace("Realigning at t=%s\n", Unit(Unit::UNIT_FS).PrettyPrint(data.m_offsets[i]).c_str());
			Align(data, i);
		}

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

		static const bool err3_table[16] =
		{
			 true,  false, false, false, false, false, false, false,
			false, false, false, false, false, false, false,  true
		};

		static const int code3_table[16] =
		{
			0, 7, 4, 3, 0, 2, 6, 7,
			7, 1, 5, 0, 3, 4, 7, 0
		};

		static const int disp3_table[16] =
		{
			 0, -2, -2, 0, -2, 0, 0, 2,
			-2, 0,  0, 2,  0, 2, 2, 0
		};

		//true only for Dx.A7
		const bool alt3_table[16] =
		{
			0, 0, 0, 0, 0, 0, 0, 1,
			1, 0, 0, 0, 0, 0, 0, 0
		};

		int code3 = false;
		int disp3 = 0;
		int err3 = false;
		if(ctl5)
		{
			if(disp5 >= 0)
				code3 = code3_pos_ctl_table[code4];
			else
				code3 = code3_neg_ctl_table[code4];
			err3 = err3_ctl_table[code4];
		}
		else
		{
			code3 = code3_table[code4];
			err3 = err3_table[code4];
		}
		disp3 = disp3_table[code4];

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

		//Special processing for a few control codes that use the .A7 format
		bool alt = alt3_table[code4];
		if(alt)
		{
			if( (code5 == 23) || (code5 == 27) || (code5 == 29) || (code5 == 30) )
				ctl5 = true;
		}

		//Horizontally shift the decoded symbol back by half a UI
		//since the recovered clock edge is in the middle of the UI.
		//We want the decoded signal boundaries to line up with the data edge, not the middle of the UI.
		auto symbolStart = data.m_offsets[i] - data.m_durations[i]/2;
		auto symbolLength = data.m_offsets[i+10] - data.m_offsets[i];
		if( (symbolStart - lastSymbolStart) > 5*symbolLength)
		{
			LogTrace("Sync lost (big gap)\n");
			first = true;
		}
		else
		{
			cap->m_offsets.push_back(symbolStart);
			cap->m_durations.push_back(lastSymbolLength);
			cap->m_samples.push_back(IBM8b10bSymbol(ctl5, err5, err3, disperr, (code3 << 5) | code5, last_disp));
		}

		lastSymbolLength = symbolLength;
		lastSymbolEnd = symbolStart + lastSymbolEnd;
		lastSymbolStart = symbolStart;
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

void IBM8b10bDecoder::Align(SparseDigitalWaveform& data, size_t& i)
{
	size_t range = m_parameters[m_commaSearchWindow].GetIntVal();

	//Look for commas in the data stream
	//TODO: make this more efficient?
	size_t max_commas = 0;
	size_t max_offset = 0;
	size_t dend = data.m_samples.size() - 20;
	for(size_t offset=0; offset < 10; offset ++)
	{
		size_t num_commas = 0;
		size_t num_errors = 0;

		//Only check the first few symbols for alignment (default is 20K UIs, 2K symbols)
		//to avoid wasting a ton of time repeatedly decoding a huge capture
		for(size_t delta=0; delta<range; delta += 10)
		{
			size_t base = i + offset + delta;
			if(base > dend)
				break;

			//Check if we have a comma (five identical bits) anywhere in the data stream
			//Commas are always at positions 2...6 within the symbol (left-right bit ordering)
			bool comma = true;
			for(int j=3; j<=6; j++)
			{
				if(data.m_samples[base+j] != data.m_samples[base+2])
				{
					comma = false;
					break;
				}
			}

			//Comma is always exactly five identical bits (so 1 and 7 must be different)
			if(data.m_samples[base+1] == data.m_samples[base+2])
				comma = false;
			if(data.m_samples[base+7] == data.m_samples[base+2])
				comma = false;

			//Count number of 0s and 1s in the symbol
			//Should always be equal (5/5) or two greater (4/6 or 6/4)
			int nones = 0;
			for(int j=0; j<10; j++)
				nones += data.m_samples[base+j];
			if( (nones != 4) && (nones != 5) && (nones != 6) )
				num_errors ++;

			if(comma)
				num_commas ++;
		}

		//Allow a *few* errors, but discard any potential alignment with more errors than commas
		if(num_errors > num_commas)
		{}

		else if(num_commas > max_commas)
		{
			max_commas = num_commas;
			max_offset = offset;
		}
		LogTrace("Found %zu commas and %zu errors at offset %zu\n", num_commas, num_errors, offset);
	}

	i += max_offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IBM8b10bWaveform

string IBM8b10bWaveform::GetColor(size_t i)
{
	const IBM8b10bSymbol& s = m_samples[i];

	if(s.m_error5 || s.m_error3 || s.m_errorDisp)
		return StandardColors::colors[StandardColors::COLOR_ERROR];
	else if(s.m_control)
		return StandardColors::colors[StandardColors::COLOR_CONTROL];
	else
		return StandardColors::colors[StandardColors::COLOR_DATA];
}

string IBM8b10bWaveform::GetText(size_t i)
{
	const IBM8b10bSymbol& s = m_samples[i];

	IBM8b10bDecoder::DisplayFormat cachedDisplayFormat = (IBM8b10bDecoder::DisplayFormat) m_displayformat.GetIntVal();

	unsigned int right = s.m_data >> 5;
	unsigned int left = s.m_data & 0x1F;

	char tmp[32];
	if(s.m_error5)
		return "ERROR (5b/6b)";
	else if(s.m_error3)
		return "ERROR (3b/4b)";
	else if(s.m_errorDisp)
		return "ERROR (disparity)";
	else
	{
		//Dotted format
		if(cachedDisplayFormat == IBM8b10bDecoder::FORMAT_DOTTED)
		{
			if(s.m_control)
				snprintf(tmp, sizeof(tmp), "K%u.%u", left, right);
			else
				snprintf(tmp, sizeof(tmp), "D%u.%u", left, right);

			if(s.m_disparity < 0)
				return string(tmp) + "-";
			else
				return string(tmp) + "+";
		}

		//Hex format
		else
		{
			if(s.m_control)
				snprintf(tmp, sizeof(tmp), "K.%02x", s.m_data);
			else
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return string(tmp);
		}
	}

	return "";
}

