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
	@brief Declaration of PCIe128b130bDecoder
 */

#ifndef PCIe128b130bDecoder_h
#define PCIe128b130bDecoder_h

class PCIe128b130bSymbol
{
public:

	enum type_t
	{
		TYPE_SCRAMBLER_DESYNCED,
		TYPE_DATA,
		TYPE_ORDERED_SET,
		TYPE_ERROR
	} m_type;

	PCIe128b130bSymbol()
	{}

	PCIe128b130bSymbol(type_t type, uint8_t* data, size_t len = 16)
	 : m_type(type)
	 , m_len(len)
	{
		for(size_t i=0; i<len; i++)
			m_data[i] = data[i];
	}

	size_t m_len;
	uint8_t m_data[32];

	bool operator== (const PCIe128b130bSymbol& s) const
	{
		return (m_type == s.m_type) && (m_len == s.m_len) && (memcmp(s.m_data, m_data, m_len) == 0);
	}
};

class PCIe128b130bWaveform : public SparseWaveform<PCIe128b130bSymbol>
{
public:
	PCIe128b130bWaveform () : SparseWaveform<PCIe128b130bSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class PCIe128b130bDecoder : public Filter
{
public:
	PCIe128b130bDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(PCIe128b130bDecoder)

protected:
	uint8_t RunScrambler(uint32_t& state);
};

#endif
