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
	if (2 != sscanf(args.c_str(), "%127[^:]:%u", hostname, &port))
	{
		//default if port not specified. VXI-11 is port 111, but liblxi fills that in for us.
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

	m_device = lxi_connect((char *)(m_hostname.c_str()), m_port, (char *)("inst0"), m_timeout, VXI11);

	if (m_device == LXI_ERROR)
	{
		LogError("Couldn't connect to VXI-11 device\n");
		return;
	}

	// When you issue a lxi_receive request, you need to specify the size of the receiving buffer.
	// However, when the data received is larger than this buffer, liblxi simply discards this data.
	// ReadReply and ReadRawData expect to be able to fetch received data piecemeal.
	// The clunky solution is to have an intermediate buffer that is large enough to store all the
	// data that could possible be returned by a scope.
	// My Siglent oscilloscope can have a waveform of 140M samples, so I reserve 150M. However,
	// I haven't been able to fetch waveforms larger than 1.4M (for reasons unknown.)
	// Maybe we should reduce this number...
	m_staging_buf_size = 150000000;
	m_staging_buf = new unsigned char[m_staging_buf_size];
	if (m_staging_buf == NULL)
		return;
	m_data_in_staging_buf = 0;
	m_data_offset = 0;
	m_data_depleted = false;
}

SCPILxiTransport::~SCPILxiTransport()
{
	delete[] m_staging_buf;
}

bool SCPILxiTransport::IsConnected()
{
	return (m_device != LXI_ERROR);
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

	int result = lxi_send(m_device, (char *)(cmd.c_str()), cmd.length(), m_timeout);

	m_data_in_staging_buf = 0;
	m_data_offset = 0;
	m_data_depleted = false;

	return (result != LXI_ERROR);
}

string SCPILxiTransport::ReadReply()
{
	string ret;

	if (!m_staging_buf)
		return ret;

	//FIXME: there *has* to be a more efficient way to do this...
	char tmp = ' ';
	while(true)
	{
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
	// XXX: Should this reset m_data_depleted just like SendCommmand?
	lxi_send(m_device, (char *)buf, len, m_timeout);
}

void SCPILxiTransport::ReadRawData(size_t len, unsigned char* buf)
{
	// Data in the staging buffer is assumed to always be a consequence of a SendCommand request.
	// Since we fetch all the reply data in one go, once all this data has been fetched, we mark
	// the staging buffer as depleted and don't issue a new lxi_receive until a new SendCommand
	// is issued.

	if (!m_staging_buf)
		return;

	if (!m_data_depleted)
	{
		if (m_data_in_staging_buf == 0)
		{
			m_data_in_staging_buf = lxi_receive(m_device, (char *)m_staging_buf, m_staging_buf_size, m_timeout);
			if (m_data_in_staging_buf == LXI_ERROR)
				m_data_in_staging_buf = 0;
			m_data_offset = 0;
		}

		unsigned int data_left = m_data_in_staging_buf - m_data_offset;
		if (data_left > 0)
		{
			int nr_bytes = len > data_left ? data_left : len;

			memcpy(buf, m_staging_buf + m_data_offset, nr_bytes);

			m_data_offset += nr_bytes;
		}

		if (m_data_offset == m_data_in_staging_buf)
				m_data_depleted = true;
	}
	else
	{
		// When this happens, the SCPIDevice is fetching more data from device than what
		// could be expected from the SendCommand that was issued.
		LogDebug("ReadRawData: data depleted.\n");
	}
}

bool SCPILxiTransport::IsCommandBatchingSupported()
{
	return false;
}
