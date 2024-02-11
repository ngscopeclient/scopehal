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
	@brief Declaration of PCIeTransportDecoder
 */

#ifndef PCIeTransportDecoder_h
#define PCIeTransportDecoder_h

#include "../scopehal/PacketDecoder.h"

class PCIeTransportSymbol
{
public:

	//Symbol type fields
	enum SymbolType
	{
		TYPE_TLP_TYPE,
		TYPE_TRAFFIC_CLASS,
		TYPE_FLAGS,
		TYPE_LENGTH,
		TYPE_REQUESTER_ID,
		TYPE_TAG,
		TYPE_LAST_BYTE_ENABLE,
		TYPE_FIRST_BYTE_ENABLE,
		TYPE_ADDRESS_X32,
		TYPE_ADDRESS_X64,
		TYPE_DATA,
		TYPE_COMPLETER_ID,
		TYPE_COMPLETION_STATUS,
		TYPE_BYTE_COUNT,
		TYPE_ERROR
	} m_type;

	//TLP type fields
	enum TlpType
	{
		TYPE_MEM_RD,
		TYPE_MEM_RD_LK,
		TYPE_MEM_WR,
		TYPE_IO_RD,
		TYPE_IO_WR,
		TYPE_CFG_RD_0,
		TYPE_CFG_WR_0,
		TYPE_CFG_RD_1,
		TYPE_CFG_WR_1,
		TYPE_MSG,
		TYPE_MSG_DATA,
		TYPE_COMPLETION,
		TYPE_COMPLETION_DATA,
		TYPE_COMPLETION_LOCKED_ERROR,
		TYPE_COMPLETION_LOCKED_DATA,

		TYPE_INVALID
	};

	//TLP flags
	enum TlpFlags
	{
		FLAG_DIGEST_PRESENT		= 0x80,
		FLAG_POISONED 			= 0x40,
		FLAG_RELAXED_ORDERING	= 0x20,
		FLAG_NO_SNOOP			= 0x10
	};

	uint64_t m_data;

	PCIeTransportSymbol()
	{}

	PCIeTransportSymbol(SymbolType type, uint64_t data = 0)
		: m_type(type)
		, m_data(data)
	{}


	bool operator==(const PCIeTransportSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

class PCIeTransportWaveform : public SparseWaveform<PCIeTransportSymbol>
{
public:
	PCIeTransportWaveform () : SparseWaveform<PCIeTransportSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief Decoder for PCIe transport layer
 */
class PCIeTransportDecoder : public PacketDecoder
{
public:
	PCIeTransportDecoder(const std::string& color);
	virtual ~PCIeTransportDecoder();

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual std::vector<std::string> GetHeaders() override;

	PROTOCOL_DECODER_INITPROC(PCIeTransportDecoder)

	static std::string FormatID(uint16_t id);
};

#endif
