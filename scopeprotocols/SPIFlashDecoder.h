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
	@brief Declaration of SPIFlashDecoder
 */
#ifndef SPIFlashDecoder_h
#define SPIFlashDecoder_h

#include "../scopehal/PacketDecoder.h"

class SPIFlashSymbol
{
public:

	enum FlashType
	{
		//Generic
		TYPE_COMMAND,
		TYPE_ADDRESS,
		TYPE_DATA,

		TYPE_DUMMY,

		//ID codes
		TYPE_VENDOR_ID,
		TYPE_PART_ID,

		//Winbond W25N specific
		TYPE_W25N_BLOCK_ADDR,
		TYPE_W25N_SR_ADDR,			//address of a status register
		TYPE_W25N_SR_STATUS,
		TYPE_W25N_SR_CONFIG,
		TYPE_W25N_SR_PROT

	} m_type;

	enum FlashCommand
	{
		CMD_READ_STATUS_REGISTER,
		CMD_READ_STATUS_REGISTER_1,
		CMD_READ_STATUS_REGISTER_2,
		CMD_READ_STATUS_REGISTER_3,
		CMD_WRITE_STATUS_REGISTER,
		CMD_READ_JEDEC_ID,
		CMD_READ,			//Read, SPI address, SPI data
		CMD_FAST_READ,		//Fast read, SPI mode, with pipeline delay
		CMD_READ_1_1_4,		//Fast read, SPI address, QSPI data
		CMD_READ_1_4_4,		//Fast read, QSPI address, QSPI data
		CMD_RESET,
		CMD_WRITE_ENABLE,
		CMD_WRITE_DISABLE,
		CMD_BLOCK_ERASE,
		CMD_PAGE_PROGRAM,
		CMD_QUAD_PAGE_PROGRAM,
		CMD_READ_SFDP,		//read serial flash discovery parameters
		CMD_ADDR_32BIT,
		CMD_ADDR_24BIT,
		CMD_RELEASE_PD,
		CMD_ENABLE_RESET,

		//Winbond W25N specific
		CMD_W25N_READ_PAGE,
		CMD_W25N_PROGRAM_EXECUTE,

		CMD_UNKNOWN
	} m_cmd;

	uint32_t m_data;

	SPIFlashSymbol()
	{}

	SPIFlashSymbol(FlashType type, FlashCommand cmd, uint32_t data)
	 : m_type(type)
	 , m_cmd(cmd)
	 , m_data(data)
	{}

	bool operator== (const SPIFlashSymbol& s) const
	{
		return (m_type == s.m_type) && (m_cmd == s.m_cmd) && (m_data == s.m_data);
	}
};

class SPIFlashWaveform : public SparseWaveform<SPIFlashSymbol>
{
public:
	SPIFlashWaveform () : SparseWaveform<SPIFlashSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class SPIFlashDecoder : public PacketDecoder
{
public:
	SPIFlashDecoder(const std::string& color);
	virtual ~SPIFlashDecoder();

	virtual void Refresh() override;

	std::vector<std::string> GetHeaders() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	enum VendorIDs
	{
		VENDOR_ID_CYPRESS	= 0x01,
		VENDOR_ID_MICRON	= 0x20,
		VENDOR_ID_WINBOND	= 0xef
	};

	enum FlashType
	{
		FLASH_TYPE_GENERIC_3BYTE_ADDRESS,
		FLASH_TYPE_GENERIC_4BYTE_ADDRESS,
		FLASH_TYPE_WINBOND_W25N
	};

	virtual bool CanMerge(Packet* first, Packet* cur, Packet* next) override;
	virtual Packet* CreateMergedHeader(Packet* pack, size_t i) override;

	PROTOCOL_DECODER_INITPROC(SPIFlashDecoder)


	static std::string GetPartID(SPIFlashWaveform* cap, const SPIFlashSymbol& s, int i);

protected:
	std::string m_typename;
	std::string m_outfile;

	std::string m_cachedfname;
	FILE* m_fpOut;
};

#endif
