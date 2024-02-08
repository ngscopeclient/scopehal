/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@author Tom Verbeure
	@brief Declaration of SCPILxiTransport
 */

#ifdef HAS_LXI

#ifndef SCPILxiTransport_h
#define SCPILxiTransport_h

/**
	@brief Abstraction of a transport layer for moving SCPI data between endpoints
 */
class SCPILxiTransport : public SCPITransport
{
public:
	SCPILxiTransport(const std::string& args);
	virtual ~SCPILxiTransport();

	//not copyable or assignable
	SCPILxiTransport(const SCPILxiTransport&) =delete;
	SCPILxiTransport& operator=(const SCPILxiTransport&) =delete;

	virtual std::string GetConnectionString() override;
	static std::string GetTransportName();

	virtual bool SendCommand(const std::string& cmd) override;
	virtual std::string ReadReply(bool endOnSemicolon = true) override;
	virtual size_t ReadRawData(size_t len, unsigned char* buf) override;
	virtual void SendRawData(size_t len, const unsigned char* buf) override;

	virtual bool IsCommandBatchingSupported() override;
	virtual bool IsConnected() override;

	TRANSPORT_INITPROC(SCPILxiTransport)

	std::string GetHostname()
	{ return m_hostname; }

	virtual void FlushRXBuffer() override;

protected:
	static bool m_lxi_initialized;

	std::string m_hostname;
	unsigned short m_port;

	int m_device;
	int m_timeout;

	int m_staging_buf_size;
	unsigned char *m_staging_buf;
	int m_data_in_staging_buf;
	int m_data_offset;
	bool m_data_depleted;
};

#endif

#endif
