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
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_DC_1M);
		SetChannelOffset(i, 0);
		SetChannelVoltageRange(i, 5);
	}

	//Set initial sample rate
	SetSampleRate(1250000000L);

	//Add the external trigger input
	m_extTrigChannel =
		new OscilloscopeChannel(this, "EX", OscilloscopeChannel::CHANNEL_TYPE_TRIGGER, "", 1, m_channels.size(), true);
	m_channels.push_back(m_extTrigChannel);
	m_extTrigChannel->SetDefaultDisplayName();

	//Set up the data plane socket
	auto csock = dynamic_cast<SCPISocketTransport*>(m_transport);
	if(!csock)
		LogFatal("PicoOscilloscope expects a SCPISocketTransport\n");

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
	return 1;
}

void PicoOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
}

int PicoOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	return 0;
}

void PicoOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
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
	snprintf(buf, sizeof(buf), ":%s:RANGE %f", m_channels[i]->GetHwname().c_str(), range);
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
	snprintf(buf, sizeof(buf), ":%s:OFFS %f", m_channels[i]->GetHwname().c_str(), offset);
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
	//FIXME
	vector<uint64_t> ret;
/*	if(m_protocol == MSO5)
		ret = {
			1000,
			10 * 1000,
			100 * 1000,
			1000 * 1000,
			10 * 1000 * 1000,
			25 * 1000 * 1000,
		};*/
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
	/*
	if(m_mdepthValid)
		return m_mdepth;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(":ACQ:MDEP?");
	string ret = m_transport->ReadReply();

	double depth;
	sscanf(ret.c_str(), "%lf", &depth);
	m_mdepth = (uint64_t)depth;
	m_mdepthValid = true;
	return m_mdepth;
	*/
	return 1;
}

void PicoOscilloscope::SetSampleDepth(uint64_t depth)
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);
	if(m_protocol == MSO5)
	{
		switch(depth)
		{
			case 1000:
				m_transport->SendCommand("ACQ:MDEP 1k");
				break;
			case 10000:
				m_transport->SendCommand("ACQ:MDEP 10k");
				break;
			case 100000:
				m_transport->SendCommand("ACQ:MDEP 100k");
				break;
			case 1000000:
				m_transport->SendCommand("ACQ:MDEP 1M");
				break;
			case 10000000:
				m_transport->SendCommand("ACQ:MDEP 10M");
				break;
			case 25000000:
				m_transport->SendCommand("ACQ:MDEP 25M");
				break;
			case 50000000:
				if(m_opt200M)
					m_transport->SendCommand("ACQ:MDEP 50M");
				else
					LogError("Invalid memory depth for channel: %lu\n", depth);
				break;
			case 100000000:
				//m_transport->SendCommand("ACQ:MDEP 100M");
				LogError("Invalid memory depth for channel: %lu\n", depth);
				break;
			case 200000000:
				//m_transport->SendCommand("ACQ:MDEP 200M");
				LogError("Invalid memory depth for channel: %lu\n", depth);
				break;
			default:
				LogError("Invalid memory depth for channel: %lu\n", depth);
		}
	}
	else
	{
		LogError("Memory depth setting not implemented for this series");
	}
	m_mdepthValid = false;
	*/
}

void PicoOscilloscope::SetSampleRate(uint64_t rate)
{
	m_srate = rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand( string("RATE ") + to_string(rate));
}

void PicoOscilloscope::SetTriggerOffset(int64_t offset)
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	double offsetval = (double)offset / FS_PER_SECOND;
	char buf[128];
	snprintf(buf, sizeof(buf), ":TIM:MAIN:OFFS %f", offsetval);
	m_transport->SendCommand(buf);
	*/
}

int64_t PicoOscilloscope::GetTriggerOffset()
{
	/*
	if(m_triggerOffsetValid)
		return m_triggerOffset;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(":TIM:MAIN:OFFS?");
	string ret = m_transport->ReadReply();

	double offsetval;
	sscanf(ret.c_str(), "%lf", &offsetval);
	m_triggerOffset = (uint64_t)(offsetval * FS_PER_SECOND);
	m_triggerOffsetValid = true;
	return m_triggerOffset;
	*/
	return 0;
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
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	//Figure out what kind of trigger is active.
	m_transport->SendCommand(":TRIG:MODE?");
	string reply = m_transport->ReadReply();
	if (reply == "EDGE")
		PullEdgeTrigger();

	//Unrecognized trigger type
	else
	{
		LogWarning("Unknown trigger type \"%s\"\n", reply.c_str());
		m_trigger = NULL;
		return;
	}
	*/
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void PicoOscilloscope::PullEdgeTrigger()
{
	/*
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new EdgeTrigger(this);
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

	lock_guard<recursive_mutex> lock(m_mutex);

	//Source
	m_transport->SendCommand(":TRIG:EDGE:SOUR?");
	string reply = m_transport->ReadReply();
	auto chan = GetChannelByHwName(reply);
	et->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source %s\n", reply.c_str());

	//Level
	m_transport->SendCommand(":TRIG:EDGE:LEV?");
	reply = m_transport->ReadReply();
	et->SetLevel(stof(reply));

	//Edge slope
	m_transport->SendCommand(":TRIG:EDGE:SLOPE?");
	reply = m_transport->ReadReply();
	if (reply == "POS")
		et->SetType(EdgeTrigger::EDGE_RISING);
	else if (reply == "NEG")
		et->SetType(EdgeTrigger::EDGE_FALLING);
	else if (reply == "RFAL")
		et->SetType(EdgeTrigger::EDGE_ANY);
		*/
}

void PicoOscilloscope::PushTrigger()
{
	/*
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");*/
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void PicoOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	//Type
	m_transport->SendCommand(":TRIG:MODE EDGE");

	//Source
	m_transport->SendCommand(":TRIG:EDGE:SOUR " + trig->GetInput(0).m_channel->GetHwname());

	//Level
	char buf[128];
	snprintf(buf, sizeof(buf), ":TRIG:EDGE:LEV %f", trig->GetLevel());
	m_transport->SendCommand(buf);

	//Slope
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommand(":TRIG:EDGE:SLOPE POS");
			break;
		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommand(":TRIG:EDGE:SLOPE NEG");
			break;
		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommand(":TRIG:EDGE:SLOPE RFAL");
			break;
		default:
			LogWarning("Unknown edge type\n");
			return;
	}
	*/
}
