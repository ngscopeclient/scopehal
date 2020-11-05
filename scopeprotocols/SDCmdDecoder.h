/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of SDCmdDecoder
 */

#ifndef SDCmdDecoder_h
#define SDCmdDecoder_h

#include "PacketDecoder.h"

class SDCmdSymbol
{
public:
	enum stype
	{
		TYPE_HEADER,
		TYPE_COMMAND,
		TYPE_COMMAND_ARGS,
		TYPE_RESPONSE_ARGS,
		TYPE_CRC_OK,
		TYPE_CRC_BAD,

		TYPE_ERROR
	};

	SDCmdSymbol()
	{}

	SDCmdSymbol(stype t, uint32_t d, uint32_t e=0, uint32_t f=0, uint32_t g=0)
	 : m_stype(t)
	 , m_data(d)
	 , m_extdata1(e)
	 , m_extdata2(f)
	 , m_extdata3(g)
	{}

	stype m_stype;
	uint32_t m_data;

	//Extended data for a few special responses
	uint32_t m_extdata1;
	uint32_t m_extdata2;
	uint32_t m_extdata3;

	bool operator== (const SDCmdSymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data) &&
			(m_extdata1 == s.m_extdata1) && (m_extdata2 == s.m_extdata2) && (m_extdata3 == s.m_extdata3);
	}
};

typedef Waveform<SDCmdSymbol> SDCmdWaveform;


/**
	@brief Decodes the SD card command bus protocol
 */
class SDCmdDecoder : public PacketDecoder
{
public:
	SDCmdDecoder(const std::string& color);

	virtual void Refresh();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual bool NeedsConfig();

	virtual std::string GetText(int i);
	virtual Gdk::Color GetColor(int i);

	virtual bool GetShowDataColumn();

	std::vector<std::string> GetHeaders();

	virtual bool CanMerge(Packet* first, Packet* cur, Packet* next);
	virtual Packet* CreateMergedHeader(Packet* pack, size_t i);

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	enum CardType
	{
		SD_GENERIC,
		SD_EMMC
	};

	PROTOCOL_DECODER_INITPROC(SDCmdDecoder)

protected:
	std::string m_cardtypename;
};

#endif
