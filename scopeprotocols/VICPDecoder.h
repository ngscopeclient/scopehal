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
	@brief Declaration of VICPDecoder
 */
#ifndef VICPDecoder_h
#define VICPDecoder_h

#include "../scopehal/PacketDecoder.h"

class VICPSymbol
{
public:

	enum FieldType
	{
		TYPE_OPCODE,
		TYPE_VERSION,
		TYPE_SEQ,
		TYPE_RESERVED,
		TYPE_LENGTH,
		TYPE_DATA
	} m_type;

	uint32_t m_data;
	std::string m_str;

	VICPSymbol()
	{}

	VICPSymbol(FieldType type, uint32_t data)
	 : m_type(type)
	 , m_data(data)
	{}

	VICPSymbol(FieldType type, std::string str)
	 : m_type(type)
	 , m_str(str)
	{}

	bool operator== (const VICPSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data) && (m_str == s.m_str);
	}
};

class VICPWaveform : public SparseWaveform<VICPSymbol>
{
public:
	VICPWaveform () : SparseWaveform<VICPSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class VICPDecoder : public PacketDecoder
{
public:
	VICPDecoder(const std::string& color);
	virtual ~VICPDecoder();

	virtual void Refresh() override;

	std::vector<std::string> GetHeaders() override;

	static std::string GetProtocolName();
	virtual bool GetShowDataColumn() override;

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual bool CanMerge(Packet* first, Packet* cur, Packet* next) override;
	virtual Packet* CreateMergedHeader(Packet* pack, size_t i) override;

	PROTOCOL_DECODER_INITPROC(VICPDecoder)

protected:
};

#endif
