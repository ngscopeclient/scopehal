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
	@brief Declaration of MilStd1553Decoder
 */
#ifndef MilStd1553Decoder_h
#define MilStd1553Decoder_h

class MilStd1553Symbol
{
public:
	enum stype
	{
		TYPE_SYNC_CTRL_STAT,
		TYPE_SYNC_DATA,
		TYPE_DATA,

		TYPE_RT_ADDR,
		TYPE_DIRECTION,
		TYPE_SUB_ADDR,
		TYPE_LENGTH,

		TYPE_PARITY_OK,
		TYPE_PARITY_BAD,

		TYPE_MSG_OK,
		TYPE_MSG_ERR,

		TYPE_TURNAROUND,

		TYPE_STATUS,

		TYPE_ERROR
	};

	enum statbits
	{
		STATUS_SERVICE_REQUEST	= 0x01,
		STATUS_MALFORMED		= 0x02,
		STATUS_BROADCAST_ACK	= 0x04,
		STATUS_BUSY				= 0x08,
		STATUS_SUBSYS_FAULT		= 0x10,
		STATUS_DYN_ACCEPT		= 0x20,
		STATUS_RT_FAULT			= 0x40,

		STATUS_ANY_FAULT		= STATUS_MALFORMED | STATUS_SUBSYS_FAULT | STATUS_RT_FAULT
	};

	MilStd1553Symbol()
	{}

	MilStd1553Symbol(stype t, uint16_t d)
	 : m_stype(t)
	 , m_data(d)
	{}

	stype m_stype;
	uint16_t m_data;

	bool operator== (const MilStd1553Symbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data);
	}
};

class MilStd1553Waveform : public SparseWaveform<MilStd1553Symbol>
{
public:
	MilStd1553Waveform () : SparseWaveform<MilStd1553Symbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class MilStd1553Decoder : public PacketDecoder
{
public:
	MilStd1553Decoder(const std::string& color);
	virtual ~MilStd1553Decoder();

	virtual void Refresh() override;
	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	std::vector<std::string> GetHeaders() override;

	PROTOCOL_DECODER_INITPROC(MilStd1553Decoder)
};

#endif
