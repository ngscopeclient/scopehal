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
	@brief Implementation of SCPILxiTransport
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPILxiTransport::SCPILxiTransport(string args)
{
	char hostname[128];
	unsigned int port = 0;
	if(2 != sscanf(args.c_str(), "%127[^:]:%u", hostname, &port))
	{
		//default if port not specified
		m_hostname = args;
		m_port = 0;
	}
	else
	{
		m_hostname = hostname;
		m_port = port;
	}

	m_timeout = 1000;

	LogDebug("Connecting to SCPI oscilloscope over VXI-11 at %s:%d\n", m_hostname.c_str(), m_port);

	m_device = lxi_connect(m_hostname.c_str(), m_port, "inst0", m_timeout, VXI11);

	if (m_device == LXI_ERROR){
		LogError("Couldn't connect to VXI-11 device\n");
		return;
	}

	m_staging_buf_size = 200000000;
	m_staging_buf = (unsigned char *)malloc(m_staging_buf_size);
	assert(m_staging_buf != NULL);
	m_data_in_staging_buf = 0;
	m_data_offset = 0;
	m_data_depleted = false;
}

SCPILxiTransport::~SCPILxiTransport()
{
	free(m_staging_buf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPILxiTransport::GetTransportName()
{
	return "lxi";
}

string SCPILxiTransport::GetConnectionString()
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s:%u", m_hostname.c_str(), m_port);
	return string(tmp);
}

bool SCPILxiTransport::SendCommand(string cmd)
{
	LogTrace("Sending %s\n", cmd.c_str());

	int result = lxi_send(m_device, cmd.c_str(), cmd.length(), m_timeout); 

	m_data_in_staging_buf = 0;
	m_data_offset = 0;
	m_data_depleted = false;

	return (result != LXI_ERROR);
}

string SCPILxiTransport::ReadReply()
{
	//FIXME: there *has* to be a more efficient way to do this...
	char tmp = ' ';
	string ret;
	while(true){
		if (m_data_depleted)
			break;
		ReadRawData(1, (unsigned char *)&tmp);
		if( (tmp == '\n') || (tmp == ';') )
			break;
		else
			ret += tmp;
	}
	LogTrace("Got %s\n", ret.c_str());
	return ret;
}

void SCPILxiTransport::SendRawData(size_t len, const unsigned char* buf)
{
	lxi_send(m_device, (const char *)buf, len, m_timeout); 
}

void SCPILxiTransport::ReadRawData(size_t len, unsigned char* buf)
{
	if (!m_data_depleted){
		if (m_data_in_staging_buf == 0){
			m_data_in_staging_buf = lxi_receive(m_device, (char *)m_staging_buf, m_staging_buf_size, m_timeout);
			if (m_data_in_staging_buf == LXI_ERROR)
				m_data_in_staging_buf = 0;
			m_data_offset = 0;
		}

		unsigned int data_left = m_data_in_staging_buf - m_data_offset;
		if (data_left > 0){
			int nr_bytes = len > data_left ? data_left : len;

			memcpy(buf, m_staging_buf + m_data_offset, nr_bytes);

			m_data_offset += nr_bytes;
		}

		if (m_data_offset == m_data_in_staging_buf)
				m_data_depleted = true;
	}
	else{
		LogDebug("ReadRawData: data depleted.\n");
	}
}
