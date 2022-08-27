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
	@brief Implementation of DDR3Decoder
 */

#include "../scopehal/scopehal.h"
#include "DDR3Decoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DDR3Decoder::DDR3Decoder(const string& color)
	: SDRAMDecoderBase(color)
{
	CreateInput("CLK");
	CreateInput("WE#");
	CreateInput("RAS#");
	CreateInput("CAS#");
	CreateInput("CS#");
	CreateInput("A12");
	CreateInput("A10");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DDR3Decoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 7) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string DDR3Decoder::GetProtocolName()
{
	return "DDR3 Command Bus";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DDR3Decoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	WaveformBase* caps[7] = {0};
	for(int i=0; i<7; i++)
	{
		caps[i] = GetInputWaveform(i);
		caps[i]->PrepareForCpuAccess();
	}

	//Sample all of the inputs
	auto cclk = caps[0];
	SparseDigitalWaveform we;
	SparseDigitalWaveform ras;
	SparseDigitalWaveform cas;
	SparseDigitalWaveform cs;
	SparseDigitalWaveform a12;
	SparseDigitalWaveform a10;
	SampleOnRisingEdgesBase(caps[1], cclk, we);
	SampleOnRisingEdgesBase(caps[2], cclk, ras);
	SampleOnRisingEdgesBase(caps[3], cclk, cas);
	SampleOnRisingEdgesBase(caps[4], cclk, cs);
	SampleOnRisingEdgesBase(caps[5], cclk, a12);
	SampleOnRisingEdgesBase(caps[6], cclk, a10);

	//Create the capture
	auto cap = new SDRAMWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = cclk->m_startTimestamp;
	cap->m_startFemtoseconds = 0;
	cap->PrepareForCpuAccess();

	//Loop over the data and look for events on clock edges
	size_t len = we.size();
	len = min(len, ras.size());
	len = min(len, cas.size());
	len = min(len, cs.size());
	len = min(len, a12.size());
	len = min(len, a10.size());
	for(size_t i=0; i<len; i++)
	{
		bool swe = we.m_samples[i];
		bool sras = ras.m_samples[i];
		bool scas = cas.m_samples[i];
		bool scs = cs.m_samples[i];
		bool sa12 = a12.m_samples[i];
		bool sa10 = a10.m_samples[i];

		if(!scs)
		{
			//NOP
			if(sras && scas && swe)
				continue;

			SDRAMSymbol sym(SDRAMSymbol::TYPE_ERROR);

			if(!sras && !scas && !swe)
				sym.m_stype = SDRAMSymbol::TYPE_MRS;
			else if(!sras && !scas && swe)
				sym.m_stype = SDRAMSymbol::TYPE_REF;
			else if(!sras && scas && !swe && !sa10)
				sym.m_stype = SDRAMSymbol::TYPE_PRE;
			else if(!sras && scas && !swe && sa10)
				sym.m_stype = SDRAMSymbol::TYPE_PREA;
			else if(!sras && scas && swe)
				sym.m_stype = SDRAMSymbol::TYPE_ACT;
			else if(sras && !scas && !swe)
			{
				if(!sa10)
					sym.m_stype = SDRAMSymbol::TYPE_WR;
				else
					sym.m_stype = SDRAMSymbol::TYPE_WRA;
			}
			else if(sras && !scas && swe)
			{
				if(!sa10)
					sym.m_stype = SDRAMSymbol::TYPE_RD;
				else
					sym.m_stype = SDRAMSymbol::TYPE_RDA;
			}

			//Unknown
			//TODO: self refresh entry/exit (we don't have CKE in the current test data source so can't use it)
			else
				LogDebug("[%zu] Unknown command (RAS=%d, CAS=%d, WE=%d, A12=%d, A10=%d)\n", i, sras, scas, swe, sa12, sa10);

			//Create the symbol
			cap->m_offsets.push_back(we.m_offsets[i]);
			cap->m_durations.push_back(we.m_durations[i]);
			cap->m_samples.push_back(sym);
		}
	}
	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}
