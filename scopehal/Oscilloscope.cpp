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
	@brief Implementation of Oscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

#include "EdgeTrigger.h"

using namespace std;

Oscilloscope::CreateMapType Oscilloscope::m_createprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Oscilloscope::Oscilloscope()
{
	m_trigger = NULL;
}

Oscilloscope::~Oscilloscope()
{
	if(m_trigger)
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	for(size_t i=0; i<m_channels.size(); i++)
		delete m_channels[i];
	m_channels.clear();

	for(auto set : m_pendingWaveforms)
	{
		for(auto it : set)
			delete it.second;
	}
	m_pendingWaveforms.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void Oscilloscope::DoAddDriverClass(string name, CreateProcType proc)
{
	m_createprocs[name] = proc;
}

void Oscilloscope::EnumDrivers(vector<string>& names)
{
	for(CreateMapType::iterator it=m_createprocs.begin(); it != m_createprocs.end(); ++it)
		names.push_back(it->first);
}

Oscilloscope* Oscilloscope::CreateOscilloscope(string driver, SCPITransport* transport)
{
	if(m_createprocs.find(driver) != m_createprocs.end())
		return m_createprocs[driver](transport);

	LogError("Invalid driver name");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device properties

void Oscilloscope::FlushConfigCache()
{
	//nothing to do, base class has no caching
}

size_t Oscilloscope::GetChannelCount()
{
	return m_channels.size();
}

OscilloscopeChannel* Oscilloscope::GetChannel(size_t i)
{
	if(i < m_channels.size())
		return m_channels[i];
	else
		return NULL;
}

OscilloscopeChannel* Oscilloscope::GetChannelByDisplayName(string name)
{
	for(auto c : m_channels)
	{
		if(c->m_displayname == name)
			return c;
	}
	return NULL;
}

OscilloscopeChannel* Oscilloscope::GetChannelByHwName(string name)
{
	for(auto c : m_channels)
	{
		if(c->GetHwname() == name)
			return c;
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering helpers

bool Oscilloscope::WaitForTrigger(int timeout)
{
	for(int i=0; i<timeout*100; i++)
	{
		if(HasPendingWaveforms())
			return true;
		std::this_thread::sleep_for(std::chrono::microseconds(10 * 1000));
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sequenced capture

size_t Oscilloscope::GetPendingWaveformCount()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	return m_pendingWaveforms.size();
}

bool Oscilloscope::HasPendingWaveforms()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	return (m_pendingWaveforms.size() != 0);
}

/**
	@brief Discard any pending waveforms that haven't yet been processed
 */
void Oscilloscope::ClearPendingWaveforms()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	while(!m_pendingWaveforms.empty())
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			delete it.second;
		m_pendingWaveforms.pop_front();
	}
}

/**
	@brief Pops the queue of pending waveforms and updates each channel with a new waveform
 */
bool Oscilloscope::PopPendingWaveform()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	if(m_pendingWaveforms.size())
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			it.first->SetData(it.second, 0);	//assume stream 0
		m_pendingWaveforms.pop_front();
		return true;
	}
	return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string Oscilloscope::SerializeConfiguration(IDTable& table)
{
	//Save basic scope info
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "    : \n");
	string config = tmp;
	snprintf(tmp, sizeof(tmp), "        id:             %d\n", table.emplace(this));
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        nick:           \"%s\"\n", m_nickname.c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        name:           \"%s\"\n", GetName().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        vendor:         \"%s\"\n", GetVendor().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        serial:         \"%s\"\n", GetSerial().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        transport:      \"%s\"\n", GetTransportName().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        args:           \"%s\"\n", GetTransportConnectionString().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        driver:         \"%s\"\n", GetDriverName().c_str());
	config += tmp;

	//Save channels
	config += "        channels:\n";
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		auto chan = GetChannel(i);
		if(!chan->IsPhysicalChannel())
			continue;	//skip any kind of math functions etc

		//Basic channel info
		snprintf(tmp, sizeof(tmp), "            : \n");
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                id:          %d\n", table.emplace(chan));
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                index:       %zu\n", i);
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                color:       \"%s\"\n", chan->m_displaycolor.c_str());
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                nick:        \"%s\"\n", chan->m_displayname.c_str());
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                name:        \"%s\"\n", chan->GetHwname().c_str());
		config += tmp;
		switch(chan->GetType())
		{
			case OscilloscopeChannel::CHANNEL_TYPE_ANALOG:
				config += "                type:        analog\n";
				break;
			case OscilloscopeChannel::CHANNEL_TYPE_DIGITAL:
				config += "                type:        digital\n";
				snprintf(tmp, sizeof(tmp), "                width:       %d\n", chan->GetWidth());
				config += tmp;
				break;
			case OscilloscopeChannel::CHANNEL_TYPE_TRIGGER:
				config += "                type:        trigger\n";
				break;

			//should never get complex channels on a scope
			default:
				break;
		}

		//Current channel configuration
		if(chan->IsEnabled())
			config += "                enabled:     1\n";
		else
			config += "                enabled:     0\n";

		if(chan->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG)
		{
			snprintf(tmp, sizeof(tmp), "                attenuation: %f\n", chan->GetAttenuation());
			config += tmp;
			snprintf(tmp, sizeof(tmp), "                bwlimit:     %d\n", chan->GetBandwidthLimit());
			config += tmp;
			snprintf(tmp, sizeof(tmp), "                vrange:      %f\n", chan->GetVoltageRange());
			config += tmp;
			snprintf(tmp, sizeof(tmp), "                offset:      %f\n", chan->GetOffset());
			config += tmp;

			switch(chan->GetCoupling())
			{
				case OscilloscopeChannel::COUPLE_DC_1M:
					config += "                coupling:    dc_1M\n";
					break;
				case OscilloscopeChannel::COUPLE_AC_1M:
					config += "                coupling:    ac_1M\n";
					break;
				case OscilloscopeChannel::COUPLE_DC_50:
					config += "                coupling:    dc_50\n";
					break;
				case OscilloscopeChannel::COUPLE_GND:
					config += "                coupling:    gnd\n";
					break;

				//should never get synthetic coupling on a scope channel
				default:
					LogWarning("unsupported coupling value when saving\n");
					break;
			}
		}
	}

	//TODO: Serialize trigger and timebase configuration

	return config;
}

void Oscilloscope::LoadConfiguration(const YAML::Node& node, IDTable& table)
{
	m_nickname = node["nick"].as<string>();

	//Load the channels
	auto& chans = node["channels"];
	for(auto it : chans)
	{
		auto& cnode = it.second;
		auto chan = m_channels[cnode["index"].as<int>()];
		table.emplace(cnode["id"].as<int>(), chan);

		//Ignore name/type.
		//These are only needed for offline scopes to create a representation of the original instrument.

		chan->m_displaycolor = cnode["color"].as<string>();
		chan->m_displayname = cnode["nick"].as<string>();

		if(cnode["enabled"].as<int>())
			chan->Enable();
		else
			chan->Disable();

		//only load AFE config for analog inputs
		if(chan->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG)
		{
			chan->SetAttenuation(cnode["attenuation"].as<float>());
			chan->SetBandwidthLimit(cnode["bwlimit"].as<int>());
			chan->SetVoltageRange(cnode["vrange"].as<float>());
			chan->SetOffset(cnode["offset"].as<float>());

			if(cnode["coupling"])
			{
				string coupling = cnode["coupling"].as<string>();
				if(coupling == "dc_50")
					chan->SetCoupling(OscilloscopeChannel::COUPLE_DC_50);
				else if(coupling == "dc_1M")
					chan->SetCoupling(OscilloscopeChannel::COUPLE_DC_1M);
				else if(coupling == "ac_1M")
					chan->SetCoupling(OscilloscopeChannel::COUPLE_AC_1M);
				else if(coupling == "gnd")
					chan->SetCoupling(OscilloscopeChannel::COUPLE_GND);
			}
		}
	}
}

void Oscilloscope::EnableTriggerOutput()
{
	//do nothing, assuming the scope needs no config to enable trigger out
}

void Oscilloscope::SetUseExternalRefclk(bool external)
{
	//override this function in the driver class if an external reference input is present
	if(external)
		LogWarning("Oscilloscope::SetUseExternalRefclk: no external reference supported\n");
}

void Oscilloscope::SetDeskewForChannel(size_t /*channel*/, int64_t /*skew*/)
{
	//override this function in the driver class if deskew is supported
}

int64_t Oscilloscope::GetDeskewForChannel(size_t /*channel*/)
{
	//override this function in the driver class if deskew is supported
	return 0;
}

bool Oscilloscope::CanInterleave()
{
	//Check each conflict in the list
	auto conflicts = GetInterleaveConflicts();
	for(auto c : conflicts)
	{
		if(c.first->IsEnabled() && c.second->IsEnabled())
			return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration (default no-op for scopes without MSO feature)

vector<Oscilloscope::DigitalBank> Oscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> ret;
	return ret;
}

Oscilloscope::DigitalBank Oscilloscope::GetDigitalBank(size_t /*channel*/)
{
	return DigitalBank();
}

bool Oscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool Oscilloscope::IsDigitalThresholdConfigurable()
{
	return false;
}

float Oscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0.1;
}

float Oscilloscope::GetDigitalThreshold(size_t /*channel*/)
{
	return 0.5;
}

void Oscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
}

void Oscilloscope::SetDigitalThreshold(size_t /*channel*/, float /*level*/)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trigger configuration

vector<string> Oscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(EdgeTrigger::GetTriggerName());
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spectrum analyzer configuration (default no-op for scopes without SA feature)

void Oscilloscope::SetSpan(int64_t /*span*/)
{
}

int64_t Oscilloscope::GetSpan()
{
	return 1;
}

void Oscilloscope::SetCenterFrequency(size_t /*channel*/, int64_t /*freq*/)
{
}

int64_t Oscilloscope::GetCenterFrequency(size_t /*channel*/)
{
	return 0;
}

void Oscilloscope::SetResolutionBandwidth(int64_t /*freq*/)
{
}

int64_t Oscilloscope::GetResolutionBandwidth()
{
	return 1;
}

bool Oscilloscope::HasFrequencyControls()
{
	return false;
}
