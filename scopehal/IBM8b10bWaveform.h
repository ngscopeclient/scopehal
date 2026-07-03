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
	@brief Declaration of IBM8b10bWaveform
 */
#ifndef IBM8b10bWaveform_h
#define IBM8b10bWaveform_h

/**
	@brief A single 8b/10b symbol

	This class is used for serialization and GPU interchange and must be POD, taking up a two bytes in memory.
	!!! Any changes to memory layout will break compatibility with saved waveforms !!!
 */
class IBM8b10bSymbol
{
public:
	IBM8b10bSymbol()
	{}

	enum flags
	{
		FLAG_ERROR_3	= 0x1,	//Invalid 3b4b sub-code
		FLAG_ERROR_5	= 0x2,	//Invalid 5b6b sub-code
		FLAG_ERROR_DISP	= 0x4,	//Invalid disparity
		FLAG_ERROR_MASK	= 0x7,	//Any error

		FLAG_CONTROL	= 0x40,	//K character
		FLAG_DISP_POS	= 0x80	//Disparity positive
	};

	IBM8b10bSymbol(bool b, bool e5, bool e3, bool ed, uint8_t d, int8_t disp)
	 : m_data(d)
	 , m_flags(0)
	{
		if(b)
			m_flags	|= FLAG_CONTROL;
		if(e5)
			m_flags |= FLAG_ERROR_5;
		if(e3)
			m_flags |= FLAG_ERROR_3;
		if(ed)
			m_flags |= FLAG_ERROR_DISP;
		if(disp > 0)
			m_flags |= FLAG_DISP_POS;
	}

	uint8_t m_data;
	uint8_t m_flags;

	bool operator== (const IBM8b10bSymbol& s) const
	{ return (m_flags == s.m_flags) && (m_data == s.m_data); }
};

class IBM8b10bWaveform : public SparseWaveform<IBM8b10bSymbol>
{
public:
	IBM8b10bWaveform()
		: SparseWaveform<IBM8b10bSymbol>(),
		m_displayFormat(FORMAT_DOTTED)
	{};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
	static FilterParameter MakeIBM8b10bDisplayFormatParameter();

	enum DisplayFormat
	{
		FORMAT_DOTTED,
		FORMAT_HEX
	};

	void SetDisplayFormat(DisplayFormat format)
	{ m_displayFormat = format; }

protected:
	DisplayFormat m_displayFormat;
};

#endif
