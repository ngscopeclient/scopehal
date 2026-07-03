/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of IBM8b10bWaveform
 */
#include "scopehal.h"
#include "IBM8b10bWaveform.h"

using namespace std;

string IBM8b10bWaveform::GetColor(size_t i)
{
	const IBM8b10bSymbol& s = m_samples[i];

	if(s.m_flags & IBM8b10bSymbol::FLAG_ERROR_MASK)
		return StandardColors::colors[StandardColors::COLOR_ERROR];
	else if(s.m_flags & IBM8b10bSymbol::FLAG_CONTROL)
		return StandardColors::colors[StandardColors::COLOR_CONTROL];
	else
		return StandardColors::colors[StandardColors::COLOR_DATA];
}

string IBM8b10bWaveform::GetText(size_t i)
{
	const IBM8b10bSymbol& s = m_samples[i];

	auto cachedDisplayFormat = m_displayFormat;

	unsigned int right = s.m_data >> 5;
	unsigned int left = s.m_data & 0x1F;

	char tmp[32];
	if(s.m_flags & IBM8b10bSymbol::FLAG_ERROR_5)
		return "ERROR (5b/6b)";
	else if(s.m_flags & IBM8b10bSymbol::FLAG_ERROR_3)
		return "ERROR (3b/4b)";
	else if(s.m_flags & IBM8b10bSymbol::FLAG_ERROR_DISP)
		return "ERROR (disparity)";
	else
	{
		//Dotted format
		if(cachedDisplayFormat == FORMAT_DOTTED)
		{
			if(s.m_flags & IBM8b10bSymbol::FLAG_CONTROL)
				snprintf(tmp, sizeof(tmp), "K%u.%u", left, right);
			else
				snprintf(tmp, sizeof(tmp), "D%u.%u", left, right);

			if(s.m_flags & IBM8b10bSymbol::FLAG_DISP_POS)
				return string(tmp) + "+";
			else
				return string(tmp) + "-";
		}

		//Hex format
		else
		{
			if(s.m_flags & IBM8b10bSymbol::FLAG_CONTROL)
				snprintf(tmp, sizeof(tmp), "K.%02x", s.m_data);
			else
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return string(tmp);
		}
	}

	return "";
}

FilterParameter IBM8b10bWaveform::MakeIBM8b10bDisplayFormatParameter()
{
	auto f = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	f.AddEnumValue("Dotted (K28.5 D21.5)", FORMAT_DOTTED);
	f.AddEnumValue("Hex (K.bc b5)", FORMAT_HEX);
	f.SetIntVal(FORMAT_DOTTED);
	return f;
}
