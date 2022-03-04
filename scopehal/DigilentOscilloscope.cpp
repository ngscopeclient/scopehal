/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
#include "DigilentOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

DigilentOscilloscope::DigilentOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_triggerArmed(false)
{
	//Set up initial cache configuration as "not valid" and let it populate as we go

	IdentifyHardware();

	//Add analog channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = string("C") + to_string(i+1);

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
		SetChannelOffset(i, 0,  0);
		SetChannelVoltageRange(i, 0, 5);
	}

	/*
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
	*/
	//Set initial memory configuration.
	//100 Msps is highest rate supported on AD Pro
	SetSampleRate(100000000L);
	SetSampleDepth(65536);

	/*
	//Add the external trigger input
	m_extTrigChannel =
		new OscilloscopeChannel(this, "EX", OscilloscopeChannel::CHANNEL_TYPE_TRIGGER, "", 1, m_channels.size(), true);
	m_channels.push_back(m_extTrigChannel);
	m_extTrigChannel->SetDefaultDisplayName();
	*/

	//Set up the data plane socket
	auto csock = dynamic_cast<SCPISocketTransport*>(m_transport);
	if(!csock)
		LogFatal("DigilentOscilloscope expects a SCPISocketTransport\n");


	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(m_channels[0]));
	SetTrigger(trig);
	PushTrigger();
	SetTriggerOffset(0);

	//For now, assume control plane port is data plane +1
	LogDebug("Connecting to data plane socket\n");
	m_dataSocket = new Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	m_dataSocket->Connect(csock->GetHostname(), csock->GetPort() + 1);
	m_dataSocket->DisableNagle();
}

/**
	@brief Color the channels based on Digilent's standard color sequence (yellow-cyan-magenta-green)
 */
string DigilentOscilloscope::GetChannelColor(size_t i)
{
	switch(i % 4)
	{
		case 0:
			return "#ffd700";

		case 1:
			return "#00bfff";

		case 2:
			return "#ff00ff";

		case 3:
		default:
			return "#00ff00";
	}
}

void DigilentOscilloscope::IdentifyHardware()
{
	//MSO channel support is still pending
	m_digitalChannelCount = 0;

	//Ask the scope how many channels it has
	m_transport->SendCommand("CHANS?");
	m_analogChannelCount = stoi(m_transport->ReadReply());
}

DigilentOscilloscope::~DigilentOscilloscope()
{
	delete m_dataSocket;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int DigilentOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string DigilentOscilloscope::GetDriverNameInternal()
{
	return "digilent";
}


void DigilentOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

bool DigilentOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	//if(i == m_extTrigChannel->GetIndex())
	//	return false;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelsEnabled[i];
}

void DigilentOscilloscope::EnableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = true;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":ON");
}

void DigilentOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":OFF");
}

OscilloscopeChannel::CouplingType DigilentOscilloscope::GetChannelCoupling(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelCouplings[i];
}

vector<OscilloscopeChannel::CouplingType> DigilentOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	/*ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);*/
	return ret;
}

void DigilentOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	bool valid = true;
	switch(type)
	{
		/*case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommand(":" + m_channels[i]->GetHwname() + ":COUP AC1M");
			break;*/

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

double DigilentOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void DigilentOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	double oldAtten = m_channelAttenuations[i];
	m_channelAttenuations[i] = atten;

	//Rescale channel voltage range and offset (no change to hardware side)
	double delta = atten / oldAtten;
	m_channelVoltageRanges[i] *= delta;
	m_channelOffsets[i] *= delta;
}

int DigilentOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void DigilentOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

float DigilentOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRanges[i];
}

void DigilentOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
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

OscilloscopeChannel* DigilentOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

float DigilentOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffsets[i];
}

void DigilentOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
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

Oscilloscope::TriggerMode DigilentOscilloscope::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool DigilentOscilloscope::AcquireData()
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
	float trigphase;
	SequenceSet s;
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Analog channels get processed separately
	vector<double*> abufs;
	vector<float> scales;
	vector<AnalogWaveform*> awfms;
	for(size_t i=0; i<numChannels; i++)
	{
		//Get channel ID and memory depth (samples, not bytes)
		if(!m_dataSocket->RecvLooped((uint8_t*)&chnum, sizeof(chnum)))
			return false;
		if(!m_dataSocket->RecvLooped((uint8_t*)&memdepth, sizeof(memdepth)))
			return false;
		double* buf = new double[memdepth];

		//Analog channels
		if(chnum < m_analogChannelCount)
		{
			abufs.push_back(buf);

			if(!m_dataSocket->RecvLooped((uint8_t*)&trigphase, sizeof(trigphase)))
				return false;

			//TODO: stream timestamp from the server

			if(!m_dataSocket->RecvLooped((uint8_t*)buf, memdepth * sizeof(double)))
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
			scales.push_back(GetChannelAttenuation(chnum));

			s[m_channels[chnum]] = cap;
		}

		/*
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
			if(podnum > 2)
			{
				LogError("Digital pod number was >2 (chnum = %zu). Possible protocol desync or data corruption?\n",
					chnum);
				return false;
			}

			//Create buffers for output waveforms
			DigitalWaveform* caps[8];
			for(size_t j=0; j<8; j++)
			{
				caps[j] = new DigitalWaveform;
				s[m_channels[m_digitalChannelBase + 8*podnum + j] ] = caps[j];
			}

			//Now that we have the waveform data, unpack it into individual channels
			#pragma omp parallel for
			for(size_t j=0; j<8; j++)
			{
				//Bitmask for this digital channel
				int16_t mask = (1 << j);

				//Create the waveform
				auto cap = caps[j];
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
			}

			delete[] buf;
		}
		*/
	}

	//Process analog captures in parallel
	#pragma omp parallel for
	for(size_t i=0; i<awfms.size(); i++)
	{
		auto cap = awfms[i];

		float scale = scales[i];
		double* buf = abufs[i];
		for(size_t j=0; j<memdepth; j++)
		{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = buf[j] * scale;
		}

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

void DigilentOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("START");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void DigilentOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SINGLE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void DigilentOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");
	m_triggerArmed = false;
}

void DigilentOscilloscope::ForceTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("FORCE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool DigilentOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> DigilentOscilloscope::GetSampleRatesNonInterleaved()
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

vector<uint64_t> DigilentOscilloscope::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> DigilentOscilloscope::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> DigilentOscilloscope::GetSampleDepthsNonInterleaved()
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

vector<uint64_t> DigilentOscilloscope::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

uint64_t DigilentOscilloscope::GetSampleRate()
{
	return m_srate;
}

uint64_t DigilentOscilloscope::GetSampleDepth()
{
	return m_mdepth;
}

void DigilentOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(string("DEPTH ") + to_string(depth));
	m_mdepth = depth;
}

void DigilentOscilloscope::SetSampleRate(uint64_t rate)
{
	m_srate = rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand( string("RATE ") + to_string(rate));
}

void DigilentOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Don't allow setting trigger offset beyond the end of the capture
	int64_t captureDuration = GetSampleDepth() * FS_PER_SECOND / GetSampleRate();
	m_triggerOffset = min(offset, captureDuration);

	PushTrigger();
}

int64_t DigilentOscilloscope::GetTriggerOffset()
{
	return m_triggerOffset;
}

bool DigilentOscilloscope::IsInterleaving()
{
	//not supported
	return false;
}

bool DigilentOscilloscope::SetInterleaving(bool /*combine*/)
{
	//not supported
	return false;
}

void DigilentOscilloscope::PullTrigger()
{
	//pulling not needed, we always have a valid trigger cached
}

void DigilentOscilloscope::PushTrigger()
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

void DigilentOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Type
	m_transport->SendCommand(":TRIG:MODE EDGE");

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

vector<Oscilloscope::AnalogBank> DigilentOscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> banks;
	banks.push_back(GetAnalogBank(0));
	return banks;
}

Oscilloscope::AnalogBank DigilentOscilloscope::GetAnalogBank(size_t /*channel*/)
{
	AnalogBank bank;
	return bank;
}

bool DigilentOscilloscope::IsADCModeConfigurable()
{
	return false;
}

vector<string> DigilentOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> ret;
	return ret;
}

size_t DigilentOscilloscope::GetADCMode(size_t /*channel*/)
{
	return 0;
}

void DigilentOscilloscope::SetADCMode(size_t /*channel*/, size_t /*mode*/)
{
	//not supported
}

bool DigilentOscilloscope::CanEnableChannel(size_t /*channel*/)
{
	//all channels always available, no resource sharing
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> DigilentOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;
	return banks;
}

Oscilloscope::DigitalBank DigilentOscilloscope::GetDigitalBank(size_t /*channel*/)
{
	DigitalBank ret;
	return ret;
}

bool DigilentOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool DigilentOscilloscope::IsDigitalThresholdConfigurable()
{
	return false;
}

float DigilentOscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0;
}

float DigilentOscilloscope::GetDigitalThreshold(size_t /*channel*/)
{
	return 0;
}

void DigilentOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{

}

void DigilentOscilloscope::SetDigitalThreshold(size_t /*channel*/, float /*level*/)
{

}
