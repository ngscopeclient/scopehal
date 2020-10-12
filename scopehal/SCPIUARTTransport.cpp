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
	@author Alyssa Milburn
	@brief Implementation of SCPIUARTTransport
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIUARTTransport::SCPIUARTTransport(const string& args)
{
	char devfile[128];
	unsigned int baudrate = 0;
	if(2 != sscanf(args.c_str(), "%127[^:]:%u", devfile, &baudrate))
	{
		//default if port not specified
		m_devfile = args;
		m_baudrate = 115200;
	}
	else
	{
		m_devfile = devfile;
		m_baudrate = baudrate;
	}

	LogDebug("Connecting to SCPI oscilloscope at %s:%d\n", m_devfile.c_str(), m_baudrate);

	if(!m_uart.Connect(m_devfile, m_baudrate))
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
	snprintf(tmp, sizeof(tmp), "%s:%u", m_devfile.c_str(), m_baudrate);
	return string(tmp);
}

bool SCPIUARTTransport::SendCommand(const string& cmd)
{
	LogTrace("Sending %s\n", cmd.c_str());
	string tempbuf = cmd + "\n";
	return m_uart.Write((unsigned char*)tempbuf.c_str(), tempbuf.length());
}

string SCPIUARTTransport::ReadReply(bool endOnSemicolon)
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
}

void SCPIUARTTransport::ReadRawData(size_t len, unsigned char* buf)
{
	m_uart.Read(buf, len);
}

bool SCPIUARTTransport::IsCommandBatchingSupported()
{
	return true;
}
