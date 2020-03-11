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
	@brief Declaration of VICPSocketTransport
 */

#ifndef VICPSocketTransport_h
#define VICPSocketTransport_h

#include "../xptools/Socket.h"

/**
	@brief A SCPI transport tunneled over LeCroy's Virtual Instrument Control Protocol
 */
class VICPSocketTransport : public SCPITransport
{
public:
	VICPSocketTransport(std::string hostname, unsigned short port);
	virtual ~VICPSocketTransport();

	virtual std::string GetConnectionString();

	virtual bool SendCommand(std::string cmd);
	virtual std::string ReadReply();
	virtual void ReadRawData(size_t len, unsigned char* buf);
	virtual void SendRawData(size_t len, const unsigned char* buf);

	//VICP constant helpers
	enum HEADER_OPS
	{
		OP_DATA		= 0x80,
		OP_REMOTE	= 0x40,
		OP_LOCKOUT	= 0x20,
		OP_CLEAR	= 0x10,
		OP_SRQ		= 0x8,
		OP_REQ		= 0x4,
		OP_EOI		= 0x1
	};

protected:
	uint8_t GetNextSequenceNumber();

	uint8_t m_nextSequence;
	uint8_t m_lastSequence;

	Socket m_socket;

	std::string m_hostname;
	unsigned short m_port;
};

#endif
