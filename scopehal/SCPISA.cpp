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
#include "SCPISA.h"

using namespace std;

//SCPISA::VNACreateMapType SCPISA::m_vnacreateprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPISA::SCPISA()
{
}

SCPISA::~SCPISA()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration
/*
void SCPISA::DoAddDriverClass(string name, VNACreateProcType proc)
{
	m_vnacreateprocs[name] = proc;
}

void SCPISA::EnumDrivers(vector<string>& names)
{
	for(auto it=m_vnacreateprocs.begin(); it != m_vnacreateprocs.end(); ++it)
		names.push_back(it->first);
}

shared_ptr<SCPISA> SCPISA::CreateVNA(string driver, SCPITransport* transport)
{
	if(m_vnacreateprocs.find(driver) != m_vnacreateprocs.end())
		return m_vnacreateprocs[driver](transport);

	LogError("Invalid VNA driver name \"%s\"\n", driver.c_str());
	return nullptr;
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default stubs for Oscilloscope methods

bool SCPISA::IsChannelEnabled([[maybe_unused]]size_t i)
{
	return true;
}

void SCPISA::EnableChannel([[maybe_unused]]size_t i)
{
	//no-op
}

void SCPISA::DisableChannel([[maybe_unused]]size_t i)
{
	//no-op
}

OscilloscopeChannel::CouplingType SCPISA::GetChannelCoupling([[maybe_unused]]size_t i)
{
	//all inputs are ac coupled 50 ohm impedance
	return OscilloscopeChannel::COUPLE_AC_50;
}

void SCPISA::SetChannelCoupling([[maybe_unused]]size_t i,[[maybe_unused]] OscilloscopeChannel::CouplingType type)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> SCPISA::GetAvailableCouplings([[maybe_unused]]size_t i)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

double SCPISA::GetChannelAttenuation([[maybe_unused]] size_t i)
{
	return 1;
}

void SCPISA::SetChannelAttenuation([[maybe_unused]] size_t i,[[maybe_unused]] double atten)
{
	//no-op
}

unsigned int SCPISA::GetChannelBandwidthLimit([[maybe_unused]] size_t i)
{
	return 0;
}

void SCPISA::SetChannelBandwidthLimit([[maybe_unused]] size_t i,[[maybe_unused]] unsigned int limit_mhz)
{
	//no-op
}

bool SCPISA::IsInterleaving()
{
	return false;
}

bool SCPISA::SetInterleaving([[maybe_unused]] bool combine)
{
	return false;
}


bool SCPISA::HasFrequencyControls()
{
	return true;
}

bool SCPISA::HasTimebaseControls()
{
	return false;
}

void SCPISA::SetTriggerOffset([[maybe_unused]] int64_t offset)
{
}

int64_t SCPISA::GetTriggerOffset()
{
	return 0;
}

vector<uint64_t> SCPISA::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SCPISA::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> SCPISA::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> SCPISA::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(1);
	return ret;
}

void SCPISA::SetSampleRate([[maybe_unused]] uint64_t rate)
{
}

uint64_t SCPISA::GetSampleRate()
{
	return 1;
}

unsigned int SCPISA::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t SCPISA::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

float SCPISA::GetChannelVoltageRange(size_t i, size_t stream)
{
	//range in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void SCPISA::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	//Range is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)]= range;
}

float SCPISA::GetChannelOffset(size_t i, size_t stream)
{
	//offset in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void SCPISA::SetChannelOffset(size_t i, size_t stream, float offset)
{
	//Offset is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}
