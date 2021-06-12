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

#define RATE_5GSPS		(5000L * 1000L * 1000L)
#define RATE_2P5GSPS	(2500L * 1000L * 1000L)
#define RATE_1P25GSPS	(1250L * 1000L * 1000L)
#define RATE_625MSPS	(625L * 1000L * 1000L)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

PicoOscilloscope::PicoOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_triggerArmed(false)
{
	//Set up initial cache configuration as "not valid" and let it populate as we go

	IdentifyHardware();

	//Set resolution
	SetADCMode(0, ADC_MODE_8BIT);

	//Add analog channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = "A";
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
		chan->SetDefaultDisplayName();

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 1;
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_DC_1M);
		SetChannelOffset(i, 0);
		SetChannelVoltageRange(i, 5);
	}

	//Add digital channels (named 1D0...7 and 2D0...7)
	m_digitalChannelBase = m_analogChannelCount;
	for(size_t i=0; i<m_digitalChannelCount; i++)
	{
		//Hardware name of the channel
		size_t ibank = i / 8;
		size_t ichan = i % 8;
		string chname = "1D0";
		chname[0] += ibank;
		chname[2] += ichan;

		//Create the channel
		size_t chnum = i + m_digitalChannelBase;
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
			GetChannelColor(ichan),
			1,
			chnum,
			true);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();

		SetDigitalHysteresis(chnum, 0.1);
		SetDigitalThreshold(chnum, 0);
	}

	//Set initial memory configuration.
	//625 Msps is the highest rate the 6000 series supports with all channels, including MSO, active.
	//TODO: pick reasonable default for other families
	SetSampleRate(625000000L);
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
	PushTrigger();
	SetTriggerOffset(10 * 1000L * 1000L);

	//For now, assume control plane port is data plane +1
	LogDebug("Connecting to data plane socket\n");
	m_dataSocket = new Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	m_dataSocket->Connect(csock->GetHostname(), csock->GetPort() + 1);
	m_dataSocket->DisableNagle();
}

/**
	@brief Color the channels based on Pico's standard color sequence (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string PicoOscilloscope::GetChannelColor(size_t i)
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

void PicoOscilloscope::IdentifyHardware()
{
	//Assume no MSO channels to start
	m_digitalChannelCount = 0;

	//Figure out device family
	if(m_model.length() < 5)
	{
		LogWarning("Unknown PicoScope model \"%s\"\n", m_model.c_str());
		m_series = SERIES_UNKNOWN;
	}
	else if(m_model[0] == '3')
	{
		if(m_model.find("MSO") > 0)
		{
			// PicoScope3000 support 16 Digital Channels for MSO (or nothing)
			m_digitalChannelCount = 16;
			m_series = SERIES_UNKNOWN;
		}
	}
	else if(m_model[0] == '6')
	{
		//We have two MSO pod connectors
		m_digitalChannelCount = 16;

		switch(m_model[2])
		{
			case '2':
				m_series = SERIES_6x2xE;
				break;

			case '0':
				if(m_model == "6403E")
					m_series = SERIES_6403E;
				else
					m_series = SERIES_6x0xE;
				break;

			default:
				LogWarning("Unknown PicoScope model \"%s\"\n", m_model.c_str());
				m_series = SERIES_UNKNOWN;
				break;
		}
	}
	else
	{
		LogWarning("Unknown PicoScope model \"%s\"\n", m_model.c_str());
		m_series = SERIES_UNKNOWN;
	}

	//Ask the scope how many channels it has
	m_transport->SendCommand("CHANS?");
	m_analogChannelCount = stoi(m_transport->ReadReply());
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
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//clear probe presence flags as those can change without our knowledge
	m_digitalBankPresent.clear();
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
	//If the pod is already active we don't have to touch anything scope side.
	//Update the cache and we're done.
	if(IsChannelIndexDigital(i))
	{
		size_t npod = GetDigitalPodIndex(i);
		if(IsDigitalPodActive(npod))
		{
			lock_guard<recursive_mutex> lock(m_cacheMutex);
			m_channelsEnabled[i] = true;
			return;
		}
	}

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

	//If the pod still has active channels after turning this one off, we don't have to touch anything scope side.
	if(IsChannelIndexDigital(i))
	{
		size_t npod = GetDigitalPodIndex(i);
		if(IsDigitalPodActive(npod))
			return;
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
	if(!m_dataSocket->RecvLooped((uint8_t*)&numChannels, sizeof(numChannels)))
		return false;

	//Get the sample interval.
	//May be different from m_srate if we changed the rate after the trigger was armed
	int64_t fs_per_sample;
	if(!m_dataSocket->RecvLooped((uint8_t*)&fs_per_sample, sizeof(fs_per_sample)))
		return false;

	//Acquire data for each channel
	size_t chnum;
	size_t memdepth;
	float config[3];
	SequenceSet s;
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Analog channels get processed separately
	vector<int16_t*> abufs;
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
		int16_t* buf = new int16_t[memdepth];

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

			if(!m_dataSocket->RecvLooped((uint8_t*)buf, memdepth * sizeof(int16_t)))
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

		//Digital pod
		else
		{
			float trigphase;
			if(!m_dataSocket->RecvLooped((uint8_t*)&trigphase, sizeof(trigphase)))
				return false;
			trigphase = -trigphase * fs_per_sample;
			if(!m_dataSocket->RecvLooped((uint8_t*)buf, memdepth * sizeof(int16_t)))
				return false;

			size_t podnum = chnum - m_analogChannelCount;

			//Now that we have the waveform data, unpack it into individual channels
			for(size_t j=0; j<8; j++)
			{
				//Bitmask for this digital channel
				int16_t mask = (1 << j);

				//Create the waveform
				DigitalWaveform* cap = new DigitalWaveform;
				cap->m_timescale = fs_per_sample;
				cap->m_triggerPhase = trigphase;
				cap->m_startTimestamp = time(NULL);
				cap->m_densePacked = false;
				cap->m_startFemtoseconds = fs;

				//Preallocate memory assuming no deduplication possible
				cap->Resize(memdepth);

				//First sample never gets deduplicated
				bool last = (buf[0] & mask) ? true : false;
				size_t k = 0;
				cap->m_offsets[0] = 0;
				cap->m_durations[0] = 1;
				cap->m_samples[0] = last;

				//Read and de-duplicate the other samples
				//TODO: can we vectorize this somehow?
				for(size_t m=1; m<memdepth; m++)
				{
					bool sample = (buf[m] & mask) ? true : false;

					//Deduplicate consecutive samples with same value
					//FIXME: temporary workaround for rendering bugs
					//if(last == sample)
					if( (last == sample) && ((m+3) < memdepth) )
						cap->m_durations[k] ++;

					//Nope, it toggled - store the new value
					else
					{
						k++;
						cap->m_offsets[k] = m;
						cap->m_durations[k] = 1;
						cap->m_samples[k] = sample;
						last = sample;
					}
				}

				//Free space reclaimed by deduplication
				cap->Resize(k);
				cap->m_offsets.shrink_to_fit();
				cap->m_durations.shrink_to_fit();
				cap->m_samples.shrink_to_fit();

				//Done
				s[m_channels[m_digitalChannelBase + 8*podnum + j] ] = cap;
			}

			delete[] buf;
		}
	}

	//Process analog captures in parallel
	#pragma omp parallel for
	for(size_t i=0; i<awfms.size(); i++)
	{
		auto cap = awfms[i];
		Convert16BitSamples(
			(int64_t*)&cap->m_offsets[0],
			(int64_t*)&cap->m_durations[0],
			(float*)&cap->m_samples[0],
			abufs[i],
			scales[i],
			-offsets[i],
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

void PicoOscilloscope::ForceTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("FORCE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
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

	//Don't allow setting trigger offset beyond the end of the capture
	int64_t captureDuration = GetSampleDepth() * FS_PER_SECOND / GetSampleRate();
	m_triggerOffset = min(offset, captureDuration);

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

	ClearPendingWaveforms();
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

vector<Oscilloscope::AnalogBank> PicoOscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> banks;
	banks.push_back(GetAnalogBank(0));
	return banks;
}

Oscilloscope::AnalogBank PicoOscilloscope::GetAnalogBank(size_t /*channel*/)
{
	AnalogBank bank;
	return bank;
}

bool PicoOscilloscope::IsADCModeConfigurable()
{
	switch(m_series)
	{
		case SERIES_6x0xE:
		case SERIES_6403E:
			return false;

		case SERIES_6x2xE:
			return true;

		default:
			LogWarning("PicoOscilloscope::IsADCModeConfigurable: unknown series\n");
			return false;
	}
}

vector<string> PicoOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	//All scopes with variable resolution start at 8 bit and go up from there
	vector<string> ret;
	ret.push_back("8 Bit");
	if(Is10BitModeAvailable())
	{
		ret.push_back("10 Bit");
		if(Is12BitModeAvailable())
			ret.push_back("12 Bit");
	}
	return ret;
}

size_t PicoOscilloscope::GetADCMode(size_t /*channel*/)
{
	return m_adcMode;
}

void PicoOscilloscope::SetADCMode(size_t /*channel*/, size_t mode)
{
	m_adcMode = (ADCMode)mode;

	lock_guard<recursive_mutex> lock(m_mutex);
	switch(mode)
	{
		case ADC_MODE_8BIT:
			m_transport->SendCommand("BITS 8");
			break;

		case ADC_MODE_10BIT:
			m_transport->SendCommand("BITS 10");
			break;

		case ADC_MODE_12BIT:
			m_transport->SendCommand("BITS 12");
			break;

		default:
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> PicoOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;
	for(size_t i=0; i<m_digitalChannelCount; i++)
	{
		DigitalBank bank;
		bank.push_back(GetChannel(m_digitalChannelBase + i));
		banks.push_back(bank);
	}
	return banks;
}

Oscilloscope::DigitalBank PicoOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank ret;
	ret.push_back(GetChannel(channel));
	return ret;
}

bool PicoOscilloscope::IsDigitalHysteresisConfigurable()
{
	return true;
}

bool PicoOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float PicoOscilloscope::GetDigitalHysteresis(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_digitalHysteresis[channel];
}

float PicoOscilloscope::GetDigitalThreshold(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_digitalThresholds[channel];
}

void PicoOscilloscope::SetDigitalHysteresis(size_t channel, float level)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_digitalHysteresis[channel] = level;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(GetChannel(channel)->GetHwname() + ":HYS " + to_string(level * 1000));
}

void PicoOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_digitalThresholds[channel] = level;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(GetChannel(channel)->GetHwname() + ":THRESH " + to_string(level));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checking for validity of configurations

/**
	@brief Returns the total number of analog channels which are currently enabled
 */
size_t PicoOscilloscope::GetEnabledAnalogChannelCount()
{
	size_t ret = 0;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(IsChannelEnabled(i))
			ret ++;
	}
	return ret;
}

/**
	@brief Returns the total number of 8-bit MSO pods which are currently enabled
 */
size_t PicoOscilloscope::GetEnabledDigitalPodCount()
{
	size_t n = 0;
	if(IsDigitalPodActive(0))
		n++;
	if(IsDigitalPodActive(1))
		n++;
	return n;
}

/**
	@brief Returns the total number of analog channels in the requested range which are currently enabled
 */
size_t PicoOscilloscope::GetEnabledAnalogChannelCountRange(size_t start, size_t end)
{
	if(end >= m_analogChannelCount)
		end = m_analogChannelCount - 1;

	size_t n = 0;
	for(size_t i = start; i <= end; i++)
	{
		if(IsChannelEnabled(i))
			n ++;
	}
	return n;
}

/**
	@brief Check if a MSO pod is present
 */
bool PicoOscilloscope::IsDigitalPodPresent(size_t npod)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_digitalBankPresent.find(npod) != m_digitalBankPresent.end())
			return m_digitalBankPresent[npod];
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(to_string(npod + 1) + "D:PRESENT?");
	int present = stoi(m_transport->ReadReply());

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	if(present)
	{
		m_digitalBankPresent[npod] = true;
		return true;
	}
	else
	{
		m_digitalBankPresent[npod] = false;
		return false;
	}
}

/**
	@brief Check if any channels in an MSO pod are enabled
 */
bool PicoOscilloscope::IsDigitalPodActive(size_t npod)
{
	size_t base = m_digitalChannelBase + 8*npod;
	for(size_t i=0; i<8; i++)
	{
		if(IsChannelEnabled(base+i))
			return true;
	}
	return false;
}

/**
	@brief Checks if a channel index refers to a MSO channel
 */
bool PicoOscilloscope::IsChannelIndexDigital(size_t i)
{
	return (i >= m_digitalChannelBase) && (i < m_digitalChannelBase + m_digitalChannelCount);
}

bool PicoOscilloscope::CanEnableChannel(size_t i)
{
	//If channel is already on, of course it can stay on
	if(IsChannelEnabled(i))
		return true;

	//Digital channels
	if(IsChannelIndexDigital(i))
	{
		size_t npod = GetDigitalPodIndex(i);

		//If the pod isn't here, we can't enable it
		if(!IsDigitalPodPresent(npod))
			return false;

		//If other channels in the pod are already active, we can enable them
		if(IsDigitalPodActive(npod))
			return true;
	}

	//Fall back to the main path if we get here
	switch(m_series)
	{
		//6000 series
		case SERIES_6403E:
		case SERIES_6x0xE:
		case SERIES_6x2xE:
			switch(GetADCMode(0))
			{
				case ADC_MODE_8BIT:
					return CanEnableChannel6000Series8Bit(i);

				case ADC_MODE_10BIT:
					return CanEnableChannel6000Series10Bit(i);

				case ADC_MODE_12BIT:
					return CanEnableChannel6000Series12Bit(i);

				default:
					break;
			}
		default:
			break;
	}

	//When in doubt, assume all channels are available
	LogWarning("PicoOscilloscope::CanEnableChannel: Unknown ADC mode\n");
	return true;
}

/**
	@brief Checks if we can enable a channel on a 6000 series scope configured for 8-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel6000Series8Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//5 Gsps is the most restrictive configuration.
	if(rate >= RATE_5GSPS)
	{
		//If we already have too many channels/MSO pods active, we're out of RAM bandwidth.
		if(EnabledChannelCount >= 2)
			return false;

		//6403E only allows *one* 5 Gsps channel
		else if(m_series == SERIES_6403E)
			return (EnabledChannelCount == 0);

		//No banking restrictions for MSO pods if we have enough memory bandwidth
		else if(IsChannelIndexDigital(i))
			return true;

		//On 8 channel scopes, we can use one channel from the left bank (ABCD) and one from the right (EFGH).
		else if(m_analogChannelCount == 8)
		{
			//Can enable a left bank channel if there's none in use
			if(i < 4)
				return (GetEnabledAnalogChannelCountAToD() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountEToH() == 0);
		}

		//On 4 channel scopes, we can use one channel from the left bank (AB) and one from the right (CD)
		else
		{
			//Can enable a left bank channel if there's none in use
			if(i < 2)
				return (GetEnabledAnalogChannelCountAToB() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountCToD() == 0);
		}
	}

	//2.5 Gsps allows more stuff
	else if(rate >= RATE_2P5GSPS)
	{
		//If we already have too many channels/MSO pods active, we're out of RAM bandwidth.
		if(EnabledChannelCount >= 4)
			return false;

		//No banking restrictions for MSO pods if we have enough memory bandwidth
		else if(IsChannelIndexDigital(i))
			return true;

		//6403E allows up to 2 channels, one AB and one CD
		else if(m_series == SERIES_6403E)
		{
			//Can enable a left bank channel if there's none in use
			if(i < 2)
				return (GetEnabledAnalogChannelCountAToB() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountCToD() == 0);
		}

		//8 channel scopes allow up to 4 channels but only one from A/B, C/D, E/F, G/H
		else if(m_analogChannelCount == 8)
		{
			if(i < 2)
				return (GetEnabledAnalogChannelCountAToB() == 0);
			else if(i < 4)
				return (GetEnabledAnalogChannelCountCToD() == 0);
			else if(i < 6)
				return (GetEnabledAnalogChannelCountEToF() == 0);
			else
				return (GetEnabledAnalogChannelCountGToH() == 0);
		}

		//On 4 channel scopes, we can run everything at 2.5 Gsps
		else
			return true;
	}

	//1.25 Gsps - just RAM bandwidth check
	else if( (rate >= RATE_1P25GSPS) && (EnabledChannelCount <= 7) )
		return true;

	//Slow enough that there's no capacity limits
	else
		return true;
}

/**
	@brief Checks if we can enable a channel on a 6000 series scope configured for 10-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel6000Series10Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//5 Gsps is only allowed on a single channel/pod
	if(rate >= RATE_5GSPS)
		return (EnabledChannelCount == 0);

	//2.5 Gsps is allowed up to two channels/pods
	else if(rate >= RATE_2P5GSPS)
	{
		//Out of bandwidth
		if(EnabledChannelCount >= 2)
			return false;

		//No banking restrictions on MSO pods
		else if(IsChannelIndexDigital(i))
			return true;

		//8 channel scopes require the two channels to be in separate banks
		else if(m_analogChannelCount == 8)
		{
			//Can enable a left bank channel if there's none in use
			if(i < 4)
				return (GetEnabledAnalogChannelCountAToD() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountEToH() == 0);
		}

		//No banking restrictions on 4 channel scopes
		else
			return true;
	}

	//1.25 Gsps is allowed up to 4 total channels/pods with no banking restrictions
	else if(rate >= RATE_1P25GSPS)
		return (EnabledChannelCount <= 3);

	//625 Msps allowed up to 8 total channels/pods with no banking restrictions
	else if(rate >= RATE_625MSPS)
		return (EnabledChannelCount <= 7);

	//Slow enough that there's no capacity limits
	else
		return true;
}

/**
	@brief Checks if we can enable a channel on a 6000 series scope configured for 12-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel6000Series12Bit(size_t i)
{
	int64_t rate = GetSampleRate();

	//Too many channels enabled?
	if(GetEnabledAnalogChannelCount() >= 2)
		return false;

	else if(rate > RATE_1P25GSPS)
		return false;

	//No banking restrictions on MSO pods
	else if(IsChannelIndexDigital(i))
		return true;

	else if(m_analogChannelCount == 8)
	{
		//Can enable a left bank channel if there's none in use
		if(i < 4)
			return (GetEnabledAnalogChannelCountAToD() == 0);

		//Can enable a right bank channel if there's none in use
		else
			return (GetEnabledAnalogChannelCountEToH() == 0);
	}

	else
	{
		//Can enable a left bank channel if there's none in use
		if(i < 2)
			return (GetEnabledAnalogChannelCountAToB() == 0);

		//Can enable a right bank channel if there's none in use
		else
			return (GetEnabledAnalogChannelCountCToD() == 0);
	}
}

bool PicoOscilloscope::Is10BitModeAvailable()
{
	//FlexRes only available on one series at the moment
	if(m_series != SERIES_6x2xE)
		return false;

	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//5 Gsps is easy, just a bandwidth cap
	if(rate >= RATE_5GSPS)
		return (EnabledChannelCount <= 1);

	//2.5 Gsps has banking restrictions on 8 channel scopes
	else if(rate >= RATE_2P5GSPS)
	{
		if(EnabledChannelCount > 2)
			return false;

		else if(m_analogChannelCount == 8)
		{
			if(GetEnabledAnalogChannelCountAToB() > 1)
				return false;
			else if(GetEnabledAnalogChannelCountCToD() > 1)
				return false;
			else if(GetEnabledAnalogChannelCountEToF() > 1)
				return false;
			else if(GetEnabledAnalogChannelCountGToH() > 1)
				return false;
			else
				return true;
		}

		else
			return true;
	}

	//1.25 Gsps and 625 Msps are just bandwidth caps
	else if(rate >= RATE_1P25GSPS)
		return (EnabledChannelCount <= 4);
	else if(rate >= RATE_625MSPS)
		return (EnabledChannelCount <= 8);

	//No capacity limits
	else
		return true;
}

bool PicoOscilloscope::Is12BitModeAvailable()
{
	//FlexRes only available on one series at the moment
	if(m_series != SERIES_6x2xE)
		return false;

	int64_t rate = GetSampleRate();

	//12 bit mode only available at 1.25 Gsps and below
	if(rate > RATE_1P25GSPS)
		return false;

	//1.25 Gsps and below have the same banking restrictions: at most one channel from the left and right half
	else
	{
		if(m_analogChannelCount == 8)
			return (GetEnabledAnalogChannelCountAToD() <= 1) && (GetEnabledAnalogChannelCountEToH() <= 1);
		else
			return (GetEnabledAnalogChannelCountAToB() <= 1) && (GetEnabledAnalogChannelCountCToD() <= 1);
	}
}
