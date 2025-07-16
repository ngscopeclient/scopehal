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
	@author Alyssa Milburn
	@brief Implementation of SCPIUARTTransport
	@ingroup transports
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIUARTTransport::SCPIUARTTransport(const string& args)
{
	char devfile[128];
	char flags[128];
	unsigned int baudrate = 0;
	m_dtrEnable = false;
	if(3 == sscanf(args.c_str(), "%127[^:]:%u:%127s", devfile, &baudrate, flags))
	{
		m_devfile = devfile;
		m_baudrate = baudrate;
		m_dtrEnable = (strcmp(flags,"DTR") == 0);
	}
	else if(2 == sscanf(args.c_str(), "%127[^:]:%u", devfile, &baudrate))
	{
		m_devfile = devfile;
		m_baudrate = baudrate;
	}
	else
	{
		//default if port not specified
		m_devfile = args;
		m_baudrate = 115200;
	}

	LogDebug("Connecting to SCPI oscilloscope at %s:%d with dtrEnable=%s\n", m_devfile.c_str(), m_baudrate, m_dtrEnable ? "true" : "false");

	if(!m_uart.Connect(m_devfile, m_baudrate, m_dtrEnable))
	{
		m_uart.Close();
		LogError("Couldn't connect to UART\n");
		return;
	}
}

SCPIUARTTransport::~SCPIUARTTransport()
{
}

bool SCPIUARTTransport::IsConnected()
{
	return m_uart.IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPIUARTTransport::GetTransportName()
{
	return "uart";
}

string SCPIUARTTransport::GetConnectionString()
{
	char tmp[256];
	if(m_dtrEnable)
		snprintf(tmp, sizeof(tmp), "%s:%u:DTR", m_devfile.c_str(), m_baudrate);
	else
		snprintf(tmp, sizeof(tmp), "%s:%u", m_devfile.c_str(), m_baudrate);
	return string(tmp);
}

bool SCPIUARTTransport::SendCommand(const string& cmd)
{
	LogTrace("Sending %s\n", cmd.c_str());
	string tempbuf = cmd + "\n";
	return m_uart.Write((unsigned char*)tempbuf.c_str(), tempbuf.length());
}

string SCPIUARTTransport::ReadReply(bool endOnSemicolon, [[maybe_unused]] function<void(float)> progress)
{
	//FIXME: there *has* to be a more efficient way to do this...
	// (see the same code in Socket)
	char tmp = ' ';
	string ret;
	while(true)
	{
		if(!m_uart.Read((unsigned char*)&tmp, 1))
			break;
		if( (tmp == '\n') || ( (tmp == ';') && endOnSemicolon ) )
			break;
		else
			ret += tmp;
	}
	LogTrace("Got %s\n", ret.c_str());
	return ret;
}

void SCPIUARTTransport::SendRawData(size_t len, const unsigned char* buf)
{
	m_uart.Write(buf, len);
	//LogTrace("Sent %zu bytes: %s\n", len,LogHexDump(buf,len).c_str());
	LogTrace("Sent %zu bytes.\n", len);
}

size_t SCPIUARTTransport::ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> progress)
{
	size_t chunk_size = len;
	if (progress && len > 1)
	{
		/* carve up the chunk_size into 1% of data block */
		chunk_size /= 100;
		if (chunk_size < 2)
			chunk_size = 2;	// Always read at least 2 bytes at once since one single byte can block on Windows system
	}

	for (size_t pos = 0; pos < len; )
	{
		size_t n = chunk_size;
		if (n > (len - pos))
			n = len - pos;
		if(!m_uart.Read(buf + pos, n))
		{
			LogTrace("Failed to get %zu bytes out of %zu (@ pos %zu)\n", n, len, pos);
			return pos;
		}
		pos += n;
		if (progress)
		{
			progress((float)pos / (float)len);
		}
	}
	//LogTrace("Got %zu bytes: %s\n", len, LogHexDump(buf,len).c_str());
	LogTrace("Got %zu bytes.\n", len);
	return len;
}

bool SCPIUARTTransport::IsCommandBatchingSupported()
{
	return false;
}

void SCPIUARTTransport::FlushRXBuffer()
{
	if (!IsConnected())
		return;
	unsigned char buf[1024];
	//ibclr(m_handle);
	while (ReadRawData(1024, buf) != 0) {}
	//do nothing
	return;
}