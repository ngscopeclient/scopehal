/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "CopperMountainVNA.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CopperMountainVNA::CopperMountainVNA(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_rbw(1)
{
	//For now, assume we're a 2-port VNA only

	//Add analog channel objects
	for(size_t dest = 0; dest<2; dest ++)
	{
		for(size_t src=0; src<2; src++)
		{
			//Hardware name of the channel
			string chname = "S" + to_string(dest+1) + to_string(src+1);

			//Create the channel
			auto ichan = m_channels.size();
			auto chan = new SParameterChannel(
				this,
				chname,
				GetChannelColor(ichan),
				ichan);
			m_channels.push_back(chan);
			chan->SetDefaultDisplayName();
			chan->SetXAxisUnits(Unit::UNIT_HZ);

			//Set initial configuration so we have a well-defined instrument state
			SetChannelVoltageRange(ichan, 0, 80);
			SetChannelOffset(ichan, 0, 40);
			SetChannelVoltageRange(ichan, 1, 360);
			SetChannelOffset(ichan, 1, 0);
		}
	}

	//TODO: FORMat:DATA binary??
	//this isn't supported over sockets??
}

CopperMountainVNA::~CopperMountainVNA()
{
}

/**
	@brief Color the channels (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string CopperMountainVNA::GetChannelColor(size_t i)
{
	switch(i % 8)
	{
		case 0:
			return "#4040ff";

		case 1:
			return "#ff4040";

		case 2:
			return "#208020";

		case 3:
			return "#ffff00";

		case 4:
			return "#600080";

		case 5:
			return "#808080";

		case 6:
			return "#40a0a0";

		case 7:
		default:
			return "#e040e0";
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device enumeration

string CopperMountainVNA::GetDriverNameInternal()
{
	return "coppermt";
}

unsigned int CopperMountainVNA::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t CopperMountainVNA::GetInstrumentTypesForChannel(size_t /*i*/)
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Driver logic

bool CopperMountainVNA::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void CopperMountainVNA::EnableChannel(size_t /*i*/)
{
	//no-op
}

void CopperMountainVNA::DisableChannel(size_t /*i*/)
{
	//no-op
}

OscilloscopeChannel::CouplingType CopperMountainVNA::GetChannelCoupling(size_t /*i*/)
{
	//all inputs are ac coupled 50 ohm impedance
	return OscilloscopeChannel::COUPLE_AC_50;
}

void CopperMountainVNA::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> CopperMountainVNA::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

double CopperMountainVNA::GetChannelAttenuation(size_t /*i*/)
{
	return 1;
}

void CopperMountainVNA::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op
}

unsigned int CopperMountainVNA::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void CopperMountainVNA::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op
}

float CopperMountainVNA::GetChannelVoltageRange(size_t i, size_t stream)
{
	//range in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void CopperMountainVNA::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	//Range is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)]= range;
}

float CopperMountainVNA::GetChannelOffset(size_t i, size_t stream)
{
	//offset in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void CopperMountainVNA::SetChannelOffset(size_t i, size_t stream, float offset)
{
	//Offset is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}

//TODO: support ext trig if any
OscilloscopeChannel* CopperMountainVNA::GetExternalTrigger()
{
	return nullptr;
}

Oscilloscope::TriggerMode CopperMountainVNA::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

void CopperMountainVNA::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//m_transport->SendCommand("INIT:ALL");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void CopperMountainVNA::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//m_transport->SendCommand("INIT:ALL");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void CopperMountainVNA::Stop()
{
	//TODO: send something other than *RST
	//For now: just wrap up after the current acquisition ends
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

void CopperMountainVNA::ForceTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//m_transport->SendCommand("INIT:ALL");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool CopperMountainVNA::IsTriggerArmed()
{
	//return m_triggerArmed;
	return true;
}

void CopperMountainVNA::PushTrigger()
{
}

void CopperMountainVNA::PullTrigger()
{
}

bool CopperMountainVNA::AcquireData()
{
	return true;
}

vector<uint64_t> CopperMountainVNA::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(1);
	return ret;
}

vector<uint64_t> CopperMountainVNA::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> CopperMountainVNA::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> CopperMountainVNA::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(10001);
	return ret;
}

vector<uint64_t> CopperMountainVNA::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

uint64_t CopperMountainVNA::GetSampleRate()
{
	return 1;
}

uint64_t CopperMountainVNA::GetSampleDepth()
{
	return 10001;
}

void CopperMountainVNA::SetSampleDepth(uint64_t /*depth*/)
{
}

void CopperMountainVNA::SetSampleRate(uint64_t /*rate*/)
{
}

void CopperMountainVNA::SetTriggerOffset(int64_t /*offset*/)
{
}

int64_t CopperMountainVNA::GetTriggerOffset()
{
	return 0;
}

bool CopperMountainVNA::IsInterleaving()
{
	return false;
}

bool CopperMountainVNA::SetInterleaving(bool /*combine*/)
{
	return false;
}

int64_t CopperMountainVNA::GetResolutionBandwidth()
{
	return m_rbw;
}

bool CopperMountainVNA::HasFrequencyControls()
{
	return true;
}

bool CopperMountainVNA::HasTimebaseControls()
{
	return false;
}
