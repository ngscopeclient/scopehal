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
	@brief Implementation of SCPITMCTransport
	@ingroup transports
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPITMCTransport::SCPITMCTransport(const string& args)
	: m_devicePath(args)
	, m_buf(nullptr)
	, m_max_read_size(2032)
{
	// <path>[:<max_read_size>]

	char *dev = strdup(m_devicePath.c_str());

	char *colon = strchr(dev, ':');
	if (colon) {
		m_max_read_size = atoi(colon + 1);
		*colon = '\0';
	}

	LogDebug("Connecting to SCPI oscilloscope over USBTMC through %s, max read size %d\n", dev, (int)m_max_read_size);

	m_handle = open(dev, O_RDWR);
	if (m_handle <= 0)
	{
		LogError("Couldn't open %s\n", dev);
		return;
	}

	free(dev);

	m_buf = new unsigned char[m_max_read_size+1];
}

SCPITMCTransport::~SCPITMCTransport()
{
	if (IsConnected())
		close(m_handle);

	delete[] m_buf;
}

bool SCPITMCTransport::IsConnected()
{
	return (m_handle > 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPITMCTransport::GetTransportName()
{
	return "usbtmc";
}

string SCPITMCTransport::GetConnectionString()
{
	return m_devicePath;
}

bool SCPITMCTransport::SendCommand(const string& cmd)
{
	if (!IsConnected())
		return false;

	LogTrace("Sending %s\n", cmd.c_str());
	int result = write(m_handle, cmd.c_str(), cmd.length());
	return (result == (int)cmd.length());
}

string SCPITMCTransport::ReadReply(bool endOnSemicolon, [[maybe_unused]] function<void(float)> progress)
{
	string ret;

	if (!m_buf || !IsConnected())
		return ret;

	bool done = false;
	while (!done) {
		int r = read(m_handle, (char *)m_buf, m_max_read_size);
		if (r <= 0) {
			LogError("Read error\n");
			break;
		}
		m_buf[r] = '\0';
		for (int i=0; i<r; i++) {
			char c = m_buf[i];
			if (c == '\n' || c == '\r' || (c == ';' && endOnSemicolon)) {
				done = true;
				break;
			} else {
				ret += c;
			}
		}
	}

	LogTrace("Got %s\n", ret.c_str());
	return ret;
}

void SCPITMCTransport::SendRawData(size_t len, const unsigned char* buf)
{
	// XXX: Should this reset m_data_depleted just like SendCommmand?
	write(m_handle, (const char *)buf, len);
}

size_t SCPITMCTransport::ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> /*progress*/)
{
	size_t done = 0;
	while (done < len) {
		size_t todo = len - done;
		if (todo > m_max_read_size)
			todo = m_max_read_size;
		int r = read(m_handle, buf + done, todo);
		if (r <= 0) {
			LogError("Read error\n");
			break;
		}
		done += r;
	}

	return len;
}

bool SCPITMCTransport::IsCommandBatchingSupported()
{
	return false;
}

void SCPITMCTransport::FlushRXBuffer(void)
{
	// FIXME: Can we flush USBTMC
	LogDebug("SCPITMCTransport::FlushRXBuffer is unimplemented\n");
}
