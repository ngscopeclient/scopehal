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
	@brief Implementation of AntikernelLabsVIO
	@ingroup miscdrivers
 */

#include "scopehal.h"
#include "AntikernelLabsVIO.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport pointing at the instrument
 */
AntikernelLabsVIO::AntikernelLabsVIO(SCPITransport* transport)
	: SCPIDevice(transport, true)
	, SCPIInstrument(transport, true)
	, m_inputChannelCount(0)
	, m_outputChannelCount(0)
{
	//Find inputs
	for(size_t i=0; i<8; i++)
	{
		string hwname = string("IN") + to_string(i);
		auto name = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":NAME?"));
		auto width = stoi(Trim(m_transport->SendCommandQueuedWithReply(hwname + ":WIDTH?")));
		if(width > 0)
		{
			auto chan = new VIOInputChannel(hwname, this, "#808080", m_channels.size(), width);
			chan->SetDisplayName(name);
			m_channels.push_back(chan);
		}
	}
	m_inputChannelCount = m_channels.size();

	//Find outputs
	for(size_t i=0; i<8; i++)
	{
		string hwname = string("OUT") + to_string(i);
		auto name = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":NAME?"));
		auto width = stoi(Trim(m_transport->SendCommandQueuedWithReply(hwname + ":WIDTH?")));
		if(width > 0)
		{
			auto chan = new VIOOutputChannel(hwname, this, "#808080", m_channels.size(), width);
			chan->SetDisplayName(name);
			m_channels.push_back(chan);
		}
	}
	m_outputChannelCount = m_channels.size() - m_inputChannelCount;
}

AntikernelLabsVIO::~AntikernelLabsVIO()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instantiation

uint32_t AntikernelLabsVIO::GetInstrumentTypes() const
{
	return INST_MISC;
}

uint32_t AntikernelLabsVIO::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return INST_MISC;
}

///@brief Returns the constant driver name
string AntikernelLabsVIO::GetDriverNameInternal()
{
	return "akl.vio";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acquisition

bool AntikernelLabsVIO::AcquireData()
{
	m_transport->SendCommandQueued("TRIG");

	bool refreshedAny = false;

	//Pull input values
	for(size_t i=0; i<m_inputChannelCount; i++)
	{
		auto chan = dynamic_cast<VIOInputChannel*>(m_channels[i]);
		if(!chan)
			continue;

		refreshedAny = true;

		auto inval = Trim(m_transport->SendCommandQueuedWithReply(chan->GetHwname() + ":VALUE?"));
		uint64_t hexinval = 0;
		sscanf(inval.c_str(), "%" SCNx64, &hexinval);

		chan->SetDigitalScalarValue(0, hexinval);
	}

	bool cacheWasEmpty = m_outputChannelCachedValues.empty();
	m_outputChannelCachedValues.resize(m_outputChannelCount);

	for(size_t i=0; i<m_outputChannelCount; i++)
	{
		auto chan = dynamic_cast<VIOOutputChannel*>(m_channels[i + m_inputChannelCount]);
		if(!chan)
			continue;

		auto outval = chan->GetInput(0).GetDigitalScalarValue();

		if( cacheWasEmpty || (m_outputChannelCachedValues[i] != outval) )
		{
			m_transport->SendCommandQueued(chan->GetHwname() + ":VALUE " + to_string_hex(outval));
			refreshedAny = true;
		}

		m_outputChannelCachedValues[i] = outval;
	}

	//If we didn't refresh anything, send an IDN and wait for the reply (but ignore it)
	//just to avoid the socket buffer getting full of TRIG
	if(!refreshedAny)
		m_transport->SendCommandQueuedWithReply("*IDN?");

	//Rate limit to ~100 Hz
	this_thread::sleep_for(chrono::milliseconds(10));

	return true;
}
