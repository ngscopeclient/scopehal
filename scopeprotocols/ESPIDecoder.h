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
	@brief Declaration of ESPIDecoder
 */
#ifndef ESPIDecoder_h
#define ESPIDecoder_h

#include "../scopehal/PacketDecoder.h"

class ESPISymbol
{
public:

	enum ESpiType
	{
		TYPE_COMMAND_TYPE,

		TYPE_CAPS_ADDR,

		TYPE_COMMAND_DATA_8,
		TYPE_COMMAND_DATA_32,
		TYPE_COMMAND_CRC_GOOD,
		TYPE_COMMAND_CRC_BAD,

		TYPE_RESPONSE_OP,
		TYPE_RESPONSE_STATUS,
		TYPE_RESPONSE_DATA_32,
		TYPE_RESPONSE_CRC_GOOD,
		TYPE_RESPONSE_CRC_BAD,

		TYPE_VWIRE_COUNT,
		TYPE_VWIRE_INDEX,
		TYPE_VWIRE_DATA,

		TYPE_GENERAL_CAPS_RD,
		TYPE_GENERAL_CAPS_WR,
		TYPE_CH0_CAPS_RD,
		TYPE_CH0_CAPS_WR,
		TYPE_CH1_CAPS_RD,
		TYPE_CH1_CAPS_WR,
		TYPE_CH2_CAPS_RD,
		TYPE_CH2_CAPS_WR,

		TYPE_REQUEST_TAG,
		TYPE_REQUEST_LEN,

		TYPE_FLASH_REQUEST_TYPE,
		TYPE_FLASH_REQUEST_ADDR,
		TYPE_FLASH_REQUEST_DATA,

		TYPE_SMBUS_REQUEST_TYPE,
		TYPE_SMBUS_REQUEST_ADDR,
		TYPE_SMBUS_REQUEST_DATA,

		TYPE_IO_ADDR,
		TYPE_IO_DATA,

		TYPE_WAIT,

		TYPE_COMPLETION_TYPE,
		TYPE_COMPLETION_DATA,

		TYPE_ERROR
	} m_type;

	//Table 3
	enum ESpiCommand
	{
		//Table 6
		COMMAND_PUT_PC					= 0x00,
		COMMAND_GET_PC					= 0x01,
		COMMAND_PUT_NP					= 0x02,
		COMMAND_GET_NP					= 0x03,
		COMMAND_PUT_OOB					= 0x06,
		COMMAND_GET_OOB					= 0x07,
		COMMAND_PUT_FLASH_C				= 0x08,
		COMMAND_GET_FLASH_NP			= 0x09,

		//Figure 37
		COMMAND_PUT_IORD_SHORT_x1		= 0x40,
		COMMAND_PUT_IORD_SHORT_x2		= 0x41,
		COMMAND_PUT_IORD_SHORT_x4		= 0x43,
		COMMAND_PUT_IOWR_SHORT_x1		= 0x44,
		COMMAND_PUT_IOWR_SHORT_x2		= 0x45,
		COMMAND_PUT_IOWR_SHORT_x4		= 0x47,
		COMMAND_PUT_MEMRD32_SHORT_x1	= 0x48,
		COMMAND_PUT_MEMRD32_SHORT_x2	= 0x49,
		COMMAND_PUT_MEMRD32_SHORT_x4	= 0x4b,
		COMMAND_PUT_MEMWR32_SHORT_x1	= 0x4c,
		COMMAND_PUT_MEMWR32_SHORT_x2	= 0x4d,
		COMMAND_PUT_MEMWR32_SHORT_x4	= 0x4f,

		//Figure 40
		COMMAND_PUT_VWIRE				= 0x04,
		COMMAND_GET_VWIRE				= 0x05,

		COMMAND_GET_STATUS				= 0x25,
		COMMAND_SET_CONFIGURATION		= 0x22,
		COMMAND_GET_CONFIGURATION		= 0x21,
		COMMAND_RESET					= 0xff,

		COMMAND_NONE					= 0x100
	};

	//Table 4
	enum ESpiResponse
	{
		RESPONSE_DEFER			= 0x1,
		RESPONSE_NONFATAL_ERROR	= 0x2,
		RESPONSE_FATAL_ERROR	= 0x3,
		RESPONSE_ACCEPT			= 0x8,
		RESPONSE_NONE			= 0xf	//also NO_RESPONSE with other bits high
	};

	enum ESpiCompletion
	{
		COMPLETION_NONE			= 0,
		COMPLETION_PERIPHERAL	= 1,
		COMPLETION_VWIRE		= 2,
		COMPLETION_FLASH		= 3
	};

	//Table 6
	enum ESpiCycleType
	{
		CYCLE_READ				= 0,
		CYCLE_WRITE				= 1,
		CYCLE_ERASE				= 2,

		CYCLE_SMBUS				= 0x21,

		// completion types
		CYCLE_SUCCESS_NODATA		= 0x06,
		CYCLE_SUCCESS_DATA_MIDDLE	= 0x09,
		CYCLE_SUCCESS_DATA_FIRST	= 0x0b,
		CYCLE_SUCCESS_DATA_LAST		= 0x0d,
		CYCLE_SUCCESS_DATA_ONLY		= 0x0f,

		CYCLE_FAIL_LAST				= 0x08,
		CYCLE_FAIL_ONLY				= 0x0e
	};

	uint64_t m_data;
	ESPISymbol()
	{}

	ESPISymbol(ESpiType type, uint64_t data = 0)
	 : m_type(type)
	 , m_data(data)
	{}

	bool operator== (const ESPISymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

class ESPIWaveform : public SparseWaveform<ESPISymbol>
{
public:
	ESPIWaveform () : SparseWaveform<ESPISymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief Decoder for Intel Enhanced Serial Peripheral Interface (eSPI)

	Reference: Enhanced Serial Peripheral Interface (eSPI) Base Specification (Intel document 327432-004)
 */
class ESPIDecoder : public PacketDecoder
{
public:
	ESPIDecoder(const std::string& color);

	virtual void Refresh() override;

	std::vector<std::string> GetHeaders() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual bool CanMerge(Packet* first, Packet* cur, Packet* next) override;
	virtual Packet* CreateMergedHeader(Packet* pack, size_t i) override;

	PROTOCOL_DECODER_INITPROC(ESPIDecoder)

protected:
	uint8_t UpdateCRC8(uint8_t crc, uint8_t data);

	enum BusWidth
	{
		BUS_WIDTH_AUTO,
		BUS_WIDTH_X1,
		BUS_WIDTH_X4
	};

	std::string m_busWidthName;
};

#endif
