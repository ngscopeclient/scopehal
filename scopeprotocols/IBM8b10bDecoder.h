/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of IBM8b10bDecoder
 */

#ifndef IBM8b10bDecoder_h
#define IBM8b10bDecoder_h

class IBM8b10bSymbol
{
public:
	IBM8b10bSymbol()
	{}

	IBM8b10bSymbol(bool b, bool e5, bool e3, bool ed, uint8_t d, int disp)
	 : m_control(b)
	 , m_error5(e5)
	 , m_error3(e3)
	 , m_errorDisp(ed)
	 , m_data(d)
	 , m_disparity(disp)
	{}

	bool m_control;
	bool m_error5;
	bool m_error3;
	bool m_errorDisp;
	uint8_t m_data;
	int m_disparity;

	bool operator== (const IBM8b10bSymbol& s) const
	{
		return (m_control == s.m_control) &&
			(m_error5 == s.m_error5) &&
			(m_error3 == s.m_error3) &&
			(m_errorDisp == s.m_errorDisp) &&
			(m_data == s.m_data) &&
			(m_disparity == s.m_disparity);
	}
};

class IBM8b10bWaveform : public SparseWaveform<IBM8b10bSymbol>
{
public:
	IBM8b10bWaveform (FilterParameter& displayformat) : SparseWaveform<IBM8b10bSymbol>(), m_displayformat(displayformat) {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;

	FilterParameter& m_displayformat;
};

class IBM8b10bDecoder : public Filter
{
public:
	IBM8b10bDecoder(const std::string& color);

	static FilterParameter MakeIBM8b10bDisplayFormatParameter();

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	enum DisplayFormat
	{
		FORMAT_DOTTED,
		FORMAT_HEX
	};

	PROTOCOL_DECODER_INITPROC(IBM8b10bDecoder)

protected:
	std::string m_displayformat;

	std::string m_commaSearchWindow;

	void Align(SparseDigitalWaveform& data, size_t& i);
};

#endif
