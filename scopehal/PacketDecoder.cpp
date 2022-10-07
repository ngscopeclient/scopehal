/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

#include "scopehal.h"
#include "PacketDecoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Color schemes

std::string PacketDecoder::m_backgroundColors[PROTO_STANDARD_COLOR_COUNT] =
{
	"#101010",		//PROTO_COLOR_DEFAULT
	"#800000",		//PROTO_COLOR_ERROR
	"#000080",		//PROTO_COLOR_STATUS
	"#808000",		//PROTO_COLOR_CONTROL
	"#336699",		//PROTO_COLOR_DATA_READ
	"#339966",		//PROTO_COLOR_DATA_WRITE
	"#600050",		//PROTO_COLOR_COMMAND
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Packet

Packet::Packet()
	: m_offset(0)
	, m_len(0)
	, m_displayForegroundColor("#ffffff")
	, m_displayBackgroundColor(PacketDecoder::m_backgroundColors[PacketDecoder::PROTO_COLOR_DEFAULT])
{
}

Packet::~Packet()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PacketDecoder::PacketDecoder(const std::string& color, Category cat)
	: Filter(color, cat, Unit(Unit::UNIT_FS))
{
	AddProtocolStream("data");
}

PacketDecoder::~PacketDecoder()
{
	ClearPackets();
}

void PacketDecoder::ClearPackets()
{
	for(auto p : m_packets)
		delete p;
	m_packets.clear();
}

bool PacketDecoder::GetShowDataColumn()
{
	return true;
}

bool PacketDecoder::GetShowImageColumn()
{
	return false;
}

/**
	@brief Checks if multiple packets can be merged under a single heading in the protocol analyzer view.

	This can be used to collapse polling loops, acknowledgements, etc in order to minimize clutter in the view.

	The default implementation in PacketDecoder always returns false so packets are not merged.

	@param first		The first packet in the merge group
	@param cur			The most recently merged packet
	@param next			The candidate packet to be merged

	@return true if packets can be merged, false otherwise
 */
bool PacketDecoder::CanMerge(Packet* /*first*/, Packet* /*cur*/, Packet* /*next*/)
{
	return false;
}

/**
	@brief Creates a summary packet for one or more merged packets

	@param pack		The first packet in the merge string
	@param i		Index of pack within m_packets
 */
Packet* PacketDecoder::CreateMergedHeader(Packet* /*pack*/, size_t /*i*/)
{
	return NULL;
}
