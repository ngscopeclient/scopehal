/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	@author Frederic BORRY
	@brief Implementation of SCPIHIDTransport
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIHIDTransport::SCPIHIDTransport(const string& args)
{
	//Figure out vendorId, productId and serialNumber
	unsigned int vendorId;
	unsigned int productId;
	char serialNumber[128] = "";
	bool hasSerial = false;
	if(3 == sscanf(args.c_str(), "%x:%x:%127s", &vendorId, &productId, serialNumber))
		hasSerial = true;
	else if(2 == sscanf(args.c_str(), "%x:%x", &vendorId, &productId))
	{}	// Noop
	else
	{
		LogError("Invallid HID connection string '%s', please use 0x<vendrId>:0x<productId>[:serialNumber]\n", args.c_str());
		return;		
	}


	LogDebug("Connecting to HID instrument at %04x:%04x:%s\n", vendorId, productId , serialNumber);

	if(!m_hid.Connect(vendorId, productId, hasSerial ? serialNumber : NULL))
	{
		m_hid.Close();
		LogError("Couldn't connect to HID\n");
		return;
	}
}

SCPIHIDTransport::~SCPIHIDTransport()
{
}

bool SCPIHIDTransport::IsConnected()
{
	return m_hid.IsValid();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPIHIDTransport::GetTransportName()
{
	return "hid";
}

string SCPIHIDTransport::GetConnectionString()
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s:%u", m_devfile.c_str(), m_baudrate);
	return string(tmp);
}

bool SCPIHIDTransport::SendCommand(const string& cmd)
{
	LogTrace("Sending %s\n", cmd.c_str());
	string tempbuf = cmd + "\n";
	return m_hid.Write((unsigned char*)tempbuf.c_str(), tempbuf.length());
}

string SCPIHIDTransport::ReadReply(bool endOnSemicolon, std::function<void(float)> /*progress*/)
{
	char tmp = ' ';
	string ret;
	while(true)
	{
		if(!m_hid.Read((unsigned char*)&tmp, 1))
			break;
		if( (tmp == '\n') || ( (tmp == ';') && endOnSemicolon ) )
			break;
		else
			ret += tmp;
	}
	LogTrace("Got %s\n", ret.c_str());
	return ret;
}

void SCPIHIDTransport::SendRawData(size_t len, const unsigned char* buf)
{
	m_hid.Write(buf, len);
	LogTrace("Sent %zu bytes: %s\n", len,LogHexDump(buf,len).c_str());
}

size_t SCPIHIDTransport::ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> /*progress*/)
{
	if(!m_hid.Read(buf, len))
		return 0;
	LogTrace("Got %zu bytes: %s\n", len, LogHexDump(buf,len).c_str());
	return len;
}

bool SCPIHIDTransport::IsCommandBatchingSupported()
{
	return true;
}
