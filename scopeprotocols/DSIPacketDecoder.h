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
	@brief Declaration of DSIPacketDecoder
 */
#ifndef DSIPacketDecoder_h
#define DSIPacketDecoder_h

#include "PacketDecoder.h"

class DSISymbol
{
public:

	enum stype
	{
		TYPE_VC,
		TYPE_IDENTIFIER,
		TYPE_LEN,
		TYPE_DATA,
		TYPE_ECC_OK,
		TYPE_ECC_BAD,
		TYPE_CHECKSUM_OK,
		TYPE_CHECKSUM_BAD,
		TYPE_ERROR
	} m_stype;

	uint16_t m_data;

	DSISymbol(stype t = TYPE_ERROR, uint16_t data = 0)
	 : m_stype(t)
	 , m_data(data)
	{}

	bool operator== (const DSISymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data);
	}
};

class DSIWaveform : public SparseWaveform<DSISymbol>
{
public:
	DSIWaveform () : SparseWaveform<DSISymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief Decodes MIPI DSI from a D-PHY data stream
 */
class DSIPacketDecoder : public PacketDecoder
{
public:
	DSIPacketDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	std::vector<std::string> GetHeaders() override;

	virtual Packet* CreateMergedHeader(Packet* pack, size_t i) override;
	virtual bool CanMerge(Packet* first, Packet* cur, Packet* next) override;

	//From table 16
	enum
	{
		//Short
		TYPE_VSYNC_START				= 0x01,
		TYPE_VSYNC_END					= 0x11,
		TYPE_HSYNC_START				= 0x21,
		TYPE_HSYNC_END					= 0x31,
		TYPE_EOTP						= 0x08,
		TYPE_CM_OFF						= 0x02,
		TYPE_CM_ON						= 0x12,
		TYPE_SHUT_DOWN					= 0x22,
		TYPE_TURN_ON					= 0x32,
		TYPE_GENERIC_SHORT_WRITE_0PARAM	= 0x03,
		TYPE_GENERIC_SHORT_WRITE_1PARAM	= 0x13,
		TYPE_GENERIC_SHORT_WRITE_2PARAM	= 0x23,
		TYPE_GENERIC_READ_0PARAM		= 0x04,
		TYPE_GENERIC_READ_1PARAM		= 0x14,
		TYPE_GENERIC_READ_2PARAM		= 0x24,
		TYPE_DCS_SHORT_WRITE_0PARAM		= 0x05,
		TYPE_DCS_SHORT_WRITE_1PARAM		= 0x15,
		TYPE_DCS_READ					= 0x06,
		TYPE_SET_MAX_RETURN_SIZE		= 0x37,

		//Long
		TYPE_NULL						= 0x09,
		TYPE_BLANKING					= 0x19,
		TYPE_GENERIC_LONG_WRITE			= 0x29,
		TYPE_DCS_LONG_WRITE				= 0x39,
		TYPE_PACKED_PIXEL_RGB565		= 0x0e,
		TYPE_PACKED_PIXEL_RGB666		= 0x1e,
		TYPE_LOOSE_PIXEL_RGB666			= 0x2e,
		TYPE_PACKED_PIXEL_RGB888		= 0x3e
	};

protected:
	uint16_t UpdateCRC(uint16_t crc, uint8_t data);

public:
	PROTOCOL_DECODER_INITPROC(DSIPacketDecoder)
};

#endif
