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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Base class for all SDRAM decodes
 */
#ifndef SDRAMDecoder_h
#define SDRAMDecoder_h

class SDRAMSymbol
{
public:
	enum stype
	{
		TYPE_MRS,
		TYPE_REF,
		TYPE_PRE,
		TYPE_PREA,
		TYPE_ACT,
		TYPE_WR,
		TYPE_WRA,
		TYPE_RD,
		TYPE_RDA,
		TYPE_STOP,

		TYPE_ERROR
	};

	SDRAMSymbol()
	{}

	SDRAMSymbol(stype t, int bank = 0)
	 : m_stype(t)
	 , m_bank(bank)
	{}

	stype m_stype;
	int m_bank;

	bool operator== (const SDRAMSymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_bank == s.m_bank);
	}
};

typedef Waveform<SDRAMSymbol> SDRAMWaveform;

class SDRAMDecoderBase : public Filter
{
public:
	SDRAMDecoderBase(const std::string& color);
	virtual ~SDRAMDecoderBase();

	virtual Gdk::Color GetColor(size_t i, size_t stream) override;
	virtual std::string GetText(size_t i, size_t stream) override;
};

#endif
