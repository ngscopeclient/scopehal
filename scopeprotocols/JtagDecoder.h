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
	@brief Declaration of JtagDecoder
 */

#ifndef JtagDecoder_h
#define JtagDecoder_h

#include "../scopehal/PacketDecoder.h"

class JtagSymbol
{
public:
	enum JtagState
	{
		TEST_LOGIC_RESET,
		RUN_TEST_IDLE,
		SELECT_DR_SCAN,
		SELECT_IR_SCAN,
		CAPTURE_DR,
		CAPTURE_IR,
		SHIFT_DR,
		SHIFT_IR,
		EXIT1_DR,
		EXIT1_IR,
		PAUSE_DR,
		PAUSE_IR,
		EXIT2_DR,
		EXIT2_IR,
		UPDATE_DR,
		UPDATE_IR,

		UNKNOWN_0,
		UNKNOWN_1,
		UNKNOWN_2,
		UNKNOWN_3,
		UNKNOWN_4,
	};

	JtagSymbol()
	{}

	JtagSymbol(JtagSymbol::JtagState state, uint8_t idata, uint8_t odata, uint8_t len)
		: m_state(state)
		, m_idata(idata)
		, m_odata(odata)
		, m_len(len)
	{}

	static const char* GetName(JtagSymbol::JtagState state);

	bool operator== (const JtagSymbol& s) const
	{
		return
			(m_state == s.m_state) &&
			(m_idata == s.m_idata) &&
			(m_odata == s.m_odata) &&
			(m_len == s.m_len);
	}

	JtagState m_state;
	uint8_t m_idata;
	uint8_t m_odata;
	uint8_t m_len;
};

class JtagWaveform : public SparseWaveform<JtagSymbol>
{
public:
	JtagWaveform () : SparseWaveform<JtagSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class JtagDecoder : public PacketDecoder
{
public:
	JtagDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual std::vector<std::string> GetHeaders() override;

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(JtagDecoder)

protected:
};

#endif
