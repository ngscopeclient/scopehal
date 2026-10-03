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
	@brief Declaration of Ethernet1000BaseXRawDecoder
 */

#ifndef Ethernet1000BaseXRawDecoder_h
#define Ethernet1000BaseXRawDecoder_h

#include "../scopehal/PacketDecoder.h"

class Ethernet1000BaseXRawSymbol
{
public:

	enum SymbolType
	{
		TYPE_DATA,
		TYPE_START,
		TYPE_STOP
	} m_type;

	uint8_t m_data;

	Ethernet1000BaseXRawSymbol()
	{}

	Ethernet1000BaseXRawSymbol(SymbolType type, uint8_t data = 0)
		: m_type(type)
		, m_data(data)
	{}


	bool operator==(const Ethernet1000BaseXRawSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}

};

class Ethernet1000BaseXRawWaveform : public SparseWaveform<Ethernet1000BaseXRawSymbol>
{
public:
	Ethernet1000BaseXRawWaveform () : SparseWaveform<Ethernet1000BaseXRawSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief Decoder for raw 1000base-X PCS but not 802.3 framing on top
 */
class Ethernet1000BaseXRawDecoder : public PacketDecoder
{
public:
	Ethernet1000BaseXRawDecoder(const std::string& color);
	virtual ~Ethernet1000BaseXRawDecoder();

	virtual std::vector<std::string> GetHeaders() override;

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;

	static std::string GetProtocolName();

	PROTOCOL_DECODER_INITPROC(Ethernet1000BaseXRawDecoder)
};

#endif
