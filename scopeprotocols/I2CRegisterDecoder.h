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
	@brief Declaration of I2CRegisterDecoder
 */
#ifndef I2CRegisterDecoder_h
#define I2CRegisterDecoder_h

#include "../scopehal/PacketDecoder.h"

class I2CRegisterSymbol
{
public:

	enum EepromType
	{
		TYPE_SELECT_READ,		//select with read bit, ack'd
		TYPE_SELECT_WRITE,		//select with write bit, ack'd
		TYPE_ADDRESS,
		TYPE_DATA
	};

	I2CRegisterSymbol()
	{}

	I2CRegisterSymbol(EepromType type, uint32_t data)
	 : m_type(type)
	 , m_data(data)
	{}

	EepromType m_type;
	uint32_t m_data;

	bool operator== (const I2CRegisterSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

class I2CRegisterWaveform : public SparseWaveform<I2CRegisterSymbol>
{
public:
	I2CRegisterWaveform (FilterParameter& rawBytes) : SparseWaveform<I2CRegisterSymbol>(), m_rawBytes(rawBytes) {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;

private:
	FilterParameter& m_rawBytes;
};

class I2CRegisterDecoder : public PacketDecoder
{
public:
	I2CRegisterDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	std::vector<std::string> GetHeaders() override;

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(I2CRegisterDecoder)

protected:
	std::string m_addrbytesname;
	std::string m_baseaddrname;
};

#endif
