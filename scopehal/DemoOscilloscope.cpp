/***********************************************************************************************************************
*                                                                                                                      *
* ngscopeclient                                                                                                        *
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
	@brief Implementation of DemoOscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "DemoOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DemoOscilloscope::DemoOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
	, m_extTrigger(NULL)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
{
	for(int i=0; i<5; i++)
	{
		m_rng[i] = new minstd_rand(m_rd());
		m_source[i] = new TestWaveformSource(*m_rng[i]);
	}

	m_model = "Oscilloscope Simulator";
	m_vendor = "Antikernel Labs";
	m_serial = "12345";

	//Create a bunch of channels
	static const char* colors[8] =
	{ "#ffff00", "#ff6abc", "#00ffff", "#00c100", "#d7ffd7", "#8482ff", "#ff0000", "#ff8000" };

	for(size_t i=0; i<4; i++)
	{
		m_channels.push_back(
			new OscilloscopeChannel(
				this,
				string("CH") + to_string(i+1),
				colors[i],
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_VOLTS),
				Stream::STREAM_TYPE_ANALOG,
				i));

		//initial configuration is 1V p-p for each
		m_channelsEnabled[i] = true;
		m_channelCoupling[i] = OscilloscopeChannel::COUPLE_DC_50;
		m_channelAttenuation[i] = 10;
		m_channelBandwidth[i] = 0;
		m_channelVoltageRange[i] = 1;
		m_channelOffset[i] = 0;

		m_channelModes[i] = CHANNEL_MODE_NOISE_LPF;
	}

	m_sweepFreq = 1e9;

	//Default sampling configuration
	m_depth = 100e3;
	m_rate = 50e9;

	m_channels[0]->SetDisplayName("Tone");
	m_channels[1]->SetDisplayName("Ramp");
	m_channels[2]->SetDisplayName("PRBS31");
	m_channels[3]->SetDisplayName("8B10B");
}

DemoOscilloscope::~DemoOscilloscope()
{
	for(int i=0; i<4; i++)
	{
		delete m_source[i];
		delete m_rng[i];
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

string DemoOscilloscope::IDPing()
{
	return "";
}

string DemoOscilloscope::GetTransportName()
{
	return "null";
}

string DemoOscilloscope::GetTransportConnectionString()
{
	return "";
}

string DemoOscilloscope::GetDriverNameInternal()
{
	return "demo";
}

unsigned int DemoOscilloscope::GetInstrumentTypes() const
{
	return INST_OSCILLOSCOPE;
}

uint32_t DemoOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerMode DemoOscilloscope::PollTrigger()
{
	if(m_triggerArmed)
		return TRIGGER_MODE_TRIGGERED;
	else
		return TRIGGER_MODE_STOP;
}

void DemoOscilloscope::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void DemoOscilloscope::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void DemoOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

void DemoOscilloscope::ForceTrigger()
{
	StartSingleTrigger();
}

bool DemoOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void DemoOscilloscope::LoadConfiguration(int version, const YAML::Node& node, IDTable& table)
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

	//Call the base class to configure everything
	Oscilloscope::LoadConfiguration(version, node, table);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration. Mostly trivial stubs.

bool DemoOscilloscope::IsChannelEnabled(size_t i)
{
	return m_channelsEnabled[i];
}

void DemoOscilloscope::EnableChannel(size_t i)
{
	m_channelsEnabled[i] = true;
}

void DemoOscilloscope::DisableChannel(size_t i)
{
	m_channelsEnabled[i] = false;
}

OscilloscopeChannel::CouplingType DemoOscilloscope::GetChannelCoupling(size_t i)
{
	return m_channelCoupling[i];
}

vector<OscilloscopeChannel::CouplingType> DemoOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	return ret;
}

void DemoOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	m_channelCoupling[i] = type;
}

double DemoOscilloscope::GetChannelAttenuation(size_t i)
{
	return m_channelAttenuation[i];
}

void DemoOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	m_channelAttenuation[i] = atten;
}

unsigned int DemoOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	return m_channelBandwidth[i];
}

void DemoOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	m_channelBandwidth[i] = limit_mhz;
}

float DemoOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	return m_channelVoltageRange[i];
}

void DemoOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	m_channelVoltageRange[i] = range;
}

OscilloscopeChannel* DemoOscilloscope::GetExternalTrigger()
{
	return m_extTrigger;
}

float DemoOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	return m_channelOffset[i];
}

void DemoOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	m_channelOffset[i] = offset;
}

vector<uint64_t> DemoOscilloscope::GetSampleRatesNonInterleaved()
{
	uint64_t k = 1000;
	uint64_t m = k * k;
	uint64_t g = k * m;

	vector<uint64_t> ret;
	ret.push_back(1 * g);
	ret.push_back(5 * g);
	ret.push_back(10 * g);
	ret.push_back(25 * g);
	ret.push_back(50 * g);
	ret.push_back(100 * g);
	ret.push_back(200 * g);
	ret.push_back(500 * g);
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleRatesInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> DemoOscilloscope::GetInterleaveConflicts()
{
	//no-op
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleDepthsNonInterleaved()
{
	uint64_t k = 1000;
	uint64_t m = k * k;

	vector<uint64_t> ret;
	ret.push_back(10 * k);
	ret.push_back(100 * k);
	ret.push_back(1 * m);
	ret.push_back(10 * m);
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleDepthsInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

uint64_t DemoOscilloscope::GetSampleRate()
{
	return m_rate;
}

uint64_t DemoOscilloscope::GetSampleDepth()
{
	return m_depth;
}

void DemoOscilloscope::SetSampleDepth(uint64_t depth)
{
	m_depth = depth;
}

void DemoOscilloscope::SetSampleRate(uint64_t rate)
{
	m_rate = rate;
}

void DemoOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t DemoOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}

bool DemoOscilloscope::IsInterleaving()
{
	return false;
}

bool DemoOscilloscope::SetInterleaving([[maybe_unused]] bool combine)
{
	return false;
}

void DemoOscilloscope::PushTrigger()
{
	//no-op
}

void DemoOscilloscope::PullTrigger()
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waveform degradation modes

vector<string> DemoOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> ret;
	ret.push_back("Ideal");
	ret.push_back("10 mV noise");
	ret.push_back("10 mV noise + 5 GHz LPF");
	return ret;
}

size_t DemoOscilloscope::GetADCMode(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return m_channelModes[channel];
}

void DemoOscilloscope::SetADCMode(size_t channel, size_t mode)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_channelModes[channel] = mode;
}

bool DemoOscilloscope::IsADCModeConfigurable()
{
	return true;
}

vector<Oscilloscope::AnalogBank> DemoOscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> ret;
	for(size_t i=0; i<GetChannelCount(); i++)
		ret.push_back(GetAnalogBank(i));
	return ret;
}

Oscilloscope::AnalogBank DemoOscilloscope::GetAnalogBank(size_t channel)
{
	AnalogBank bank;
	bank.push_back(GetOscilloscopeChannel(channel));
	return bank;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waveform synthesis

bool DemoOscilloscope::AcquireData()
{
	//cap waveform rate at 50 wfm/s to avoid saturating cpu
	std::this_thread::sleep_for(std::chrono::microseconds(20 * 1000));

	//Sweeping frequency
	m_sweepFreq += 1e6;
	if(m_sweepFreq > 1.5e9)
		m_sweepFreq = 1.1e9;
	float sweepPeriod = FS_PER_SECOND / m_sweepFreq;

	//Signal degradations
	float noise[4] =
	{
		0.01, 0.01, 0.01, 0.01
	};
	bool lpf2 = false;
	bool lpf3 = false;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		for(size_t i=0; i<4; i++)
		{
			if(m_channelModes[i] == CHANNEL_MODE_IDEAL)
				noise[i] = 0;
		}
		if(m_channelModes[2] == CHANNEL_MODE_NOISE_LPF)
			lpf2 = true;
		if(m_channelModes[3] == CHANNEL_MODE_NOISE_LPF)
			lpf3 = true;
	}

	//Generate waveforms

	auto depth = GetSampleDepth();
	int64_t sampleperiod = FS_PER_SECOND / m_rate;
	WaveformBase* waveforms[5] = {NULL};
	#pragma omp parallel for
	for(int i=0; i<5; i++)
	{
		if(!m_channelsEnabled[i])
			continue;

		switch(i)
		{
			case 0:
				waveforms[i] = m_source[i]->GenerateNoisySinewave(0.9, 0.0, 1e6, sampleperiod, depth, noise[0]);
				break;

			case 1:
				waveforms[i] = m_source[i]->GenerateNoisySinewaveMix(0.9, 0.0, M_PI_4, 1e6, sweepPeriod, sampleperiod, depth, noise[1]);
				break;

			case 2:
				waveforms[i] = m_source[i]->GeneratePRBS31(0.9, 96969.6, sampleperiod, depth, lpf2, noise[2]);
				break;

			case 3:
				waveforms[i] = m_source[i]->Generate8b10b(0.9, 800e3, sampleperiod, depth, lpf3, noise[3]);
				break;

			default:
				break;
		}

		waveforms[i]->MarkModifiedFromCpu();
	}

	SequenceSet s;
	for(int i=0; i<4; i++)
		s[GetOscilloscopeChannel(i)] = waveforms[i];

	//Timestamp the waveform(s)
	double now = GetTime();
	time_t start = now;
	double tfrac = now - start;
	int64_t fs = tfrac * FS_PER_SECOND;
	for(auto it : s)
	{
		auto wfm = it.second;
		if(!wfm)
			continue;

		wfm->m_startTimestamp = start;
		wfm->m_startFemtoseconds = fs;
		wfm->m_triggerPhase = 0;
	}

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

