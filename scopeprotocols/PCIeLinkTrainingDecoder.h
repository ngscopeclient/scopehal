/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of PCIe2LinkTrainingDecoder
 */

#ifndef PCIe2LinkTrainingDecoder_h
#define PCIe2LinkTrainingDecoder_h

#include "../scopehal/PacketDecoder.h"

class PCIeLinkTrainingSymbol
{
public:

	enum SymbolType
	{
		TYPE_HEADER,
		TYPE_LINK_NUMBER,
		TYPE_LANE_NUMBER,
		TYPE_NUM_FTS,
		TYPE_RATE_ID,
		TYPE_TRAIN_CTL,
		TYPE_EQ,
		TYPE_TS_ID,
		TYPE_ERROR
	} m_type;

	uint8_t m_data;

	PCIeLinkTrainingSymbol()
	{}

	PCIeLinkTrainingSymbol(SymbolType type, uint8_t data = 0)
		: m_type(type)
		, m_data(data)
	{}


	bool operator==(const PCIeLinkTrainingSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

class PCIeLTSSMSymbol
{
public:
	enum SymbolType
	{
		TYPE_DETECT,
		TYPE_POLLING_ACTIVE,
		TYPE_POLLING_CONFIGURATION,
		TYPE_CONFIGURATION,
		TYPE_L0,
		TYPE_RECOVERY_RCVRLOCK,
		TYPE_RECOVERY_SPEED,
		TYPE_RECOVERY_RCVRCFG
	} m_type;

	PCIeLTSSMSymbol()
	{}

	PCIeLTSSMSymbol(SymbolType type)
		: m_type(type)
	{}


	bool operator==(const PCIeLTSSMSymbol& s) const
	{
		return (m_type == s.m_type);
	}
};

//packets stream
class PCIeLinkTrainingWaveform : public SparseWaveform<PCIeLinkTrainingSymbol>
{
public:
	PCIeLinkTrainingWaveform () : SparseWaveform<PCIeLinkTrainingSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

//states stream
class PCIeLTSSMWaveform : public SparseWaveform<PCIeLTSSMSymbol>
{
public:
	PCIeLTSSMWaveform () : SparseWaveform<PCIeLTSSMSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief Decoder for PCIe gen 1/2 link training
 */
class PCIeLinkTrainingDecoder : public PacketDecoder
{
public:
	PCIeLinkTrainingDecoder(const std::string& color);
	virtual ~PCIeLinkTrainingDecoder();

	virtual void Refresh() override;

	virtual std::vector<std::string> GetHeaders() override;
	virtual bool GetShowDataColumn() override;

	static std::string GetProtocolName();

	virtual bool CanMerge(Packet* first, Packet* cur, Packet* next) override;
	virtual Packet* CreateMergedHeader(Packet* pack, size_t i) override;

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(PCIeLinkTrainingDecoder)
};

#endif
