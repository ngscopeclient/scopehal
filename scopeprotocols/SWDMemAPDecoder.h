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
	@brief Declaration of SWDMemAPDecoder
 */
#ifndef SWDMemAPDecoder_h
#define SWDMemAPDecoder_h

#include "../scopehal/PacketDecoder.h"

class SWDMemAPSymbol
{
public:

	SWDMemAPSymbol()
	{}

	SWDMemAPSymbol(bool write, uint32_t addr, uint32_t data)
	 : m_write(write)
	 , m_addr(addr)
	 , m_data(data)
	{}

	bool m_write;
	uint32_t m_addr;
	uint32_t m_data;

	bool operator== (const SWDMemAPSymbol& s) const
	{
		return (m_write == s.m_write) && (m_addr == s.m_addr) && (m_data == s.m_data);
	}
};

typedef Waveform<SWDMemAPSymbol> SWDMemAPWaveform;

class SWDMemAPDecoder : public PacketDecoder
{
public:
	SWDMemAPDecoder(const std::string& color);

	virtual Gdk::Color GetColor(size_t i, size_t stream);
	virtual std::string GetText(size_t i, size_t stream);

	virtual void Refresh();

	static std::string GetProtocolName();

	std::vector<std::string> GetHeaders();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	PROTOCOL_DECODER_INITPROC(SWDMemAPDecoder)

protected:
};

#endif
