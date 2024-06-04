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

#include "scopehal.h"
#include "SCPIVNA.h"

using namespace std;

SCPIVNA::VNACreateMapType SCPIVNA::m_vnacreateprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIVNA::SCPIVNA()
{
}

SCPIVNA::~SCPIVNA()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void SCPIVNA::DoAddDriverClass(string name, VNACreateProcType proc)
{
	m_vnacreateprocs[name] = proc;
}

void SCPIVNA::EnumDrivers(vector<string>& names)
{
	for(auto it=m_vnacreateprocs.begin(); it != m_vnacreateprocs.end(); ++it)
		names.push_back(it->first);
}

shared_ptr<SCPIVNA> SCPIVNA::CreateVNA(string driver, SCPITransport* transport)
{
	if(m_vnacreateprocs.find(driver) != m_vnacreateprocs.end())
		return m_vnacreateprocs[driver](transport);

	LogError("Invalid VNA driver name \"%s\"\n", driver.c_str());
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default stubs for Oscilloscope methods

bool SCPIVNA::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void SCPIVNA::EnableChannel(size_t /*i*/)
{
	//no-op
}

void SCPIVNA::DisableChannel(size_t /*i*/)
{
	//no-op
}

OscilloscopeChannel::CouplingType SCPIVNA::GetChannelCoupling(size_t /*i*/)
{
	//all inputs are ac coupled 50 ohm impedance
	return OscilloscopeChannel::COUPLE_AC_50;
}

void SCPIVNA::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> SCPIVNA::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

double SCPIVNA::GetChannelAttenuation(size_t /*i*/)
{
	return 1;
}

void SCPIVNA::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op
}

unsigned int SCPIVNA::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void SCPIVNA::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op
}

bool SCPIVNA::IsInterleaving()
{
	return false;
}

bool SCPIVNA::SetInterleaving(bool /*combine*/)
{
	return false;
}


bool SCPIVNA::HasFrequencyControls()
{
	return true;
}

bool SCPIVNA::HasTimebaseControls()
{
	return false;
}

void SCPIVNA::SetTriggerOffset(int64_t /*offset*/)
{
}

int64_t SCPIVNA::GetTriggerOffset()
{
	return 0;
}

vector<uint64_t> SCPIVNA::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SCPIVNA::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> SCPIVNA::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> SCPIVNA::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(1);
	return ret;
}

void SCPIVNA::SetSampleRate(uint64_t /*rate*/)
{
}

uint64_t SCPIVNA::GetSampleRate()
{
	return 1;
}

unsigned int SCPIVNA::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t SCPIVNA::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

float SCPIVNA::GetChannelVoltageRange(size_t i, size_t stream)
{
	//range in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void SCPIVNA::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	//Range is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)]= range;
}

float SCPIVNA::GetChannelOffset(size_t i, size_t stream)
{
	//offset in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void SCPIVNA::SetChannelOffset(size_t i, size_t stream, float offset)
{
	//Offset is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}
