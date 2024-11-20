/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of RGBLEDDecoder
 */

#include "../scopehal/scopehal.h"
#include "RGBLEDDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RGBLEDDecoder::RGBLEDDecoder(const string& color) : Filter(color, CAT_BUS)
	, m_type(m_parameters["LED Type"])
{
	AddProtocolStream("data");
	CreateInput("din");

	m_type = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_type.AddEnumValue("Everlight 19-C47", TYPE_19_C47);
	m_type.AddEnumValue("Worldsemi WS2812", TYPE_WS2812);
	m_type.SetIntVal(TYPE_WS2812);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool RGBLEDDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if((i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
	{
		return true;
	}

	return false;
}

string RGBLEDDecoder::GetProtocolName()
{
	return "RGB LED";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void RGBLEDDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	LogTrace("Refresh\n");
	LogIndenter li;

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto sdin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto udin = dynamic_cast<UniformDigitalWaveform*>(din);

	//Create the capture
	auto cap = new RGBLEDWaveform;
	cap->PrepareForCpuAccess();
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	SetData(cap, 0);

	//Measure widths of all edges in the incoming signal
	//Add a dummy edge at beginning and end
	vector<int64_t> edges;
	edges.push_back(din->m_triggerPhase);
	if(sdin)
		FindZeroCrossings(sdin, edges);
	else
		FindZeroCrossings(udin, edges);
	edges.push_back(GetOffsetScaled(sdin, udin, din->size()));

	//Figure out nominal pulse width
	const int64_t ns = 1000 * 1000;
	const int64_t us = 1000 * ns;
	int64_t tolerance = 0;
	int64_t long_nom = 0;
	int64_t short_nom = 0;
	switch(m_type.GetIntVal())
	{
		case TYPE_19_C47:
			long_nom = 900 * ns;
			short_nom = 300 * ns;
			tolerance = 80 * ns;
			break;

		case TYPE_WS2812:
		default:
			long_nom = 800 * ns;
			short_nom = 450 * ns;
			tolerance = 150 * ns;
			break;
	}

	//Actual acceptable pulse widths including tolerance banding
	const int64_t short_min = short_nom - tolerance;
	const int64_t short_max = short_nom + tolerance;
	const int64_t long_min = long_nom - tolerance;
	const int64_t long_max = long_nom + tolerance;
	Unit fs(Unit::UNIT_FS);
	LogTrace("Expecting short pulse length: [%s, %s] nominal %s\n",
		fs.PrettyPrint(short_min).c_str(),
		fs.PrettyPrint(short_max).c_str(),
		fs.PrettyPrint(short_nom).c_str());
	LogTrace("Expecting long pulse length: [%s, %s] nominal %s\n",
		fs.PrettyPrint(long_min).c_str(),
		fs.PrettyPrint(long_max).c_str(),
		fs.PrettyPrint(long_nom).c_str());

	//Iterate over pulse widths and decode
	size_t bcount = 0;
	int64_t tstart = 0;
	bool phase = 0;
	pwidth_t wlast = AMBIGUOUS;
	uint32_t value = 0;
	bool error = false;
	for(size_t i=1; i<edges.size(); i++)
	{
		auto duration = edges[i] - edges[i-1];

		//If the pulse is more than 50us long, call it a reset
		if(duration > 50 * us)
		{
			LogTrace("Found reset pulse (%s)\n", fs.PrettyPrint(duration).c_str());
			bcount = 0;
			phase = 0;
			value = 0;
			error = false;
			continue;
		}

		//If more than 5us and the first pulse, allow that as a reset too
		//(to avoid the need for a long idle period at the start of the waveform)
		if( (duration > (5*us)) && (i == 1) )
		{
			LogTrace("Found initial reset pulse (%s)\n", fs.PrettyPrint(duration).c_str());
			bcount = 0;
			phase = 0;
			value = 0;
			error = false;
			continue;
		}

		//Figure out what the bit value is
		[[maybe_unused]] pwidth_t pulsewidth = AMBIGUOUS;

		if( (duration >= short_min) && (duration <= short_max) )
		{
			pulsewidth = SHORT;
			//LogDebug("[bcount=%zu] short (%s)\n", bcount, fs.PrettyPrint(duration).c_str());
		}
		else if( (duration >= long_min) && (duration <= long_max) )
		{
			pulsewidth = LONG;
			//LogDebug("[bcount=%zu] long (%s)\n", bcount, fs.PrettyPrint(duration).c_str());
		}
		else if( (bcount != 23) || (phase != 1) )
			LogTrace("[bcount=%zu] ambiguous (%s)\n", bcount, fs.PrettyPrint(duration).c_str());

		//Ignore length of inter-frame gap
		if( (bcount == 23) && (phase == 1) )
		{
			bcount = 0;
			phase = 0;
			value = 0;
			error = false;
			continue;
		}

		//First half of a symbol: read the bit value
		if(phase == 0)
		{
			//If this is the start of a symbol, save the timestamp
			if(bcount == 0)
				tstart = edges[i-1];

			value <<= 1;
			if(pulsewidth == LONG)
				value |= 1;
			else if(pulsewidth == AMBIGUOUS)
				error = true;

			wlast = pulsewidth;
		}

		//Second half of a symbol: expect opposite of initial
		else
		{
			if( (wlast == SHORT) && (pulsewidth == LONG) )
			{}
			else if( (wlast == LONG) && (pulsewidth == SHORT) )
			{}

			else
				error = 1;

			//Otherwise it's something else.
			//TODO: handle inter-frame gaps?
			bcount ++;
		}

		phase = !phase;

		//End of a symbol?
		if( (bcount == 23) && (phase == 1) )
		{
			LogTrace("Decoded value (started at %s): error=%d, value=#%06x\n",
				fs.PrettyPrint(tstart).c_str(), error, value);

			//Add the symbol
			cap->m_offsets.push_back(tstart);
			cap->m_durations.push_back(edges[i] - tstart);
			if(error)
				cap->m_samples.push_back(RGBLEDSymbol(0x80000000 | value));
			else
				cap->m_samples.push_back(RGBLEDSymbol(value));
		}
	}

	//Done
	cap->MarkModifiedFromCpu();
}

string RGBLEDWaveform::GetColor(size_t i)
{
	const RGBLEDSymbol& s = m_samples[i];
	//TODO: display setting or something?
	/*if(s.m_data & 0x80000000)
		return StandardColors::colors[StandardColors::COLOR_ERROR];
	else*/
	{
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "#%06x", s.m_data & 0xffffff);
		return string(tmp);
	}
}

string RGBLEDWaveform::GetText(size_t i)
{
	const RGBLEDSymbol& s = m_samples[i];

	if(s.m_data & 0x80000000)
		return string("(!) #") + to_string_hex(s.m_data & 0xffffff);

	char tmp[32];
	snprintf(tmp, sizeof(tmp), "#%06x", s.m_data);
	return string(tmp);
}
