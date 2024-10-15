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
	@brief Declaration of SCPISocketTransport
	@ingroup transports
 */

#ifndef SCPISocketTransport_h
#define SCPISocketTransport_h

#include "../xptools/Socket.h"

/**
	@brief Transport that moves SCPI data over a single TCP socket with no framing

	@ingroup transports
 */
class SCPISocketTransport : public SCPITransport
{
public:
	SCPISocketTransport(const std::string& args);
	SCPISocketTransport(const std::string& hostname, unsigned short port);
	virtual ~SCPISocketTransport();

	virtual std::string GetConnectionString() override;
	static std::string GetTransportName();

	virtual void FlushRXBuffer(void) override;
	virtual bool SendCommand(const std::string& cmd) override;
	virtual std::string ReadReply(bool endOnSemicolon = true, std::function<void(float)> progress = nullptr) override;
	virtual size_t ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> progress = nullptr) override;
	virtual void SendRawData(size_t len, const unsigned char* buf) override;

	virtual bool IsCommandBatchingSupported() override;
	virtual bool IsConnected() override;

	TRANSPORT_INITPROC(SCPISocketTransport)

	///@brief Returns the hostname of the connected instrument
	const std::string& GetHostname()
	{ return m_hostname; }

	///@brief Returns the port number of the connected instrument
	unsigned short GetPort()
	{ return m_port; }

	/**
		@brief Sets timeouts for the connection

		@param txUs		Send timeout, in microseconds
		@param rxUs		Receive timeout, in microseconds
	 */
	void SetTimeouts(unsigned int txUs, unsigned int rxUs)
	{
		m_socket.SetTxTimeout(txUs);
		m_socket.SetRxTimeout(rxUs);
	}

protected:

	void SharedCtorInit();

	///@brief The socket for commands
	Socket m_socket;

	///@brief IP or hostname of the instrument
	std::string m_hostname;

	///@brief TCP port number of the instrument
	unsigned short m_port;
};

#endif
