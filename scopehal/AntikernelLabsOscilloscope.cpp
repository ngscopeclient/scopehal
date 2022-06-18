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

#include "scopehal.h"
#include "AntikernelLabsOscilloscope.h"
#include "SCPISocketTransport.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AntikernelLabsOscilloscope::AntikernelLabsOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	/*
	, m_triggerArmed(false)
	, m_triggerOneShot(false)*/
{
	//Create the data-plane socket for our waveforms
	auto socktrans = dynamic_cast<SCPISocketTransport*>(transport);
	if(!socktrans)
		LogFatal("Antikernel Labs oscilloscopes only support SCPISocketTransport\n");
	m_waveformTransport = new SCPISocketTransport(socktrans->GetHostname() + ":50101");

	//Last digit of the model number is the number of channels
	//int model_number = atoi(m_model.c_str() + 3);	//FIXME: are all series IDs 3 chars e.g. "RTM"?
	//int nchans = model_number % 10;

	int nchans = 1;

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("C1");
		chname[1] += i;

		//Color the channels based on Antikernel Labs's color sequence
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#ffff80";
				break;

			case 1:
				color = "#ff8080";
				break;

			case 2:
				color = "#80ffff";
				break;

			case 3:
				color = "#80ff80";
				break;

			//TODO: colors for the other 4 channels
		}

		//Create the channel
		m_channels.push_back(
			new OscilloscopeChannel(
			this,
			chname,
			color,
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_ANALOG,
			i));

		//Request all points when we download
		//m_transport->SendCommand(chname + ":DATA:POIN MAX");
	}
	m_analogChannelCount = nchans;

	/*
	//Add the external trigger input
	m_extTrigChannel = new OscilloscopeChannel(
		this,
		"EX",
		Stream::STREAM_TYPE_TRIGGER,
		"",
		m_channels.size(),
		true);
	m_channels.push_back(m_extTrigChannel);
	*/
}

AntikernelLabsOscilloscope::~AntikernelLabsOscilloscope()
{
	delete m_waveformTransport;
	m_waveformTransport = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string AntikernelLabsOscilloscope::GetDriverNameInternal()
{
	return "aklabs";
}

unsigned int AntikernelLabsOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void AntikernelLabsOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	/*
	m_channelsEnabled.clear();
	*/
}

bool AntikernelLabsOscilloscope::IsChannelEnabled(size_t /*i*/)
{
	/*
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//TODO: handle digital channels, for now just claim they're off
	if(i >= m_analogChannelCount)
		return false;

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
		return m_channelsEnabled[i];

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":STAT?");
	string reply = m_transport->ReadReply();
	if(reply == "OFF")
	{
		m_channelsEnabled[i] = false;
		return false;
	}
	else
	{
		m_channelsEnabled[i] = true;
		return true;
	}
	*/
	return true;
}

void AntikernelLabsOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":EN");
}

void AntikernelLabsOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DIS");
}

vector<OscilloscopeChannel::CouplingType> AntikernelLabsOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	return ret;
}

OscilloscopeChannel::CouplingType AntikernelLabsOscilloscope::GetChannelCoupling(size_t /*i*/)
{
	//All channels are 50 ohm all the time
	return OscilloscopeChannel::COUPLE_DC_50;
}

void AntikernelLabsOscilloscope::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//No-op, all channels are 50 ohm all the time
}

double AntikernelLabsOscilloscope::GetChannelAttenuation(size_t /*i*/)
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");

	string reply = m_transport->ReadReply();
	double atten;
	sscanf(reply.c_str(), "%lf", &atten);
	return atten;
	*/

	LogWarning("AntikernelLabsOscilloscope::GetChannelAttenuation unimplemented\n");
	return 1;
}

void AntikernelLabsOscilloscope::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//FIXME

	LogWarning("AntikernelLabsOscilloscope::SetChannelAttenuation unimplemented\n");
}

int AntikernelLabsOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL?");
	string reply = m_transport->ReadReply();
	if(reply == "20M")
		return 20;
	else
		return 0;
	*/

	LogWarning("AntikernelLabsOscilloscope::GetChannelBandwidthLimit unimplemented\n");
	return 0;
}

void AntikernelLabsOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	LogWarning("AntikernelLabsOscilloscope::SetChannelBandwidthLimit unimplemented\n");
}

float AntikernelLabsOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":GAIN?");
	string reply = m_transport->ReadReply();

	/*
		Current firmware reports the gain of the VGA, not overall system gain (this will change eventually).
		We have a -6 dB fixed attenuator before the VGA then 2 dB of fixed gain after it - so subtract 4 dB.
	*/
	int db;
	sscanf(reply.c_str(), "%d", &db);
	db -= 4;

	float frac_gain = pow(10, db / 20.0f);
	float vfs = 2.0 / frac_gain;

	//LogDebug("Channel gain is %d dB (%.2f V/V, Vfs = %.3f)\n", db, frac_gain, vfs);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = vfs;
	return vfs;
}

void AntikernelLabsOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	//Convert requested range to gain
	float frac_gain = 2.0 / range;
	float db = 20*log10(frac_gain) + 4;
	//LogDebug("Setting channel gain to %.2f dB (rounding to %.0f)\n", db, round(db));

	//Clip gain to the supported range
	if(db < -9)
	{
		db = -9;
		range = 2.0 / pow(10, (db-4)/20.0f);
	}
	else if(db > 26)
	{
		db = 26;
		range = 2.0 / pow(10, (db-4)/20.0f);
	}

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%s:GAIN %d", m_channels[i]->GetHwname().c_str(), (int)round(db));

	lock_guard<recursive_mutex> lock2(m_mutex);
	m_transport->SendCommand(tmp);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
}

OscilloscopeChannel* AntikernelLabsOscilloscope::GetExternalTrigger()
{
	//FIXME
	LogWarning("AntikernelLabsOscilloscope::GetExternalTrigger unimplemented\n");
	return NULL;
}

float AntikernelLabsOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFS?");

	string reply = m_transport->ReadReply();
	float offset;
	sscanf(reply.c_str(), "%f", &offset);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void AntikernelLabsOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%s:OFFS %f", m_channels[i]->GetHwname().c_str(), offset);
	m_transport->SendCommand(tmp);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

Oscilloscope::TriggerMode AntikernelLabsOscilloscope::PollTrigger()
{
	//Always report "triggered" for now, since waveforms come nonstop.
	//TODO: API needs to have a better way to handle push-based workflows
	return TRIGGER_MODE_TRIGGERED;
}

bool AntikernelLabsOscilloscope::AcquireData()
{
	//Read the waveform data
	const int depth = 16384;
	static uint8_t waveform[depth];
	m_waveformTransport->ReadRawData(depth, waveform);

	//LogDebug("Got waveform\n");
	LogIndenter li;

	//1600 ps per sample for now, hard coded
	AnalogWaveform* cap = new AnalogWaveform;
	cap->m_timescale = 1600;
	cap->m_triggerPhase = 0;
	double t = GetTime();
	cap->m_startTimestamp = floor(t);
	cap->m_startFemtoseconds = (t - cap->m_startTimestamp) * FS_PER_SECOND;

	//Process the samples
	float fullscale = GetChannelVoltageRange(0, 0);
	float scale = fullscale / 256.0f;
	float offset = GetChannelOffset(0, 0);
	cap->Resize(depth);
	for(size_t i=0; i<depth; i++)
	{
		cap->m_offsets[i] = i;
		cap->m_durations[i] = 1;
		cap->m_samples[i] = ((waveform[i] - 128.0f) * scale) + offset;
	}

	//See what the actual voltages are at the zero crossing
	//TODO: this isn't the actual trigger point??
	float vtrig = 0;
	float trigfrac = Filter::InterpolateTime(cap, 57, vtrig);
	cap->m_triggerPhase = -trigfrac * cap->m_timescale;

	//Done, update
	lock_guard<recursive_mutex> lock(m_mutex);
	map<int, vector<AnalogWaveform*> > pending_waveforms;
	pending_waveforms[0].push_back(cap);

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	//single segment only for now
	for(size_t i=0; i<num_pending; i++)
	{
		SequenceSet s;
		for(size_t j=0; j<m_analogChannelCount; j++)
		{
			if(IsChannelEnabled(j))
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	return true;
}

void AntikernelLabsOscilloscope::Start()
{
	//Arm the trigger using the current awful hack (sending literally anything)
	m_waveformTransport->SendCommand("ohai");

	/*
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = false;
	*/
}

void AntikernelLabsOscilloscope::StartSingleTrigger()
{
	//Arm the trigger using the current awful hack (sending literally anything)
	m_waveformTransport->SendCommand("ohai");

	/*
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = true;
	*/
}

void AntikernelLabsOscilloscope::Stop()
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
	*/
}

void AntikernelLabsOscilloscope::ForceTrigger()
{
	//TODO
}

bool AntikernelLabsOscilloscope::IsTriggerArmed()
{
	//return m_triggerArmed;
	return true;
}

vector<uint64_t> AntikernelLabsOscilloscope::GetSampleRatesNonInterleaved()
{
	LogWarning("AntikernelLabsOscilloscope::GetSampleRatesNonInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> AntikernelLabsOscilloscope::GetSampleRatesInterleaved()
{
	LogWarning("AntikernelLabsOscilloscope::GetSampleRatesInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> AntikernelLabsOscilloscope::GetInterleaveConflicts()
{
	LogWarning("AntikernelLabsOscilloscope::GetInterleaveConflicts unimplemented\n");

	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> AntikernelLabsOscilloscope::GetSampleDepthsNonInterleaved()
{
	LogWarning("AntikernelLabsOscilloscope::GetSampleDepthsNonInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> AntikernelLabsOscilloscope::GetSampleDepthsInterleaved()
{
	LogWarning("AntikernelLabsOscilloscope::GetSampleDepthsInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}

uint64_t AntikernelLabsOscilloscope::GetSampleRate()
{
	//FIXME
	return 625000000L;
}

uint64_t AntikernelLabsOscilloscope::GetSampleDepth()
{
	//FIXME
	return 16384;
}

void AntikernelLabsOscilloscope::SetSampleDepth(uint64_t /*depth*/)
{
	//FIXME
}

void AntikernelLabsOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//FIXME
}

void AntikernelLabsOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t AntikernelLabsOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}

bool AntikernelLabsOscilloscope::IsInterleaving()
{
	return false;
}

bool AntikernelLabsOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void AntikernelLabsOscilloscope::PullTrigger()
{
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

	//Default setup
	et->SetInput(0, StreamDescriptor(m_channels[0], 0), true);
	et->SetLevel(0.5);
	et->SetType(EdgeTrigger::EDGE_RISING);
}

void AntikernelLabsOscilloscope::PushTrigger()
{
	//no-op for now
}
