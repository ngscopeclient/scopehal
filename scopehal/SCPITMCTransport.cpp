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

SCPITMCTransport::SCPITMCTransport(const string& args)
	: m_devicePath(args)
	, m_staging_buf_size(0)
	, m_staging_buf(nullptr)
	, m_data_in_staging_buf(0)
	, m_data_offset(0)
	, m_data_depleted(false)
	, m_fix_buggy_driver(false)
	, m_transfer_size(48)
{
	// TODO: add configuration options:
	// - set the maximum request size of usbtmc read requests (currently 2032)
	// - set timeout value (when using kernel that has usbtmc v2 version)
	// - set size of staging buffer (or get rid of it entirely?)

	// FIXME: currently not used
	m_timeout = 1000;

	LogDebug("Connecting to SCPI oscilloscope over USBTMC through %s\n", m_devicePath.c_str());

	char devicePath[128] = "";
	char transferBufferSize[128] = "";
	char *ptr;
	if(2 == sscanf(m_devicePath.c_str(), "%127[^:]:%127s", devicePath, transferBufferSize))
	{
		// set size for a buggy tmc firmware
		// FIXME: Workaround for Siglent SDS1x04X-E. Max request size is 48 byte. Bug in firmware 8.2.6.1.37R9 ?
		m_fix_buggy_driver = true;
		m_transfer_size = strtol(transferBufferSize, &ptr, 10);
		if(m_transfer_size == 0)
		{
			LogNotice("USBTMC wrong size value %s\n", transferBufferSize);
			return;
		}
		LogNotice("Set USBTMC transfer size %d bytes. Workaround for buggy firmware.\n", m_transfer_size);
	}

	m_handle = open(devicePath, O_RDWR);
	if (m_handle <= 0)
	{
		LogError("Couldn't open %s\n", devicePath);
		return;
	}

	// Right now, I'm using an internal staging buffer because this code has been copied from the lxi transport driver.
	// It's not strictly needed...
	m_staging_buf_size = 150000000;
	m_staging_buf = new unsigned char[m_staging_buf_size];
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

bool SCPITMCTransport::SendCommand(const string& cmd)
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

string SCPITMCTransport::ReadReply(bool endOnSemicolon)
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
		if( (tmp == '\n') || ( (tmp == ';') && endOnSemicolon ) )
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

size_t SCPITMCTransport::ReadRawData(size_t len, unsigned char* buf)
{
	// Data in the staging buffer is assumed to always be a consequence of a SendCommand request.
	// Since we fetch all the reply data in one go, once all this data has been fetched, we mark
	// the staging buffer as depleted and don't issue a new read until a new SendCommand
	// is issued.

	if (!m_staging_buf || !IsConnected())
		return 0;

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
				if(m_fix_buggy_driver == false)
				{
				    bytes_requested = (max_bytes_per_req < len) ? max_bytes_per_req : len;
				    bytes_fetched = read(m_handle, (char *)m_staging_buf + i, m_staging_buf_size);
				}
				else
				{
					// limit each request to m_transfer_size
					bytes_requested = m_transfer_size;
				    bytes_fetched = read(m_handle, (char *)m_staging_buf + i, m_transfer_size);
				}
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
		return 0;
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
