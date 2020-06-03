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
	@brief Implementation of SCPITMCTransport
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

SCPITMCTransport::SCPITMCTransport(string args)
{
	// TODO: add configuration options:
	// - set the maximum request size of usbtmc read requests (currently 2032)
	// - set timeout value (when using kernel that has usbtmc v2 version)
	// - set size of staging buffer (or get rid of it entirely?)
	m_devicePath = args;

	// FIXME: currently not used
	m_timeout = 1000;

	LogDebug("Connecting to SCPI oscilloscope over USBTMC through %s\n", m_devicePath.c_str());

	m_handle = open(m_devicePath.c_str(), O_RDWR);
	if (m_handle <= 0)
	{
		LogError("Couldn't open %s\n", m_devicePath.c_str());
		return;
	}

	// Right now, I'm using an internal staging buffer because this code has been copied from the lxi transport driver.
	// It's not strictly needed...
	m_staging_buf_size = 150000000;
	m_staging_buf = new unsigned char[m_staging_buf_size];
	if (m_staging_buf == NULL)
		return;
	m_data_in_staging_buf = 0;
	m_data_offset = 0;
	m_data_depleted = false;
}

SCPITMCTransport::~SCPITMCTransport()
{
	if (IsConnected())
		close(m_handle);

	delete[] m_staging_buf;
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

bool SCPITMCTransport::SendCommand(string cmd)
{
	if (!IsConnected())
		return false;

	LogTrace("Sending %s\n", cmd.c_str());

	int result = write(m_handle, cmd.c_str(), cmd.length());

	m_data_in_staging_buf = 0;
	m_data_offset = 0;
	m_data_depleted = false;

	return (result == (int)cmd.length());
}

string SCPITMCTransport::ReadReply()
{
	string ret;

	if (!m_staging_buf || !IsConnected())
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

void SCPITMCTransport::SendRawData(size_t len, const unsigned char* buf)
{
	// XXX: Should this reset m_data_depleted just like SendCommmand?
	write(m_handle, (const char *)buf, len);
}

void SCPITMCTransport::ReadRawData(size_t len, unsigned char* buf)
{
	// Data in the staging buffer is assumed to always be a consequence of a SendCommand request.
	// Since we fetch all the reply data in one go, once all this data has been fetched, we mark
	// the staging buffer as depleted and don't issue a new read until a new SendCommand
	// is issued.

	if (!m_staging_buf || !IsConnected())
		return;

	if (!m_data_depleted)
	{
		if (m_data_in_staging_buf == 0)
		{

#if 0
			// This is what we'd use if we could be sure that the installed Linux kernel had
			// usbtmc driver v2.
			m_data_in_staging_buf = read(m_handle, (char *)m_staging_buf, m_staging_buf_size);
#else			
			// Split up one potentially large read into a bunch of smaller ones.
			// The performance impact of this is pretty small.
			const int max_bytes_per_req = 2032;
			int i = 0;
			int bytes_fetched, bytes_requested;

			do
			{
				bytes_requested = (max_bytes_per_req < len) ? max_bytes_per_req : len;
				bytes_fetched = read(m_handle, (char *)m_staging_buf + i, m_staging_buf_size);
				i += bytes_fetched;
			} while(bytes_fetched == bytes_requested);

			m_data_in_staging_buf = i;
#endif

			if (m_data_in_staging_buf <= 0)
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

bool SCPITMCTransport::IsCommandBatchingSupported()
{
	return false;
}
