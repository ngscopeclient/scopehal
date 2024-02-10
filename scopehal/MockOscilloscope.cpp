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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of MockOscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "MockOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MockOscilloscope::MockOscilloscope(
	const string& name,
	const string& vendor,
	const string& serial,
	const std::string& transport,
	const std::string& driver,
	const std::string& args)
	: m_name(name)
	, m_vendor(vendor)
	, m_serial(serial)
	, m_extTrigger(NULL)
	, m_transport(transport)
	, m_driver(driver)
	, m_args(args)
{
	//Need to run this loader prior to the main Oscilloscope loader
	m_loaders.push_front(sigc::mem_fun(*this, &MockOscilloscope::DoLoadConfiguration));

	m_serializers.push_back(sigc::mem_fun(*this, &MockOscilloscope::DoSerializeConfiguration));
}

MockOscilloscope::~MockOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

bool MockOscilloscope::IsOffline()
{
	return true;
}

string MockOscilloscope::IDPing()
{
	return "";
}

string MockOscilloscope::GetTransportName()
{
	return m_transport;
}

string MockOscilloscope::GetTransportConnectionString()
{
	return m_args;
}

unsigned int MockOscilloscope::GetInstrumentTypes() const
{
	return INST_OSCILLOSCOPE;
}

uint32_t MockOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

string MockOscilloscope::GetName() const
{
	return m_name;
}

string MockOscilloscope::GetVendor() const
{
	return m_vendor;
}

string MockOscilloscope::GetSerial() const
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerMode MockOscilloscope::PollTrigger()
{
	//we never trigger
	return TRIGGER_MODE_STOP;
}

bool MockOscilloscope::AcquireData()
{
	//no new data possible
	return false;
}

void MockOscilloscope::ArmTrigger()
{
	//no-op, we never trigger
}

void MockOscilloscope::StartSingleTrigger()
{
	//no-op, we never trigger
}

void MockOscilloscope::Start()
{
	//no-op, we never trigger
}

void MockOscilloscope::Stop()
{
	//no-op, we never trigger
}

void MockOscilloscope::ForceTrigger()
{
	//no-op, we never trigger
}

bool MockOscilloscope::IsTriggerArmed()
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void MockOscilloscope::DoSerializeConfiguration(YAML::Node& node, IDTable& /*table*/)
{
	node["transport"] = GetTransportName();
	node["args"] = GetTransportConnectionString();
	node["driver"] = GetDriverName();
}

void MockOscilloscope::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& table)
{
	//Load the channels
	auto& chans = node["channels"];
	for(auto it : chans)
	{
		auto& cnode = it.second;

		//Allocate channel space if we didn't have it yet
		size_t index = cnode["index"].as<int>();
		if(m_channels.size() < (index+1))
			m_channels.resize(index+1);

		//Configure the channel
		Stream::StreamType type = Stream::STREAM_TYPE_PROTOCOL;
		string stype = cnode["type"].as<string>();
		if(stype == "analog")
			type = Stream::STREAM_TYPE_ANALOG;
		else if(stype == "digital")
			type = Stream::STREAM_TYPE_DIGITAL;
		else if(stype == "trigger")
			type = Stream::STREAM_TYPE_TRIGGER;
		else if(stype == "protocol")
			type = Stream::STREAM_TYPE_PROTOCOL;

		auto chan = new OscilloscopeChannel(
			this,
			cnode["name"].as<string>(),
			cnode["color"].as<string>(),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			type,
			index);
		m_channels[index] = chan;

		//Create the channel ID
		table.emplace(cnode["id"].as<int>(), chan);
	}

	//If any of our channels are null, we're missing configuration for them in the file
	//Create dummy channels so nothing segfaults
	for(size_t i=0; i<m_channels.size(); i++)
	{
		if(m_channels[i] != nullptr)
			continue;

		auto chan = new OscilloscopeChannel(
			this,
			"MISSINGNO.",
			"#808080",
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_UNDEFINED,
			i);
		m_channels[i] = chan;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration. Mostly trivial stubs.

bool MockOscilloscope::IsChannelEnabled(size_t i)
{
	return m_channelsEnabled[i];
}

void MockOscilloscope::EnableChannel(size_t i)
{
	m_channelsEnabled[i] = true;
}

void MockOscilloscope::DisableChannel(size_t i)
{
	m_channelsEnabled[i] = false;
}

vector<OscilloscopeChannel::CouplingType> MockOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);
	//TODO: other options? or none?
	return ret;
}

OscilloscopeChannel::CouplingType MockOscilloscope::GetChannelCoupling(size_t i)
{
	return m_channelCoupling[i];
}

void MockOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	m_channelCoupling[i] = type;
}

double MockOscilloscope::GetChannelAttenuation(size_t i)
{
	return m_channelAttenuation[i];
}

void MockOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	m_channelAttenuation[i] = atten;
}

unsigned int MockOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	return m_channelBandwidth[i];
}

void MockOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	m_channelBandwidth[i] = limit_mhz;
}

float MockOscilloscope::GetChannelVoltageRange(size_t i, size_t stream)
{
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void MockOscilloscope::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)] = range;
}

OscilloscopeChannel* MockOscilloscope::GetExternalTrigger()
{
	return m_extTrigger;
}

float MockOscilloscope::GetChannelOffset(size_t i, size_t stream)
{
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void MockOscilloscope::SetChannelOffset(size_t i, size_t stream, float offset)
{
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}

vector<uint64_t> MockOscilloscope::GetSampleRatesNonInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> MockOscilloscope::GetSampleRatesInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> MockOscilloscope::GetInterleaveConflicts()
{
	//no-op
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> MockOscilloscope::GetSampleDepthsNonInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> MockOscilloscope::GetSampleDepthsInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

uint64_t MockOscilloscope::GetSampleRate()
{
	return m_sampleRate;
}

uint64_t MockOscilloscope::GetSampleDepth()
{
	return m_sampleDepth;
}

void MockOscilloscope::SetSampleDepth(uint64_t depth)
{
	m_sampleDepth = depth;
}

void MockOscilloscope::SetSampleRate(uint64_t rate)
{
	m_sampleRate = rate;
}

void MockOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t MockOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}

bool MockOscilloscope::IsInterleaving()
{
	return false;
}

bool MockOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void MockOscilloscope::PushTrigger()
{
	//no-op
}

void MockOscilloscope::PullTrigger()
{
	//no-op
}

/**
	@brief Calculate min/max of each channel and adjust gain/offset accordingly
 */
void MockOscilloscope::AutoscaleVertical()
{
	for(auto c : m_channels)
	{
		auto chan = dynamic_cast<OscilloscopeChannel*>(c);
		if(!chan)
			continue;

		auto swfm = dynamic_cast<SparseAnalogWaveform*>(chan->GetData(0));
		auto uwfm = dynamic_cast<UniformAnalogWaveform*>(chan->GetData(0));

		float vmin;
		float vmax;

		if(swfm)
		{
			if(swfm->m_samples.empty())
				continue;

			vmin = vmax = swfm->m_samples[0];

			for(auto s : swfm->m_samples)
			{
				vmin = min(vmin, (float)s);
				vmax = max(vmax, (float)s);
			}
		}

		else if(uwfm)
		{
			if(uwfm->m_samples.empty())
				continue;

			vmin = vmax = uwfm->m_samples[0];

			for(auto s : uwfm->m_samples)
			{
				vmin = min(vmin, (float)s);
				vmax = max(vmax, (float)s);
			}
		}

		else
			continue;

		//Calculate bounds
		chan->SetVoltageRange((vmax - vmin) * 1.05, 0);
		chan->SetOffset( -( (vmax - vmin)/2 + vmin ), 0);
	}
}
