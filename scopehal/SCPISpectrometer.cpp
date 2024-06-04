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
#include "SCPISpectrometer.h"

using namespace std;

SCPISpectrometer::SpectrometerCreateMapType SCPISpectrometer::m_spectrometercreateprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPISpectrometer::SCPISpectrometer()
{
	m_serializers.push_back(sigc::mem_fun(*this, &SCPISpectrometer::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &SCPISpectrometer::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &SCPISpectrometer::DoPreLoadConfiguration));
}

SCPISpectrometer::~SCPISpectrometer()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void SCPISpectrometer::DoAddDriverClass(string name, SpectrometerCreateProcType proc)
{
	m_spectrometercreateprocs[name] = proc;
}

void SCPISpectrometer::EnumDrivers(vector<string>& names)
{
	for(auto it=m_spectrometercreateprocs.begin(); it != m_spectrometercreateprocs.end(); ++it)
		names.push_back(it->first);
}

shared_ptr<SCPISpectrometer> SCPISpectrometer::CreateSpectrometer(string driver, SCPITransport* transport)
{
	if(m_spectrometercreateprocs.find(driver) != m_spectrometercreateprocs.end())
		return m_spectrometercreateprocs[driver](transport);

	LogError("Invalid spectrometer driver name \"%s\"\n", driver.c_str());
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default stubs for Oscilloscope methods

bool SCPISpectrometer::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void SCPISpectrometer::EnableChannel(size_t /*i*/)
{
	//no-op
}

void SCPISpectrometer::DisableChannel(size_t /*i*/)
{
	//no-op
}

OscilloscopeChannel::CouplingType SCPISpectrometer::GetChannelCoupling(size_t /*i*/)
{
	//not electrical inputs
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void SCPISpectrometer::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> SCPISpectrometer::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_SYNTHETIC);
	return ret;
}

double SCPISpectrometer::GetChannelAttenuation(size_t /*i*/)
{
	return 1;
}

void SCPISpectrometer::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op
}

unsigned int SCPISpectrometer::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void SCPISpectrometer::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op
}

bool SCPISpectrometer::IsInterleaving()
{
	return false;
}

bool SCPISpectrometer::SetInterleaving(bool /*combine*/)
{
	return false;
}


bool SCPISpectrometer::HasFrequencyControls()
{
	return false;
}

bool SCPISpectrometer::HasTimebaseControls()
{
	return false;
}

void SCPISpectrometer::SetTriggerOffset(int64_t /*offset*/)
{
}

int64_t SCPISpectrometer::GetTriggerOffset()
{
	return 0;
}

vector<uint64_t> SCPISpectrometer::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SCPISpectrometer::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> SCPISpectrometer::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> SCPISpectrometer::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(1);
	return ret;
}

void SCPISpectrometer::SetSampleRate(uint64_t /*rate*/)
{
}

uint64_t SCPISpectrometer::GetSampleRate()
{
	return 1;
}

unsigned int SCPISpectrometer::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t SCPISpectrometer::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

float SCPISpectrometer::GetChannelVoltageRange(size_t i, size_t stream)
{
	//range in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void SCPISpectrometer::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	//Range is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)]= range;
}

float SCPISpectrometer::GetChannelOffset(size_t i, size_t stream)
{
	//offset in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void SCPISpectrometer::SetChannelOffset(size_t i, size_t stream, float offset)
{
	//Offset is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void SCPISpectrometer::DoSerializeConfiguration(YAML::Node& node, IDTable& /*table*/)
{
	node["integration"] = GetIntegrationTime();
}

void SCPISpectrometer::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& /*idmap*/)
{
	if(node["integration"])
		SetIntegrationTime(node["integration"].as<int64_t>());
}

void SCPISpectrometer::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& /*node*/,
	IDTable& /*idmap*/,
	ConfigWarningList& /*list*/)
{
}

