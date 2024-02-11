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
	@brief Declaration of PCIeDataLinkDecoder
 */

#ifndef PCIeDataLinkDecoder_h
#define PCIeDataLinkDecoder_h

#include "../scopehal/PacketDecoder.h"

class PCIeDataLinkSymbol
{
public:

	//Data link symbols
	enum SymbolType
	{
		TYPE_DLLP_TYPE,
		TYPE_DLLP_VC,
		TYPE_DLLP_DATA,
		TYPE_DLLP_CRC_OK,
		TYPE_DLLP_CRC_BAD,

		TYPE_DLLP_SEQUENCE,
		TYPE_DLLP_HEADER_CREDITS,
		TYPE_DLLP_DATA_CREDITS,

		TYPE_TLP_SEQUENCE,
		TYPE_TLP_CRC_OK,
		TYPE_TLP_CRC_BAD,
		TYPE_TLP_DATA,

		TYPE_ERROR
	} m_type;

	//DLLP byte 0 type fields (PCIe 2.0 Base Spec table 3-1)
	enum DLLPType
	{
		DLLP_TYPE_ACK 							= 0x00,
		DLLP_TYPE_NAK							= 0x10,
		DLLP_TYPE_PM_ENTER_L1					= 0x20,
		DLLP_TYPE_PM_ENTER_L23					= 0x21,
		DLLP_TYPE_PM_ACTIVE_STATE_REQUEST_L1	= 0x23,
		DLLP_TYPE_PM_REQUEST_ACK				= 0x24,
		DLLP_TYPE_VENDOR_SPECIFIC				= 0x30,

		DLLP_TYPE_INITFC1_P						= 0x40,
		DLLP_TYPE_INITFC1_NP					= 0x50,
		DLLP_TYPE_INITFC1_CPL					= 0x60,
		DLLP_TYPE_INITFC2_P						= 0xc0,
		DLLP_TYPE_INITFC2_NP					= 0xd0,
		DLLP_TYPE_INITFC2_CPL					= 0xe0,
		DLLP_TYPE_UPDATEFC_P					= 0x80,
		DLLP_TYPE_UPDATEFC_NP					= 0x90,
		DLLP_TYPE_UPDATEFC_CPL					= 0xa0
	};

	uint32_t m_data;

	PCIeDataLinkSymbol()
	{}

	PCIeDataLinkSymbol(SymbolType type, uint32_t data = 0)
		: m_type(type)
		, m_data(data)
	{}


	bool operator==(const PCIeDataLinkSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

class PCIeDataLinkWaveform : public SparseWaveform<PCIeDataLinkSymbol>
{
public:
	PCIeDataLinkWaveform () : SparseWaveform<PCIeDataLinkSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief Decoder for PCIe data link layer
 */
class PCIeDataLinkDecoder : public PacketDecoder
{
public:
	PCIeDataLinkDecoder(const std::string& color);
	virtual ~PCIeDataLinkDecoder();

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual std::vector<std::string> GetHeaders() override;

	enum FramingMode
	{
		MODE_GEN12,
		MODE_GEN345
	};

	PROTOCOL_DECODER_INITPROC(PCIeDataLinkDecoder)

protected:
	uint16_t CalculateDllpCRC(uint8_t type, uint8_t* data);
	uint32_t CalculateTlpCRC(Packet* pack);

	std::string m_framingMode;
};

#endif
