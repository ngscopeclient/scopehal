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
	@brief Declaration of EthernetProtocolDecoder
 */
#ifndef EthernetProtocolDecoder_h
#define EthernetProtocolDecoder_h

#include "../scopehal/ProtocolDecoder.h"
#include "../scopehal/PacketDecoder.h"

/**
	@brief Part of an Ethernet frame (speed doesn't matter)
 */
class EthernetFrameSegment
{
public:
	enum SegmentType
	{
		TYPE_INVALID,
		TYPE_PREAMBLE,
		TYPE_SFD,
		TYPE_DST_MAC,
		TYPE_SRC_MAC,
		TYPE_ETHERTYPE,
		TYPE_VLAN_TAG,
		TYPE_PAYLOAD,
		TYPE_FCS,
		TYPE_INBAND_STATUS,	//RGMII or similar
		TYPE_NO_CARRIER
	} m_type;

	std::vector<uint8_t> m_data;

	EthernetFrameSegment()
	{}

	EthernetFrameSegment(SegmentType type, uint8_t value)
		: m_type(type)
	{ m_data.push_back(value); }

	bool operator==(const EthernetFrameSegment& rhs) const
	{
		return (m_data == rhs.m_data) && (m_type == rhs.m_type);
	}
};

typedef Waveform<EthernetFrameSegment> EthernetWaveform;

class EthernetProtocolDecoder : public PacketDecoder
{
public:
	EthernetProtocolDecoder(std::string color);

	virtual std::string GetText(int i);
	virtual Gdk::Color GetColor(int i);

	virtual bool NeedsConfig();
	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel);

	virtual std::vector<std::string> GetHeaders();

protected:
	void BytesToFrames(
		std::vector<uint8_t>& bytes,
		std::vector<uint64_t>& starts,
		std::vector<uint64_t>& ends,
		EthernetWaveform* cap);
};

#endif
