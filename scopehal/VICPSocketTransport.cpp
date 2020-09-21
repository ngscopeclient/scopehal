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
	@brief Implementation of VICPSocketTransport
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VICPSocketTransport::VICPSocketTransport(string args)
	: m_nextSequence(1)
	, m_lastSequence(1)
	, m_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
{
	char hostname[128];
	unsigned int port = 0;
	if(2 != sscanf(args.c_str(), "%127[^:]:%u", hostname, &port))
	{
		//default if port not specified
		m_hostname = args;
		m_port = 1861;
	}
	else
	{
		m_hostname = hostname;
		m_port = port;
	}

	LogDebug("Connecting to VICP oscilloscope at %s:%d\n", m_hostname.c_str(), m_port);

	if(!m_socket.Connect(m_hostname, m_port))
	{
		m_socket.Close();
		LogError("Couldn't connect to socket\n");
		return;
	}
	if(!m_socket.DisableNagle())
	{
		m_socket.Close();
		LogError("Couldn't disable Nagle\n");
		return;
	}

	//Attempt to set a 32 MB RX buffer.
	if(!m_socket.SetRxBuffer(32 * 1024 * 1024))
		LogWarning("Could not set 32 MB RX buffer. Consider increasing /proc/sys/net/core/rmem_max\n");
}

VICPSocketTransport::~VICPSocketTransport()
{
}

bool VICPSocketTransport::IsConnected()
{
	return m_socket.IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string VICPSocketTransport::GetTransportName()
{
	return "vicp";
}

string VICPSocketTransport::GetConnectionString()
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s:%u", m_hostname.c_str(), m_port);
	return string(tmp);
}

uint8_t VICPSocketTransport::GetNextSequenceNumber()
{
	m_lastSequence = m_nextSequence;

	//EOI increments the sequence number.
	//Wrap mod 256, but skip zero!
	m_nextSequence ++;
	if(m_nextSequence == 0)
		m_nextSequence = 1;

	return m_lastSequence;
}

bool VICPSocketTransport::SendCommand(string cmd)
{
	//Operation and flags header
	string payload;
	uint8_t op 	= OP_DATA | OP_EOI;

	//TODO: remote, clear, poll flags
	payload += op;
	payload += 0x01;							//protocol version number
	payload += GetNextSequenceNumber();
	payload += '\0';							//reserved

	//Next 4 header bytes are the message length (network byte order)
	uint32_t len = cmd.length();
	payload += (len >> 24) & 0xff;
	payload += (len >> 16) & 0xff;
	payload += (len >> 8)  & 0xff;
	payload += (len >> 0)  & 0xff;

	//Add message data
	payload += cmd;

	//Actually send it
	SendRawData(payload.size(), (const unsigned char*)payload.c_str());
	return true;
}

string VICPSocketTransport::ReadReply()
{
	string payload;
	while(true)
	{
		//Read the header
		unsigned char header[8];
		ReadRawData(8, header);

		//Sanity check
		if(header[1] != 1)
		{
			LogError("Bad VICP protocol version\n");
			return "";
		}
		if(header[2] != m_lastSequence)
		{
			//LogError("Bad VICP sequence number %d (expected %d)\n", header[2], m_lastSequence);
			//return "";
		}
		if(header[3] != 0)
		{
			LogError("Bad VICP reserved field\n");
			return "";
		}

		//Read the message data
		uint32_t len = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];
		size_t current_size = payload.size();
		payload.resize(current_size + len);
		char* rxbuf = &payload[current_size];
		ReadRawData(len, (unsigned char*)rxbuf);

		//Skip empty blocks, or just newlines
		if( (len == 0) || (rxbuf[0] == '\n' && len == 1))
		{
			//Special handling needed for EOI.
			if(header[0] & OP_EOI)
			{
				//EOI on an empty block is a stop if we have data from previous blocks.
				if(current_size != 0)
					break;

				//But if we have no data, hold off and wait for the next frame
				else
				{
					payload = "";
					continue;
				}
			}
		}

		//Check EOI flag
		if(header[0] & OP_EOI)
			break;
	}

	//make sure there's a null terminator
	payload += "\0";

	return payload;
}

void VICPSocketTransport::SendRawData(size_t len, const unsigned char* buf)
{
	m_socket.SendLooped(buf, len);
}

void VICPSocketTransport::ReadRawData(size_t len, unsigned char* buf)
{
	m_socket.RecvLooped(buf, len);
}

bool VICPSocketTransport::IsCommandBatchingSupported()
{
	return true;
}
