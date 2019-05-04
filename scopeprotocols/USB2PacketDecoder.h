/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of USB2PacketDecoder
 */
#ifndef USB2PacketDecoder_h
#define USB2PacketDecoder_h

#include "../scopehal/PacketDecoder.h"
#include "USB2PMADecoder.h"

/**
	@brief Part of a packet
 */
class USB2PacketSymbol
{
public:

	enum SymbolType
	{
		TYPE_PID,
		TYPE_ADDR,
		TYPE_ENDP,
		TYPE_CRC5,
		TYPE_CRC16,
		TYPE_NFRAME,
		TYPE_DATA,
		TYPE_ERROR
	};

	enum Pids
	{
		PID_RESERVED	= 0x0,
		PID_OUT			= 0x1,
		PID_ACK 		= 0x2,
		PID_DATA0		= 0x3,
		PID_PING 		= 0x4,
		PID_SOF			= 0x5,
		PID_NYET		= 0x6,
		PID_DATA2		= 0x7,
		PID_SPLIT 		= 0x8,
		PID_IN			= 0x9,
		PID_NAK			= 0xa,
		PID_DATA1		= 0xb,
		PID_PRE_ERR 	= 0xc,
		PID_SETUP		= 0xd,
		PID_STALL		= 0xe,
		PID_MDATA		= 0xf
	};

	USB2PacketSymbol(SymbolType type = TYPE_PID, uint16_t data=0)
	 : m_type(type)
	 , m_data(data)
	{
	}

	SymbolType m_type;
	uint16_t m_data;	//frame number is >1 byte
						//in all other cases only low byte is meaningful

	bool operator==(const USB2PacketSymbol& rhs) const
	{
		return (m_type == rhs.m_type);
	}
};

typedef OscilloscopeSample<USB2PacketSymbol> USB2PacketSample;
typedef CaptureChannel<USB2PacketSymbol> USB2PacketCapture;

class USB2PacketDecoder : public PacketDecoder
{
public:
	USB2PacketDecoder(std::string color);

	virtual void Refresh();
	virtual ChannelRenderer* CreateRenderer();

	virtual bool NeedsConfig();
	virtual bool IsOverlay();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual double GetVoltageRange();

	virtual std::vector<std::string> GetHeaders();
	virtual bool GetShowDataColumn();

	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel);

	PROTOCOL_DECODER_INITPROC(USB2PacketDecoder)

protected:
	void FindPackets(USB2PacketCapture* cap);
	void DecodeSof(USB2PacketCapture* cap, USB2PacketSample& start, size_t& i);
	void DecodeSetup(USB2PacketCapture* cap, USB2PacketSample& start, size_t& i);
	void DecodeData(USB2PacketCapture* cap, USB2PacketSample& start, size_t& i);
};

#endif
