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

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPITwinLanTransport::SCPITwinLanTransport(const string& args)
	: SCPISocketTransport(args)
	, m_dataport(5026)
	, m_secondarysocket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
{
	//Figure out the data port number
	char hostname[128] = "";
	char hostname2[128] = "";
	unsigned int port = 0;
	unsigned int dport;
	if(4 == sscanf(args.c_str(), "%127[^:]:%u:%127[^:]%u", hostname, &port, hostname2, &dport))
		m_dataport = dport;
	else if(3 == sscanf(args.c_str(), "%127[^:]:%u:%u", hostname, &port, &dport))
	{
		m_dataport = dport;
		strncpy(hostname2, hostname, sizeof(hostname));
	}
	else if(2 == sscanf(args.c_str(), "%127[^:]:%u", hostname, &port))
	{
		m_dataport = port+1;
		strncpy(hostname2, hostname, sizeof(hostname));
	}

	//Connect the secondary socket
	LogDebug("Connecting to data plane socket\n");
	m_secondarysocket.Connect(hostname2, m_dataport);
	m_secondarysocket.DisableNagle();
}

SCPITwinLanTransport::~SCPITwinLanTransport()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Discovery

string SCPITwinLanTransport::GetTransportName()
{
	return "twinlan";
}

string SCPITwinLanTransport::GetConnectionString()
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s:%u:%u", m_hostname.c_str(), m_port, m_dataport);
	return string(tmp);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Secondary socket I/O

size_t SCPITwinLanTransport::ReadRawData(size_t len, unsigned char* buf)
{
	if(m_secondarysocket.RecvLooped(buf, len))
		return len;
	else
		return 0;
}

void SCPITwinLanTransport::SendRawData(size_t len, const unsigned char* buf)
{
	m_secondarysocket.SendLooped(buf, len);
}
