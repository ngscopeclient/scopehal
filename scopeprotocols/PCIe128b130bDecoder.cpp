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
	@brief Implementation of PCIe128b130bDecoder
 */

#include "../scopehal/scopehal.h"
#include "PCIe128b130bDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIe128b130bDecoder::PCIe128b130bDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
{
	AddProtocolStream("data");
	CreateInput("data");
	CreateInput("clk");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIe128b130bDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string PCIe128b130bDecoder::GetProtocolName()
{
	return "128b/130b";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIe128b130bDecoder::Refresh()
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
	auto cap = new PCIe128b130bWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	//Record the value of the data stream at each clock edge
	SparseDigitalWaveform data;
	SampleOnAnyEdgesBase(din, clkin, data);

	//Look at each phase and figure out block alignment
	size_t end = data.size() - 130;
	size_t best_offset = 0;
	size_t best_errors = end;
	for(size_t offset=0; offset < 130; offset ++)
	{
		size_t errors = 0;
		for(size_t i=offset; i<end; i+= 130)
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
	uint8_t symbols[32] = {0};
	bool scrambler_locked = false;
	uint32_t scrambler = 0;
	for(size_t i=best_offset; i<end; i += 130)
	{
		//Extract the header bits
		uint8_t header =
			(data.m_samples[i] ? 2 : 0) |
			(data.m_samples[i+1] ? 1 : 0);

		//Figure out type
		PCIe128b130bSymbol::type_t type;
		if( (header == 0) || (header == 3) )
			type = PCIe128b130bSymbol::TYPE_ERROR;
		else if(header == 1)
		{
			if(scrambler_locked)
				type = PCIe128b130bSymbol::TYPE_DATA;
			else
				type = PCIe128b130bSymbol::TYPE_SCRAMBLER_DESYNCED;
		}
		else
			type = PCIe128b130bSymbol::TYPE_ORDERED_SET;

		//Extract the data bytes, but don't descramble yet
		size_t len = 16;
		for(size_t j=0; j<16; j++)
		{
			uint8_t tmp = 0;
			for(size_t k=0; k<8; k++)
				tmp |= (data.m_samples[i + j*8 + k + 2] << ( /* 7- */ k) );
			symbols[j] = tmp;
		}

		//TODO: If this is a skip ordered set (SOS) it can vary in length if bridging is used

		bool is_sos = false;

		//SOS starts with 0xAA
		//Last 3 symbols are LFSR content
		if(type == PCIe128b130bSymbol::TYPE_ORDERED_SET)
		{
			if(symbols[0] == 0xaa)
			{
				is_sos = true;

				for(size_t j=1; j<len; j++)
				{
					if(symbols[j] == 0xe1)
					{
						scrambler = (symbols[j+1] << 16) | (symbols[j+2] << 8) | (symbols[j+3]);
						break;
					}
				}

				scrambler_locked = true;
			}
		}

		//Iterate scrambler for everything but SOS
		if(!is_sos)
		{
			//Throw away scrambler output for ordered sets
			if(type == PCIe128b130bSymbol::TYPE_ORDERED_SET)
			{
				for(size_t j=0; j<len; j++)
					RunScrambler(scrambler);
			}

			//Descramble data
			else
			{
				for(size_t j=0; j<len; j++)
					symbols[j] ^= RunScrambler(scrambler);
			}
		}

		int64_t tstart = data.m_offsets[i] - data.m_durations[i]/2;
		int64_t tend = data.m_offsets[i+130];

		//Scrambler not locked? Prefer to extend existing symbol
		if(type == PCIe128b130bSymbol::TYPE_SCRAMBLER_DESYNCED)
		{
			size_t sz = cap->m_offsets.size();
			if(sz > 0)
			{
				if(cap->m_samples[sz-1].m_type == PCIe128b130bSymbol::TYPE_SCRAMBLER_DESYNCED)
				{
					tstart = cap->m_offsets[sz-1];
					cap->m_durations[sz-1] = (tend - tstart);
					continue;
				}
			}
		}

		//No, add a new symbol
		cap->m_offsets.push_back(tstart);
		cap->m_durations.push_back(tend - data.m_offsets[i]);
		cap->m_samples.push_back(PCIe128b130bSymbol(type, symbols, len));
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

std::string PCIe128b130bWaveform::GetColor(size_t i)
{
	const PCIe128b130bSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case PCIe128b130bSymbol::TYPE_SCRAMBLER_DESYNCED:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case PCIe128b130bSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case PCIe128b130bSymbol::TYPE_ORDERED_SET:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case PCIe128b130bSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string PCIe128b130bWaveform::GetText(size_t i)
{
	string ret;

	const PCIe128b130bSymbol& s = m_samples[i];

	if(s.m_type == PCIe128b130bSymbol::TYPE_SCRAMBLER_DESYNCED)
		return "Scrambler desynced";
	else if(s.m_type == PCIe128b130bSymbol::TYPE_ERROR)
		return "ERROR";

	char tmp[32];
	for(size_t j=0; j<s.m_len; j++)
	{
		snprintf(tmp, sizeof(tmp), "%02x", s.m_data[j]);
		ret += tmp;
	}

	return ret;
}

uint8_t PCIe128b130bDecoder::RunScrambler(uint32_t& state)
{
	uint8_t ret = 0;

	for(int j=0; j<8; j++)
	{
		bool b22 = (state & 0x400000);
		state <<= 1;
		if(b22)
		{
			state ^= 0x210125;
			ret |= (1 << j);
		}
	}

	return ret;
}
