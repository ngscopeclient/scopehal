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
	@brief Declaration of IPv4Decoder
 */
#ifndef IPv4Decoder_h
#define IPv4Decoder_h

class IPv4Symbol
{
public:

	enum SegmentType
	{
		TYPE_ERROR,
		TYPE_VERSION,
		TYPE_HEADER_LEN,
		TYPE_DIFFSERV,
		TYPE_LENGTH,
		TYPE_ID,
		TYPE_FLAGS,
		TYPE_FRAG_OFFSET,
		TYPE_TTL,
		TYPE_PROTOCOL,
		TYPE_HEADER_CHECKSUM,
		TYPE_SOURCE_IP,
		TYPE_DEST_IP,
		TYPE_OPTIONS,
		TYPE_DATA
	} m_type;

	std::vector<uint8_t> m_data;

	IPv4Symbol()
	{}

	IPv4Symbol(SegmentType type, uint8_t value)
		: m_type(type)
	{ m_data.push_back(value); }

	bool operator==(const IPv4Symbol& rhs) const
	{
		return (m_data == rhs.m_data) && (m_type == rhs.m_type);
	}
};

class IPv4Waveform : public SparseWaveform<IPv4Symbol>
{
public:
	IPv4Waveform () : SparseWaveform<IPv4Symbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class IPv4Decoder : public Filter
{
public:
	IPv4Decoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(IPv4Decoder)
};

#endif
