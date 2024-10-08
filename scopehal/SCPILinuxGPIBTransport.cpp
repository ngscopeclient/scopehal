/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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
	@author Mike Walters
	@brief Implementation of SCPILinuxGPIBTransport
 */

#ifdef HAS_LINUXGPIB

#include <stdio.h>
#include <stdlib.h>
//#include <string.h>

#include "scopehal.h"

#include <gpib/ib.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPILinuxGPIBTransport::SCPILinuxGPIBTransport(const string& args)
	: m_devicePath(args)
{
	auto result = sscanf(args.c_str(), "%d:%d:%d:%d", &m_board_index, &m_pad, &m_sad, &m_timeout);
	if (result < 2) {
		LogError("Invalid device string, must specify at least board index and primary address");
		return;
	}

	LogDebug("Connecting to SCPI oscilloscope over GPIB%d with address %d:%d\n",
		m_board_index,
		m_pad,
		m_sad
	);

	m_handle = ibdev(m_board_index, m_pad, m_sad, m_timeout, 0, 0);
	if (m_handle < 0)
	{
		LogError("Couldn't open %s\n", m_devicePath.c_str());
		return;
	}
	ibclr(m_handle);
}

SCPILinuxGPIBTransport::~SCPILinuxGPIBTransport()
{
	if (IsConnected())
		ibonl(m_handle, 0);

}

bool SCPILinuxGPIBTransport::IsConnected()
{
	return (m_handle >= 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPILinuxGPIBTransport::GetTransportName()
{
	return "gpib";
}

string SCPILinuxGPIBTransport::GetConnectionString()
{
	return m_devicePath;
}

void SCPILinuxGPIBTransport::FlushRXBuffer()
{
	if (!IsConnected())
		return;

	unsigned char buf[1024];
	ibclr(m_handle);
	while (ReadRawData(1024, buf) != 0) {}
}

bool SCPILinuxGPIBTransport::SendCommand(const string& cmd)
{
	if (!IsConnected())
		return false;

	LogTrace("Sending %s\n", cmd.c_str());
	string tempbuf = cmd + "\n";
	ibwrt(m_handle, tempbuf.c_str(), tempbuf.length());
	return (ibcnt == (int)tempbuf.length());
}

string SCPILinuxGPIBTransport::ReadReply(bool endOnSemicolon)
{
	string ret;
	if (!IsConnected())
		return ret;

	char buf[1024];
	while(true)
	{
		ibrd(m_handle, buf, 1024);
		ret.append(buf, ibcnt);
		if (ret.back() == '\n' || (endOnSemicolon && (ret.back() == ';'))) {
			ret.pop_back();
			break;
		}
	}
	LogTrace("Got %s\n", ret.c_str());
	return ret;
}

void SCPILinuxGPIBTransport::SendRawData(size_t len, const unsigned char* buf)
{
	if (!IsConnected())
		return;

	ibwrt(m_handle, (const char *)buf, len);
}

size_t SCPILinuxGPIBTransport::ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> /*progress*/)
{
	if (!IsConnected())
		return 0;

	ibrd(m_handle, buf, len);
	return ibcnt;
}

bool SCPILinuxGPIBTransport::IsCommandBatchingSupported()
{
	return false;
}

#endif