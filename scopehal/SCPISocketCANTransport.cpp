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
	@brief Implementation of SCPISocketCANTransport
 */

#include "scopehal.h"

#ifdef __linux

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/can.h>
#include <linux/can/raw.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPISocketCANTransport::SCPISocketCANTransport(const string& args)
	: m_devname(args)
{
	m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if(m_socket < 0)
	{
		perror("failed to open socket\n");
		LogError("Failed to open CAN interface\n");
		return;
	}

	ifreq ifr;
	strncpy(ifr.ifr_name, args.c_str(), sizeof(ifr.ifr_name) - 1);
	if(0 != ioctl(m_socket, SIOCGIFINDEX, &ifr))
	{
		perror("SIOCGIFINDEX failed\n");
		LogError("Failed to open CAN interface\n");
		return;
	}
	LogTrace("Found CAN interface %s at index %d\n", args.c_str(), ifr.ifr_ifindex);

	sockaddr_can addr;
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if(0 != bind(m_socket, (sockaddr*)&addr, sizeof(addr)))
	{
		perror("bind failed\n");
		LogError("Failed to open CAN interface\n");
		return;
	}

	//set 1ms timeout if no packets
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000;
	if(0 != setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
	{
		perror("setsockopt SO_RCVTIMEO\n");
		LogError("Failed to open set RX timeout\n");
		return;
	}
}

SCPISocketCANTransport::~SCPISocketCANTransport()
{
	close(m_socket);
}

bool SCPISocketCANTransport::IsConnected()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPISocketCANTransport::GetTransportName()
{
	return "socketcan";
}

string SCPISocketCANTransport::GetConnectionString()
{
	return m_devname;
}

bool SCPISocketCANTransport::SendCommand(const string& /*cmd*/)
{
	//read only
	return false;
}

string SCPISocketCANTransport::ReadReply(bool /*endOnSemicolon*/)
{
	return "";
}

void SCPISocketCANTransport::FlushRXBuffer(void)
{
}

void SCPISocketCANTransport::SendRawData(size_t /*len*/, const unsigned char* /*buf*/)
{
}

size_t SCPISocketCANTransport::ReadRawData(size_t len, unsigned char* buf)
{
	return read(m_socket, buf, len);
}

bool SCPISocketCANTransport::IsCommandBatchingSupported()
{
	return true;
}

#endif
