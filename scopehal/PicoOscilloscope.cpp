/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "PicoOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

PicoOscilloscope::PicoOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_triggerArmed(false)
{
	//Set up initial cache configuration as "not valid" and let it populate as we go

	//Ask the scope how many channels it has
	m_transport->SendCommand("CHANS?");
	m_analogChannelCount = stoi(m_transport->ReadReply());

	//TODO: Check digital channels

	//Add channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = "A";
		chname[0] += i;

		//Color the channels based on Pico's standard color sequence (blue-red-green-yellow-purple-gray-cyan-magenta)
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#4040ff";
				break;

			case 1:
				color = "#ff4040";
				break;

			case 2:
				color = "#208020";
				break;

			case 3:
				color = "#ffff00";
				break;

			case 4:
				color = "#600080";
				break;

			case 5:
				color = "#808080";
				break;

			case 6:
				color = "#40a0a0";
				break;

			case 7:
				color = "#e040e0";
				break;
		}

		//Create the channel
		auto chan = new OscilloscopeChannel(this, chname, OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, 1, i, true);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 1;
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_DC_1M);
		SetChannelOffset(i, 0);
		SetChannelVoltageRange(i, 5);
	}

	//Set initial memory configuration
	SetSampleRate(1250000000L);
	SetSampleDepth(1000000);

	//Add the external trigger input
	m_extTrigChannel =
		new OscilloscopeChannel(this, "EX", OscilloscopeChannel::CHANNEL_TYPE_TRIGGER, "", 1, m_channels.size(), true);
	m_channels.push_back(m_extTrigChannel);
	m_extTrigChannel->SetDefaultDisplayName();

	//Set up the data plane socket
	auto csock = dynamic_cast<SCPISocketTransport*>(m_transport);
	if(!csock)
		LogFatal("PicoOscilloscope expects a SCPISocketTransport\n");

	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(m_channels[0]));
	SetTrigger(trig);
	m_triggerOffset = 0;
	PushTrigger();

	//For now, assume control plane port is data plane +1
	LogDebug("Connecting to data plane socket\n");
	m_dataSocket = new SCPISocketTransport(csock->GetHostname(), csock->GetPort() + 1);
}

PicoOscilloscope::~PicoOscilloscope()
{
	delete m_dataSocket;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int PicoOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string PicoOscilloscope::GetDriverNameInternal()
{
	return "pico";
}

void PicoOscilloscope::FlushConfigCache()
{
	//no-op
}

bool PicoOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelsEnabled[i];
}

void PicoOscilloscope::EnableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = true;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":ON");
}

void PicoOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":OFF");
}

OscilloscopeChannel::CouplingType PicoOscilloscope::GetChannelCoupling(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelCouplings[i];
}

vector<OscilloscopeChannel::CouplingType> PicoOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);
	return ret;
}

void PicoOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	bool valid = true;
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
			LogError("Invalid coupling for channel\n");
			valid = false;
	}

	if(valid)
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelCouplings[i] = type;
	}
}

double PicoOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void PicoOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	double oldAtten = m_channelAttenuations[i];
	m_channelAttenuations[i] = atten;

	//Rescale channel voltage range and offset
	double delta = atten / oldAtten;
	m_channelVoltageRanges[i] *= delta;
	m_channelOffsets[i] *= delta;
}

int PicoOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void PicoOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

double PicoOscilloscope::GetChannelVoltageRange(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRanges[i];
}

void PicoOscilloscope::SetChannelVoltageRange(size_t i, double range)
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

OscilloscopeChannel* PicoOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

double PicoOscilloscope::GetChannelOffset(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffsets[i];
}

void PicoOscilloscope::SetChannelOffset(size_t i, double offset)
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

Oscilloscope::TriggerMode PicoOscilloscope::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool PicoOscilloscope::AcquireData()
{
	//Read the number of channels in the current waveform
	uint16_t numChannels;
	if(!m_dataSocket->ReadRawData(sizeof(numChannels), (uint8_t*)&numChannels))
		return false;

	//Get the sample interval.
	//May be different from m_srate if we changed the rate after the trigger was armed
	int64_t fs_per_sample;
	if(!m_dataSocket->ReadRawData(sizeof(fs_per_sample), (uint8_t*)&fs_per_sample))
		return false;

	//Acquire data for each channel
	size_t chnum;
	size_t memdepth;
	float scale;
	SequenceSet s;
	for(size_t i=0; i<numChannels; i++)
	{
		//Get channel ID and memory depth (samples, not bytes)
		if(!m_dataSocket->ReadRawData(sizeof(chnum), (uint8_t*)&chnum))
			return false;
		if(!m_dataSocket->ReadRawData(sizeof(memdepth), (uint8_t*)&memdepth))
			return false;
		if(!m_dataSocket->ReadRawData(sizeof(scale), (uint8_t*)&scale))
			return false;
		scale *= GetChannelAttenuation(chnum);

		//TODO: stream timestamp from the server

		//Allocate the buffer
		int16_t* buf = new int16_t[memdepth];
		if(!m_dataSocket->ReadRawData(memdepth * sizeof(int16_t), (uint8_t*)buf))
			return false;

		auto offset = GetChannelOffset(chnum);

		//Create our waveform
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = fs_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		cap->m_densePacked = true;
		double t = GetTime();
		cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
		cap->Resize(memdepth);
		for(size_t j=0; j<memdepth; j++)
		{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = (buf[j] * scale) + offset;
		}

		s[m_channels[chnum]] = cap;

		//Clean up
		delete[] buf;
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

void PicoOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("START");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void PicoOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SINGLE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void PicoOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");
	m_triggerArmed = false;
}

bool PicoOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> PicoOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	string rates;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("RATES?");
		rates = m_transport->ReadReply();
	}

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = rates.find(',', i+1);
		if(i == string::npos)
			break;

		auto block = rates.substr(istart, i-istart);
		auto fs = stol(block);
		auto hz = FS_PER_SECOND / fs;
		ret.push_back(hz);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> PicoOscilloscope::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> PicoOscilloscope::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> PicoOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

	string depths;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("DEPTHS?");
		depths = m_transport->ReadReply();
	}

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = depths.find(',', i+1);
		if(i == string::npos)
			break;

		ret.push_back(stol(depths.substr(istart, i-istart)));

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> PicoOscilloscope::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

uint64_t PicoOscilloscope::GetSampleRate()
{
	return m_srate;
}

uint64_t PicoOscilloscope::GetSampleDepth()
{
	return m_mdepth;
}

void PicoOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(string("DEPTH ") + to_string(depth));
	m_mdepth = depth;
}

void PicoOscilloscope::SetSampleRate(uint64_t rate)
{
	m_srate = rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand( string("RATE ") + to_string(rate));
}

void PicoOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_triggerOffset = offset;
	PushTrigger();
}

int64_t PicoOscilloscope::GetTriggerOffset()
{
	return m_triggerOffset;
}

bool PicoOscilloscope::IsInterleaving()
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

bool PicoOscilloscope::SetInterleaving(bool /*combine*/)
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

void PicoOscilloscope::PullTrigger()
{
	//pulling not needed, we always have a valid trigger cached
}

void PicoOscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void PicoOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Type
	//m_transport->SendCommand(":TRIG:MODE EDGE");

	//Delay
	m_transport->SendCommand("TRIG:DELAY " + to_string(m_triggerOffset));

	//Source
	m_transport->SendCommand("TRIG:SOU " + trig->GetInput(0).m_channel->GetHwname());

	//Level
	char buf[128];
	snprintf(buf, sizeof(buf), "TRIG:LEV %f", trig->GetLevel());
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
