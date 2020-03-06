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

#include "scopehal.h"
#include "LeCroyVICPOscilloscope.h"
#include "LeCroyVICPOscilloscope.h"
#include "ProtocolDecoder.h"
#include "base64.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyVICPOscilloscope::LeCroyVICPOscilloscope(string hostname, unsigned short port)
	: LeCroyOscilloscope(hostname, port)
	, m_nextSequence(1)
	, m_lastSequence(1)
{
	LogDebug("Connecting to VICP oscilloscope at %s:%d\n", hostname.c_str(), port);

	if(!m_socket.Connect(hostname, port))
	{
		LogError("Couldn't connect to socket\n");
		return;
	}
	if(!m_socket.DisableNagle())
	{
		LogError("Couldn't disable Nagle\n");
		return;
	}

	//standard initialization
	FlushConfigCache();
	IdentifyHardware();
	DetectAnalogChannels();
	SharedCtorInit();
	DetectOptions();

	//TODO: make this switchable
	SendCommand("DISP ON");
}

LeCroyVICPOscilloscope::~LeCroyVICPOscilloscope()
{
	//SendCommand("DISP ON");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VICP protocol logic

uint8_t LeCroyVICPOscilloscope::GetNextSequenceNumber(bool eoi)
{
	m_lastSequence = m_nextSequence;

	//EOI increments the sequence number.
	//Wrap mod 256, but skip zero!
	if(eoi)
	{
		m_nextSequence ++;
		if(m_nextSequence == 0)
			m_nextSequence = 1;
	}

	return m_lastSequence;
}

bool LeCroyVICPOscilloscope::SendCommand(string cmd, bool eoi)
{
	//Operation and flags header
	string payload;
	uint8_t op 	= OP_DATA;
	if(eoi)
		op |= OP_EOI;
	//TODO: remote, clear, poll flags
	payload += op;
	payload += 0x01;							//protocol version number
	payload += GetNextSequenceNumber(eoi);
	payload += '\0';							//reserved

	//Next 4 header bytes are the message length (network byte order)
	uint32_t len = cmd.length();
	payload += (len >> 24) & 0xff;
	payload += (len >> 16) & 0xff;
	payload += (len >> 8)  & 0xff;
	payload += (len >> 0)  & 0xff;

	//Add message data
	payload += cmd;

	//Actually send it
	if(!m_socket.SendLooped((const unsigned char*)payload.c_str(), payload.size()))
		return false;

	return true;
}

/**
	@brief Read exactly one packet from the socket
 */
string LeCroyVICPOscilloscope::ReadData()
{
	//Read the header
	unsigned char header[8];
	if(!m_socket.RecvLooped(header, 8))
		return "";

	//Sanity check
	if(header[1] != 1)
	{
		LogError("Bad VICP protocol version\n");
		return "";
	}
	if(header[2] != m_lastSequence)
	{
		//LogError("Bad VICP sequence number %d (expected %d)\n", header[2], m_lastSequence);
		//return "";
	}
	if(header[3] != 0)
	{
		LogError("Bad VICP reserved field\n");
		return "";
	}

	//TODO: pay attention to header?

	//Read the message data
	uint32_t len = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];
	string ret;
	ret.resize(len);
	if(!m_socket.RecvLooped((unsigned char*)&ret[0], len))
		return "";

	return ret;
}

string LeCroyVICPOscilloscope::ReadSingleBlockString(bool trimNewline)
{
	while(true)
	{
		string payload = ReadData();

		//Skip empty blocks
		if(payload.empty() || payload == "\n")
			continue;

		if(trimNewline && (payload.length() > 0) )
		{
			int iend = payload.length() - 1;
			if(trimNewline && (payload[iend] == '\n'))
				payload.resize(iend);
		}

		payload += "\0";
		return payload;
	}
}

string LeCroyVICPOscilloscope::ReadMultiBlockString()
{
	//Read until we get the closing quote
	string data;
	bool first  = true;
	while(true)
	{
		string payload = ReadSingleBlockString();
		data += payload;
		if(!first && payload.find("\"") != string::npos)
			break;
		first = false;
	}
	return data;
}
