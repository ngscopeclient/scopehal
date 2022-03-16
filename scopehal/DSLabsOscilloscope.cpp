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
#include "DSLabsOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;

#define RATE_5GSPS		(5000L * 1000L * 1000L)
#define RATE_2P5GSPS	(2500L * 1000L * 1000L)
#define RATE_1P25GSPS	(1250L * 1000L * 1000L)
#define RATE_625MSPS	(625L * 1000L * 1000L)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

DSLabsOscilloscope::DSLabsOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport, true)
	, m_triggerArmed(false)
{
	//Set up initial cache configuration as "not valid" and let it populate as we go
	IdentifyHardware();

	//Add analog channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = "0";
		chname[0] += i;

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			GetChannelColor(i),
			1,
			i,
			true);
		m_channels.push_back(chan);

		string nicename = "ch0";
		nicename[2] += i;
		chan->SetDisplayName(nicename);

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 1;
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_AC_1M);
		SetChannelOffset(i, 0,  0);
		SetChannelVoltageRange(i, 0, 5);
	}

	//Set initial memory configuration.
	SetSampleRate(100000L);
	SetSampleDepth(1000);

	//Set up the data plane socket
	auto csock = dynamic_cast<SCPISocketTransport*>(m_transport);
	if(!csock)
		LogFatal("DSLabsOscilloscope expects a SCPISocketTransport\n");

	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(m_channels[0]));
	SetTrigger(trig);
	PushTrigger();
	SetTriggerOffset(1000000000000); //1ms to allow trigphase interpolation

	//For now, assume control plane port is data plane +1
	LogDebug("Connecting to data plane socket\n");
	m_dataSocket = new Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	m_dataSocket->Connect(csock->GetHostname(), csock->GetPort() + 1);
	m_dataSocket->DisableNagle();
}

/**
	@brief Color the channels based on Pico's standard color sequence (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string DSLabsOscilloscope::GetChannelColor(size_t i)
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

void DSLabsOscilloscope::IdentifyHardware()
{
	//Assume no MSO channels to start
	m_digitalChannelCount = 0;

	m_series = SERIES_UNKNOWN;

	if (m_model == "DSCope U3P100") {
		m_series = DSCOPE_U3P100;
		LogDebug("Found DSCope U3P100\n");
	}

	if (m_series == SERIES_UNKNOWN)
		LogWarning("Unknown DSLabs model \"%s\"\n", m_model.c_str());

	//Ask the scope how many channels it has
	m_transport->SendCommand("CHANS?");
	m_analogChannelCount = stoi(m_transport->ReadReply());
}

DSLabsOscilloscope::~DSLabsOscilloscope()
{
	delete m_dataSocket;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int DSLabsOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string DSLabsOscilloscope::GetDriverNameInternal()
{
	return "dslabs";
}

void DSLabsOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

bool DSLabsOscilloscope::IsChannelEnabled(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelsEnabled[i];
}

void DSLabsOscilloscope::EnableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = true;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":ON");
}

void DSLabsOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":OFF");
}

OscilloscopeChannel::CouplingType DSLabsOscilloscope::GetChannelCoupling(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelCouplings[i];
}

vector<OscilloscopeChannel::CouplingType> DSLabsOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	return ret;
}

void DSLabsOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
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

double DSLabsOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void DSLabsOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	double oldAtten = m_channelAttenuations[i];
	m_channelAttenuations[i] = atten;

	//Rescale channel voltage range and offset
	double delta = atten / oldAtten;
	m_channelVoltageRanges[i] *= delta;
	m_channelOffsets[i] *= delta;
}

int DSLabsOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void DSLabsOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

float DSLabsOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRanges[i];
}

void DSLabsOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
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

OscilloscopeChannel* DSLabsOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

float DSLabsOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffsets[i];
}

void DSLabsOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
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

Oscilloscope::TriggerMode DSLabsOscilloscope::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool DSLabsOscilloscope::AcquireData()
{
	const uint8_t r = 'K';
	m_dataSocket->SendLooped(&r, 1);

	//Read the sequence number of the current waveform
	uint32_t seqnum;
	if(!m_dataSocket->RecvLooped((uint8_t*)&seqnum, sizeof(seqnum)))
		return false;

	//Read the number of channels in the current waveform
	uint16_t numChannels;
	if(!m_dataSocket->RecvLooped((uint8_t*)&numChannels, sizeof(numChannels)))
		return false;

	//Get the sample interval.
	//May be different from m_srate if we changed the rate after the trigger was armed
	int64_t fs_per_sample;
	if(!m_dataSocket->RecvLooped((uint8_t*)&fs_per_sample, sizeof(fs_per_sample)))
		return false;

	// LogDebug("Receive header: SEQ#%u, %d channels\n", seqnum, numChannels);

	//Acquire data for each channel
	size_t chnum;
	size_t memdepth;
	float config[3];
	SequenceSet s;
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Analog channels get processed separately
	vector<uint8_t*> abufs;
	vector<AnalogWaveform*> awfms;
	vector<float> scales;
	vector<float> offsets;

	for(size_t i=0; i<numChannels; i++)
	{
		//Get channel ID and memory depth (samples, not bytes)
		if(!m_dataSocket->RecvLooped((uint8_t*)&chnum, sizeof(chnum)))
			return false;
		if(!m_dataSocket->RecvLooped((uint8_t*)&memdepth, sizeof(memdepth)))
			return false;

		// LogDebug("ch%ld: Receive %ld samples\n", chnum, memdepth);

		uint8_t* buf = new uint8_t[memdepth];

		//Analog channels
		if(chnum < m_analogChannelCount)
		{
			abufs.push_back(buf);

			//Scale and offset are sent in the header since they might have changed since the capture began
			if(!m_dataSocket->RecvLooped((uint8_t*)&config, sizeof(config)))
				return false;
			float scale = config[0];
			float offset = config[1];
			float trigphase = -config[2] * fs_per_sample;
			scale *= GetChannelAttenuation(chnum);

			//TODO: stream timestamp from the server

			if(!m_dataSocket->RecvLooped((uint8_t*)buf, memdepth * sizeof(int8_t)))
				return false;

			//Create our waveform
			AnalogWaveform* cap = new AnalogWaveform;
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = trigphase;
			cap->m_startTimestamp = time(NULL);
			cap->m_densePacked = true;
			cap->m_startFemtoseconds = fs;
			cap->Resize(memdepth);
			awfms.push_back(cap);
			scales.push_back(scale);
			offsets.push_back(offset);

			s[m_channels[chnum]] = cap;
		}
	}

	//Process analog captures in parallel
	#pragma omp parallel for
	for(size_t i=0; i<awfms.size(); i++)
	{
		auto cap = awfms[i];
		ConvertUnsigned8BitSamples(
			(int64_t*)&cap->m_offsets[0],
			(int64_t*)&cap->m_durations[0],
			(float*)&cap->m_samples[0],
			abufs[i],
			scales[i],
			offsets[i],
			cap->m_offsets.size(),
			0);
		delete[] abufs[i];
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

void DSLabsOscilloscope::Start()
{
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.clear();
	m_pendingWaveformsMutex.unlock();

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("START");
	m_transport->FlushCommandQueue();
	m_transport->ReadReply();

	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void DSLabsOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SINGLE");
	m_transport->FlushCommandQueue();
	m_transport->ReadReply();

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.clear();
	m_pendingWaveformsMutex.unlock();

	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void DSLabsOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");
	m_transport->FlushCommandQueue();
	m_transport->ReadReply();

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.clear();
	m_pendingWaveformsMutex.unlock();

	m_triggerArmed = false;
}

void DSLabsOscilloscope::ForceTrigger()
{
	// lock_guard<recursive_mutex> lock(m_mutex);
	// m_transport->SendCommand("FORCE");
	// m_triggerArmed = true;
	// m_triggerOneShot = true;

	this->StartSingleTrigger();
}

bool DSLabsOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> DSLabsOscilloscope::GetSampleRatesNonInterleaved()
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
		auto hz = stol(block);
		ret.push_back(hz);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> DSLabsOscilloscope::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> DSLabsOscilloscope::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> DSLabsOscilloscope::GetSampleDepthsNonInterleaved()
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

vector<uint64_t> DSLabsOscilloscope::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

uint64_t DSLabsOscilloscope::GetSampleRate()
{
	return m_srate;
}

uint64_t DSLabsOscilloscope::GetSampleDepth()
{
	return m_mdepth;
}

void DSLabsOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(string("DEPTH ") + to_string(depth));
	m_mdepth = depth;
}

void DSLabsOscilloscope::SetSampleRate(uint64_t rate)
{
	m_srate = rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand( string("RATE ") + to_string(rate));
}

void DSLabsOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Don't allow setting trigger offset beyond the end of the capture
	int64_t captureDuration = GetSampleDepth() * FS_PER_SECOND / GetSampleRate();
	m_triggerOffset = min(offset, captureDuration);

	PushTrigger();
}

int64_t DSLabsOscilloscope::GetTriggerOffset()
{
	return m_triggerOffset;
}

bool DSLabsOscilloscope::IsInterleaving()
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

bool DSLabsOscilloscope::SetInterleaving(bool /*combine*/)
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

void DSLabsOscilloscope::PullTrigger()
{
	//pulling not needed, we always have a valid trigger cached
}

void DSLabsOscilloscope::PushTrigger()
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
void DSLabsOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
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

vector<Oscilloscope::AnalogBank> DSLabsOscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> banks;
	banks.push_back(GetAnalogBank(0));
	return banks;
}

Oscilloscope::AnalogBank DSLabsOscilloscope::GetAnalogBank(size_t /*channel*/)
{
	AnalogBank bank;
	return bank;
}

bool DSLabsOscilloscope::IsADCModeConfigurable()
{
	return false;
}

vector<string> DSLabsOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	//All scopes with variable resolution start at 8 bit and go up from there
	vector<string> ret;
	ret.push_back("8 Bit");
	return ret;
}

size_t DSLabsOscilloscope::GetADCMode(size_t /*channel*/)
{
	return 0;
}

void DSLabsOscilloscope::SetADCMode(size_t /*channel*/, size_t /*mode*/)
{
	return;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> DSLabsOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;
	return banks;
}

Oscilloscope::DigitalBank DSLabsOscilloscope::GetDigitalBank(size_t /*channel*/)
{
	DigitalBank ret;
	return ret;
}

bool DSLabsOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool DSLabsOscilloscope::IsDigitalThresholdConfigurable()
{
	return false;
}

float DSLabsOscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0;
}

float DSLabsOscilloscope::GetDigitalThreshold(size_t /*channel*/)
{
	return 0;
}

void DSLabsOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
	// TODO
}

void DSLabsOscilloscope::SetDigitalThreshold(size_t /*channel*/, float /*level*/)
{
	// TODO
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checking for validity of configurations

bool DSLabsOscilloscope::CanEnableChannel(size_t /*i*/)
{
	return true;
}
