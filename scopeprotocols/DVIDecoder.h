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
	@brief Declaration of DVIDecoder
 */

#ifndef DVIDecoder_h
#define DVIDecoder_h

#include "../scopehal/PacketDecoder.h"

class DVISymbol
{
public:
	enum DVIType
	{
		DVI_TYPE_PREAMBLE,
		DVI_TYPE_HSYNC,
		DVI_TYPE_VSYNC,
		DVI_TYPE_VIDEO,
		DVI_TYPE_ERROR
	};

	//default for STL
	DVISymbol()
	{}

	DVISymbol(DVIType type, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0)
	 : m_type(type)
	 , m_red(r)
	 , m_green(g)
	 , m_blue(b)
	{}

	DVIType m_type;
	uint8_t m_red;
	uint8_t m_green;
	uint8_t m_blue;

	bool operator== (const DVISymbol& s) const
	{
		return (m_type == s.m_type) && (m_red == s.m_red) && (m_green == s.m_green) && (m_blue == s.m_blue);
	}
};

class VideoScanlinePacket : public Packet
{
public:
	virtual ~VideoScanlinePacket();
};

typedef Waveform<DVISymbol> DVIWaveform;

class DVIDecoder : public PacketDecoder
{
public:
	DVIDecoder(std::string color);

	virtual std::string GetText(int i);
	virtual Gdk::Color GetColor(int i);

	virtual void Refresh();
	virtual bool NeedsConfig();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual bool GetShowImageColumn();

	virtual std::vector<std::string> GetHeaders();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	PROTOCOL_DECODER_INITPROC(DVIDecoder)

protected:
};

#endif
