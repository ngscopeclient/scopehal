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
	@brief Declaration of EthernetAutonegotiationPageDecoder
 */

#ifndef EthernetAutonegotiationPageDecoder_h
#define EthernetAutonegotiationPageDecoder_h

#include "PacketDecoder.h"

class EthernetAutonegotiationPageSample
{
public:
	enum stype_t
	{
		//Base page (802.3-2018 28.2.1.2)
		TYPE_BASE_PAGE,

		//Message page (802.3-2018 28.2.3.4.1)
		TYPE_MESSAGE_PAGE,

		//Unformatted page (without known decoding)
		TYPE_UNFORMATTED_PAGE,

		//Acknowledgement (same as the previous codeword, but also with the ACK bit set)
		TYPE_ACK,

		//Unformatted page decodes for specific formats
		TYPE_1000BASET_TECH_0,
		TYPE_1000BASET_TECH_1,
		TYPE_EEE_TECH
	};

	stype_t m_type;

	uint16_t m_value;

	EthernetAutonegotiationPageSample(stype_t t = TYPE_BASE_PAGE, uint16_t v = 0)
	: m_type(t)
	, m_value(v)
	{}
};

class EthernetAutonegotiationPageWaveform : public SparseWaveform<EthernetAutonegotiationPageSample>
{
public:
	EthernetAutonegotiationPageWaveform () : SparseWaveform<EthernetAutonegotiationPageSample>() {};

	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class EthernetAutonegotiationPageDecoder : public PacketDecoder
{
public:
	EthernetAutonegotiationPageDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual std::vector<std::string> GetHeaders() override;
	virtual bool CanMerge(Packet* first, Packet* cur, Packet* next) override;
	virtual Packet* CreateMergedHeader(Packet* pack, size_t i) override;

	PROTOCOL_DECODER_INITPROC(EthernetAutonegotiationPageDecoder)

protected:
};

#endif
