/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

#include "../scopehal/scopehal.h"
#include "SDRAMDecoderBase.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SDRAMDecoderBase

SDRAMDecoderBase::SDRAMDecoderBase(const string& color)
	: Filter(color, CAT_MEMORY)
{
	AddProtocolStream("data");
}

SDRAMDecoderBase::~SDRAMDecoderBase()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pretty printing

Gdk::Color SDRAMDecoderBase::GetColor(size_t i, size_t /*stream*/)
{
	auto capture = dynamic_cast<SDRAMWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SDRAMSymbol& s = capture->m_samples[i];

		switch(s.m_stype)
		{
			case SDRAMSymbol::TYPE_MRS:
			case SDRAMSymbol::TYPE_REF:
			case SDRAMSymbol::TYPE_PRE:
			case SDRAMSymbol::TYPE_PREA:
			case SDRAMSymbol::TYPE_STOP:
				return StandardColors::colors[StandardColors::COLOR_CONTROL];

			case SDRAMSymbol::TYPE_ACT:
			case SDRAMSymbol::TYPE_WR:
			case SDRAMSymbol::TYPE_WRA:
			case SDRAMSymbol::TYPE_RD:
			case SDRAMSymbol::TYPE_RDA:
				return StandardColors::colors[StandardColors::COLOR_ADDRESS];

			case SDRAMSymbol::TYPE_ERROR:
			default:
				return StandardColors::colors[StandardColors::COLOR_ERROR];
		}
	}

	//error
	return StandardColors::colors[StandardColors::COLOR_ERROR];
}

string SDRAMDecoderBase::GetText(size_t i, size_t /*stream*/)
{
	auto capture = dynamic_cast<SDRAMWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SDRAMSymbol& s = capture->m_samples[i];

		switch(s.m_stype)
		{
			case SDRAMSymbol::TYPE_MRS:
				return "MRS";

			case SDRAMSymbol::TYPE_REF:
				return "REF";

			case SDRAMSymbol::TYPE_PRE:
				return "PRE";

			case SDRAMSymbol::TYPE_PREA:
				return "PREA";

			case SDRAMSymbol::TYPE_STOP:
				return "STOP";

			case SDRAMSymbol::TYPE_ACT:
				return "ACT";

			case SDRAMSymbol::TYPE_WR:
				return "WR";

			case SDRAMSymbol::TYPE_WRA:
				return "WRA";

			case SDRAMSymbol::TYPE_RD:
				return "RD";

			case SDRAMSymbol::TYPE_RDA:
				return "RDA";

			case SDRAMSymbol::TYPE_ERROR:
			default:
				return "ERR";
		}
	}
	return "";
}
