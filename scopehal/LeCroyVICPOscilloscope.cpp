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
{
	m_transport = new VICPSocketTransport(hostname, port);

	//standard initialization
	FlushConfigCache();
	IdentifyHardware();
	DetectAnalogChannels();
	SharedCtorInit();
	DetectOptions();
}

LeCroyVICPOscilloscope::~LeCroyVICPOscilloscope()
{
	delete m_transport;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VICP protocol logic

bool LeCroyVICPOscilloscope::SendCommand(string cmd, bool eoi)
{
	m_transport->SendCommand(cmd);
	return true;
}

/**
	@brief Read exactly one packet from the socket
 */
string LeCroyVICPOscilloscope::ReadData()
{
	return m_transport->ReadReply();
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
