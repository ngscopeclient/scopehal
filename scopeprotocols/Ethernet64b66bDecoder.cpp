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
	@brief Implementation of Ethernet64b66bDecoder
 */

#include "../scopehal/scopehal.h"
#include "Ethernet64b66bDecoder.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet64b66bDecoder::Ethernet64b66bDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
{
	AddProtocolStream("data");
	CreateInput("data");
	CreateInput("clk");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool Ethernet64b66bDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string Ethernet64b66bDecoder::GetProtocolName()
{
	return "64b/66b";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet64b66bDecoder::Refresh()
{
	//Get the input data
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = GetInputWaveform(0);
	auto clkin = GetInputWaveform(1);
	din->PrepareForCpuAccess();
	clkin->PrepareForCpuAccess();

	//Create the capture
	auto cap = new Ethernet64b66bWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	//Record the value of the data stream at each clock edge
	SparseDigitalWaveform data;
	SampleOnAnyEdgesBase(din, clkin, data);

	//Look at each phase and figure out block alignment
	size_t end = data.size() - 66;
	size_t best_offset = 0;
	size_t best_errors = end;
	for(size_t offset=0; offset < 66; offset ++)
	{
		size_t errors = 0;
		for(size_t i=offset; i<end; i+= 66)
		{
			if(data.m_samples[i] == data.m_samples[i+1])
				errors ++;
		}

		if(errors < best_errors)
		{
			best_offset = offset;
			best_errors = errors;
		}
	}


	//Decode the actual data
	bool first		= true;
	uint64_t lfsr	= 0;

	for(size_t i=best_offset; i<end; i += 66)
	{
		//Extract the header bits
		uint8_t header =
			(data.m_samples[i] ? 2 : 0) |
			(data.m_samples[i+1] ? 1 : 0);

		//Extract the data bits and descramble them.
		uint64_t codeword = 0;
		for(size_t j=0; j<64; j++)
		{
			bool b = data.m_samples[i + 2 + j];

			codeword >>= 1;
			if(b ^ ( (lfsr >> 38) & 1) ^ ( (lfsr >> 57) & 1) )
				codeword |= 0x8000000000000000L;

			lfsr = (lfsr << 1) | b;
		}

		//Need to swap bit/byte ordering around a bunch.
		uint64_t bytes[8] =
		{
			(codeword >> 56) & 0xff,
			(codeword >> 48) & 0xff,
			(codeword >> 40) & 0xff,
			(codeword >> 32) & 0xff,
			(codeword >> 24) & 0xff,
			(codeword >> 16) & 0xff,
			(codeword >> 8) & 0xff,
			(codeword >> 0) & 0xff,
		};

		codeword =
			(bytes[7] << 56) |
			(bytes[6] << 48) |
			(bytes[5] << 40) |
			(bytes[4] << 32) |
			(bytes[3] << 24) |
			(bytes[2] << 16) |
			(bytes[1] << 8) |
			bytes[0];

		//Just prime the scrambler, we can't decode yet
		if(first)
			first = false;

		//Process descrambled data
		else
		{
			cap->m_offsets.push_back(data.m_offsets[i] - data.m_durations[i]/2);
			cap->m_durations.push_back(data.m_offsets[i+66] - data.m_offsets[i]);
			cap->m_samples.push_back(Ethernet64b66bSymbol(header, codeword));
		}
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

std::string Ethernet64b66bWaveform::GetColor(size_t i)
{
	const Ethernet64b66bSymbol& s = m_samples[i];

	switch(s.m_header)
	{
		case 1:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case 2:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string Ethernet64b66bWaveform::GetText(size_t i)
{
	const Ethernet64b66bSymbol& s = m_samples[i];

	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%016" PRIx64, s.m_data);
	return string(tmp);
}

