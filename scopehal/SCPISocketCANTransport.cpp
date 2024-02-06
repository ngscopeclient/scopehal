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
{
	/*
	char hostname[128];
	unsigned int port = 0;
	if(2 != sscanf(args.c_str(), "%127[^:]:%u", hostname, &port))
	{
		//default if port not specified
		m_hostname = args;
		m_port = 5025;
	}
	else
	{
		m_hostname = hostname;
		m_port = port;
	}
	*/
}

SCPISocketCANTransport::~SCPISocketCANTransport()
{
}

bool SCPISocketCANTransport::IsConnected()
{
	true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPISocketCANTransport::GetTransportName()
{
	return "socketcan";
}

string SCPISocketCANTransport::GetConnectionString()
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s:%u", m_devname.c_str(), m_baudrate);
	return string(tmp);
}

bool SCPISocketCANTransport::SendCommand(const string& /*cmd*/)
{
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

size_t SCPISocketCANTransport::ReadRawData(size_t /*len*/, unsigned char* /*buf*/)
{
	return 0;
}

bool SCPISocketCANTransport::IsCommandBatchingSupported()
{
	return true;
}

#endif
