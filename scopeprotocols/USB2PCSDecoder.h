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
	@brief Declaration of USB2PCSDecoder
 */
#ifndef USB2PCSDecoder_h
#define USB2PCSDecoder_h

#include "USB2PMADecoder.h"

/**
	@brief A single symbol at the PCS layer (byte or command)
 */
class USB2PCSSymbol
{
public:

	enum SymbolType
	{
		TYPE_SYNC,
		TYPE_EOP,
		TYPE_RESET,
		//TODO: handle suspend (idle for >3 ms)
		//TODO: handle resume
		TYPE_DATA,
		TYPE_ERROR
	};

	USB2PCSSymbol(SymbolType type = TYPE_SYNC, uint8_t data = 0)
	 : m_type(type)
	 , m_data(data)
	{
	}

	SymbolType m_type;
	uint8_t m_data;

	bool operator==(const USB2PCSSymbol& rhs) const
	{
		return (m_type == rhs.m_type);
	}
};

class USB2PCSWaveform : public SparseWaveform<USB2PCSSymbol>
{
public:
	USB2PCSWaveform () : SparseWaveform<USB2PCSSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class USB2PCSDecoder : public Filter
{
public:
	USB2PCSDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(USB2PCSDecoder)

protected:
	enum BusSpeed
	{
		SPEED_1M,
		SPEED_12M,
		SPEED_480M
	};

	enum DecodeState
	{
		STATE_IDLE,
		STATE_SYNC,
		STATE_DATA
	};

	void RefreshIterationIdle(
		size_t nin,
		DecodeState& state,
		BusSpeed& speed,
		size_t& ui_width,
		USB2PCSWaveform* cap,
		USB2PMAWaveform* din,
		size_t& count,
		int64_t& offset
		);

	void RefreshIterationSync(
		size_t nin,
		DecodeState& state,
		BusSpeed speed,
		size_t& ui_width,
		USB2PCSWaveform* cap,
		USB2PMAWaveform* din,
		size_t& count,
		int64_t& offset,
		uint8_t& data,
		bool& first
		);

	void RefreshIterationData(
		size_t nin,
		size_t nlast,
		DecodeState& state,
		BusSpeed speed,
		size_t& ui_width,
		USB2PCSWaveform* cap,
		USB2PMAWaveform* din,
		size_t& count,
		int64_t& offset,
		uint8_t& data);
};

#endif
