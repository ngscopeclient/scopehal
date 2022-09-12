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

#include "scopehal.h"
#include "RemoteBridgeOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RemoteBridgeOscilloscope::RemoteBridgeOscilloscope(SCPITransport* transport, bool identify)
	: SCPIDevice(transport, identify)
	, SCPIOscilloscope()
	, m_triggerArmed(false)
{

}

RemoteBridgeOscilloscope::~RemoteBridgeOscilloscope()
{

}

void RemoteBridgeOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("START");

	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RemoteBridgeOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SINGLE");

	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RemoteBridgeOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");

	m_triggerArmed = false;
}

void RemoteBridgeOscilloscope::ForceTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("FORCE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RemoteBridgeOscilloscope::PullTrigger()
{
	//pulling not needed, we always have a valid trigger cached
}

void RemoteBridgeOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Don't allow setting trigger offset beyond the end of the capture
	int64_t captureDuration = GetSampleDepth() * FS_PER_SECOND / GetSampleRate();
	m_triggerOffset = min(offset, captureDuration);

	PushTrigger();
}

int64_t RemoteBridgeOscilloscope::GetTriggerOffset()
{
	return m_triggerOffset;
}

uint64_t RemoteBridgeOscilloscope::GetSampleRate()
{
	return m_srate;
}

uint64_t RemoteBridgeOscilloscope::GetSampleDepth()
{
	return m_mdepth;
}

void RemoteBridgeOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(string("DEPTH ") + to_string(depth));
	m_mdepth = depth;
}

void RemoteBridgeOscilloscope::SetSampleRate(uint64_t rate)
{
	m_srate = rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand( string("RATE ") + to_string(rate));
}


void RemoteBridgeOscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");

	ClearPendingWaveforms();
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void RemoteBridgeOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Type
	//m_transport->SendCommand(":TRIG:MODE EDGE");

	//Delay
	m_transport->SendCommand("TRIG:DELAY " + to_string(m_triggerOffset));

	//Source
	auto chan = trig->GetInput(0).m_channel;
	m_transport->SendCommand("TRIG:SOU " + chan->GetHwname());

	//Level
	char buf[128];
	snprintf(buf, sizeof(buf), "TRIG:LEV %f", trig->GetLevel() / chan->GetAttenuation());
	m_transport->SendCommand(buf);

	//Slope
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommand("TRIG:EDGE:DIR RISING");
			break;
		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommand("TRIG:EDGE:DIR FALLING");
			break;
		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommand("TRIG:EDGE:DIR ANY");
			break;
		default:
			LogWarning("Unknown edge type\n");
			return;
	}
}

bool RemoteBridgeOscilloscope::PeekTriggerArmed()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ARMED?");
	auto reply = m_transport->ReadReply();
	if(stoi(reply) == 1)
		return true;
	return false;
}

bool RemoteBridgeOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

//

bool RemoteBridgeOscilloscope::IsChannelEnabled(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelsEnabled[i];
}

void RemoteBridgeOscilloscope::EnableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = true;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":ON");
}

void RemoteBridgeOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":OFF");
}

OscilloscopeChannel::CouplingType RemoteBridgeOscilloscope::GetChannelCoupling(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelCouplings[i];
}

void RemoteBridgeOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	vector<OscilloscopeChannel::CouplingType> available = GetAvailableCouplings(i);

	if (!count(available.begin(), available.end(), type))
	{
		return;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":COUP AC1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":COUP DC1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":COUP DC50");
			break;

		default:
			LogError("Coupling not supported in RemoteBridgeOscilloscope: %d\n", type);
			return;
	}

	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelCouplings[i] = type;
	}
}

//

float RemoteBridgeOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRanges[i];
}

void RemoteBridgeOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	char buf[128];
	snprintf(buf, sizeof(buf), ":%s:RANGE %f", m_channels[i]->GetHwname().c_str(), range / GetChannelAttenuation(i));
	m_transport->SendCommand(buf);
}

float RemoteBridgeOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffsets[i];
}

void RemoteBridgeOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	char buf[128];
	snprintf(buf, sizeof(buf), ":%s:OFFS %f", m_channels[i]->GetHwname().c_str(), -offset / GetChannelAttenuation(i));
	m_transport->SendCommand(buf);
}
