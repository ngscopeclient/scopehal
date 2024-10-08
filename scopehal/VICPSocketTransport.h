/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of VICPSocketTransport
	@ingroup transports
 */

#ifndef VICPSocketTransport_h
#define VICPSocketTransport_h

#include "../xptools/Socket.h"

/**
	@brief A SCPI transport tunneled over LeCroy's Virtual Instrument Control Protocol

	Protocol layer is based on LeCroy's released VICPClient.h, but rewritten and modernized heavily

	@ingroup transports
 */
class VICPSocketTransport : public SCPITransport
{
public:
	VICPSocketTransport(const std::string& args);
	virtual ~VICPSocketTransport();

	virtual std::string GetConnectionString() override;

	///@brief Returns the constant string "vicp"
	static std::string GetTransportName();

	///@brief Returns the hostname of the scope this transport is connected to
	std::string GetHostname()
	{ return m_hostname; }

	virtual bool SendCommand(const std::string& cmd) override;
	virtual std::string ReadReply(bool endOnSemicolon = true) override;
	virtual size_t ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> progress = nullptr) override;
	virtual void SendRawData(size_t len, const unsigned char* buf) override;

	virtual bool IsCommandBatchingSupported() override;
	virtual bool IsConnected() override;

	virtual void FlushRXBuffer() override;

	///@brief VICP header opcode values
	enum HEADER_OPS
	{
		///@brief Data block
		OP_DATA		= 0x80,

		///@brief Not used
		OP_REMOTE	= 0x40,

		///@brief Not used
		OP_LOCKOUT	= 0x20,

		///@brief Not used
		OP_CLEAR	= 0x10,

		///@brief GPIB SRQ signal
		OP_SRQ		= 0x8,

		///@brief GPIB REQ signal
		OP_REQ		= 0x4,

		///@brief GPIB EOI signal
		OP_EOI		= 0x1
	};

	TRANSPORT_INITPROC(VICPSocketTransport)

protected:
	uint8_t GetNextSequenceNumber();

	///@brief Next sequence number
	uint8_t m_nextSequence;

	///@brief Previous sequence number
	uint8_t m_lastSequence;

	///@brief Socket for communicating with the scope
	Socket m_socket;

	///@brief Hostname our socket is connected to
	std::string m_hostname;

	///@brief Port our socket is connected to
	unsigned short m_port;
};

#endif
