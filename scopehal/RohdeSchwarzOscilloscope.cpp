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

#include "scopehal.h"
#include "RohdeSchwarzOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RohdeSchwarzOscilloscope::RohdeSchwarzOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_triggerChannelValid(false)
	, m_triggerLevelValid(false)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
{
	//Last digit of the model number is the number of channels
	int model_number = atoi(m_model.c_str() + 3);	//FIXME: are all series IDs 3 chars e.g. "RTM"?
	int nchans = model_number % 10;

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN1");
		chname[4] += i;

		//Color the channels based on R&S's standard color sequence (yellow-green-orange-bluegray)
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#ffff00";
				break;

			case 1:
				color = "#00ff00";
				break;

			case 2:
				color = "#ff8000";
				break;

			case 3:
				color = "#8080ff";
				break;
		}

		//Create the channel
		m_channels.push_back(
			new OscilloscopeChannel(
			this,
			chname,
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			color,
			1,
			i,
			true));

		//Request all points when we download
		m_transport->SendCommand(chname + ":DATA:POIN MAX");
	}
	m_analogChannelCount = nchans;

	//Add the external trigger input
	m_extTrigChannel = new OscilloscopeChannel(
		this,
		"EX",
		OscilloscopeChannel::CHANNEL_TYPE_TRIGGER,
		"",
		1,
		m_channels.size(),
		true);
	m_channels.push_back(m_extTrigChannel);

	//Configure transport format to raw IEEE754 float, little endian
	//TODO: if instrument internal is big endian, skipping the bswap might improve download performance?
	//Might be faster to do it on a beefy x86 than the embedded side of things.
	m_transport->SendCommand("FORM:DATA REAL");
	m_transport->SendCommand("FORM:BORD LSBFirst");

	//See what options we have
	m_transport->SendCommand("*OPT?");
	string reply = m_transport->ReadReply();
	vector<string> options;
	string opt;
	for(unsigned int i=0; i<reply.length(); i++)
	{
		if(reply[i] == 0)
		{
			options.push_back(opt);
			break;
		}

		else if(reply[i] == ',')
		{
			options.push_back(opt);
			opt = "";
		}

		else
			opt += reply[i];
	}
	if(opt != "")
		options.push_back(opt);

	//Print out the option list and do processing for each
	LogDebug("Installed options:\n");
	if(options.empty())
		LogDebug("* None\n");
	for(auto opt : options)
	{
		if(opt == "B243")
			LogDebug("* 350 MHz bandwidth upgrade for RTM3004\n");
		else
			LogDebug("* %s (unknown)\n", opt.c_str());
	}
}

RohdeSchwarzOscilloscope::~RohdeSchwarzOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string RohdeSchwarzOscilloscope::GetDriverNameInternal()
{
	return "rs";
}

unsigned int RohdeSchwarzOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void RohdeSchwarzOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelsEnabled.clear();
	m_triggerChannelValid = false;
	m_triggerLevelValid = false;
}

bool RohdeSchwarzOscilloscope::IsChannelEnabled(size_t i)
{
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
}

void RohdeSchwarzOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":STAT ON");
}

void RohdeSchwarzOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":STAT OFF");
}

OscilloscopeChannel::CouplingType RohdeSchwarzOscilloscope::GetChannelCoupling(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
	string reply = m_transport->ReadReply();

	if(reply == "ACLimit")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if(reply == "DCLimit")
		return OscilloscopeChannel::COUPLE_DC_1M;
	else if(reply == "GND")
		return OscilloscopeChannel::COUPLE_GND;
	else if(reply == "DC")
		return OscilloscopeChannel::COUPLE_DC_50;

	else
	{
		LogWarning("invalid coupling value\n");
		return OscilloscopeChannel::COUPLE_DC_50;
	}
}

void RohdeSchwarzOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
			break;

		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP ACLimit");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DCLimit");
			break;

		case OscilloscopeChannel::COUPLE_GND:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP GND");
			break;

		default:
			LogError("Invalid coupling for channel\n");
	}
}

double RohdeSchwarzOscilloscope::GetChannelAttenuation(size_t /*i*/)
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");

	string reply = m_transport->ReadReply();
	double atten;
	sscanf(reply.c_str(), "%lf", &atten);
	return atten;
	*/

	LogWarning("RohdeSchwarzOscilloscope::GetChannelAttenuation unimplemented\n");
	return 1;
}

void RohdeSchwarzOscilloscope::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//FIXME

	LogWarning("RohdeSchwarzOscilloscope::SetChannelAttenuation unimplemented\n");
}

int RohdeSchwarzOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
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

	LogWarning("RohdeSchwarzOscilloscope::GetChannelBandwidthLimit unimplemented\n");
	return 0;
}

void RohdeSchwarzOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	LogWarning("RohdeSchwarzOscilloscope::SetChannelBandwidthLimit unimplemented\n");
}

double RohdeSchwarzOscilloscope::GetChannelVoltageRange(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":RANGE?");

	string reply = m_transport->ReadReply();
	double range;
	sscanf(reply.c_str(), "%lf", &range);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void RohdeSchwarzOscilloscope::SetChannelVoltageRange(size_t /*i*/, double /*range*/)
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::SetChannelVoltageRange unimplemented\n");
}

OscilloscopeChannel* RohdeSchwarzOscilloscope::GetExternalTrigger()
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::GetExternalTrigger unimplemented\n");
	return NULL;
}

double RohdeSchwarzOscilloscope::GetChannelOffset(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFS?");

	string reply = m_transport->ReadReply();
	double offset;
	sscanf(reply.c_str(), "%lf", &offset);
	offset = -offset;
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void RohdeSchwarzOscilloscope::SetChannelOffset(size_t /*i*/, double /*offset*/)
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::SetChannelOffset unimplemented\n");
}

void RohdeSchwarzOscilloscope::ResetTriggerConditions()
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::ResetTriggerConditions unimplemented\n");
}

Oscilloscope::TriggerMode RohdeSchwarzOscilloscope::PollTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("ACQ:STAT?");
	string stat = m_transport->ReadReply();

	if(stat == "RUN")
		return TRIGGER_MODE_RUN;
	else if( (stat == "STOP") || (stat == "BRE") )
		return TRIGGER_MODE_STOP;
	else if(stat == "COMP")
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}
}

bool RohdeSchwarzOscilloscope::AcquireData(bool toQueue)
{
	//LogDebug("Acquiring data\n");

	lock_guard<recursive_mutex> lock(m_mutex);
	LogIndenter li;

	double xstart;
	double xstop;
	size_t length;
	int ignored;
	map<int, vector<AnalogCapture*> > pending_waveforms;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
		{
			if(!toQueue)
				m_channels[i]->SetData(NULL);
			continue;
		}

		//This is basically the same function as a LeCroy WAVEDESC, but much less detailed
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":DATA:HEAD?");
		string reply = m_transport->ReadReply();
		sscanf(reply.c_str(), "%lf,%lf,%zu,%d", &xstart, &xstop, &length, &ignored);

		//Figure out the sample rate
		double capture_len_sec = xstop - xstart;
		double sec_per_sample = capture_len_sec / length;
		int64_t ps_per_sample = round(sec_per_sample * 1e12f);
		//LogDebug("%ld ps/sample\n", ps_per_sample);

		float* temp_buf = new float[length];

		//Set up the capture we're going to store our data into (no high res timer on R&S scopes)
		AnalogCapture* cap = new AnalogCapture;
		cap->m_timescale = ps_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startPicoseconds = (t - floor(t)) * 1e12f;

		//Ask for the data
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":DATA?");

		//Read and discard the length header
		char tmp[16] = {0};
		m_transport->ReadRawData(2, (unsigned char*)tmp);
		int num_digits = atoi(tmp+1);
		m_transport->ReadRawData(num_digits, (unsigned char*)tmp);
		int actual_len = atoi(tmp);

		//Read the actual data
		m_transport->ReadRawData(length*sizeof(float), (unsigned char*)temp_buf);

		//Format the capture
		for(size_t i=0; i<length; i++)
			cap->m_samples.push_back(AnalogSample(i, 1, temp_buf[i]));

		//Done, update the data
		if(!toQueue)
			m_channels[i]->SetData(cap);
		else
			pending_waveforms[i].push_back(cap);

		//Clean up
		delete[] temp_buf;
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 0;
	if(toQueue)				//if saving to queue, the 0'th segment counts too
		num_pending ++;
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

	//TODO: support digital channels

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		m_transport->SendCommand("SING");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

void RohdeSchwarzOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RohdeSchwarzOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RohdeSchwarzOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

bool RohdeSchwarzOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

size_t RohdeSchwarzOscilloscope::GetTriggerChannelIndex()
{
	//Check cache
	//No locking, worst case we return a result a few seconds old
	if(m_triggerChannelValid)
		return m_triggerChannel;

	lock_guard<recursive_mutex> lock(m_mutex);

	//Look it up
	m_transport->SendCommand("TRIG:A:SOUR?");
	string ret = m_transport->ReadReply();

	//This is a bit annoying because the hwname's used here are DIFFERENT than everywhere else!
	if(ret.find("CH") == 0)
	{
		m_triggerChannelValid = true;
		m_triggerChannel = atoi(ret.c_str()+2) - 1;
		return m_triggerChannel;
	}
	else if(ret == "EXT")
	{
		m_triggerChannelValid = true;
		m_triggerChannel = m_extTrigChannel->GetIndex();
		return m_triggerChannel;
	}
	else
	{
		m_triggerChannelValid = false;
		LogWarning("Unknown trigger source %s\n", ret.c_str());
		return 0;
	}
}

void RohdeSchwarzOscilloscope::SetTriggerChannelIndex(size_t /*i*/)
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::SetTriggerChannelIndex unimplemented\n");
}

float RohdeSchwarzOscilloscope::GetTriggerVoltage()
{
	//Check cache.
	//No locking, worst case we return a just-invalidated (but still fresh-ish) result.
	if(m_triggerLevelValid)
		return m_triggerLevel;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("TRIG:A:LEV?");
	string ret = m_transport->ReadReply();

	double level;
	sscanf(ret.c_str(), "%lf", &level);
	m_triggerLevel = level;
	m_triggerLevelValid = true;
	return level;
}

void RohdeSchwarzOscilloscope::SetTriggerVoltage(float /*v*/)
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::SetTriggerVoltage unimplemented\n");
}

Oscilloscope::TriggerType RohdeSchwarzOscilloscope::GetTriggerType()
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::GetTriggerType unimplemented\n");
	return Oscilloscope::TRIGGER_TYPE_RISING;
}

void RohdeSchwarzOscilloscope::SetTriggerType(Oscilloscope::TriggerType /*type*/)
{
	//FIXME
	LogWarning("RohdeSchwarzOscilloscope::SetTriggerType unimplemented\n");
}

void RohdeSchwarzOscilloscope::SetTriggerForChannel(OscilloscopeChannel* /*channel*/, vector<TriggerType> /*triggerbits*/)
{
	//unimplemented, no LA support
	LogWarning("RohdeSchwarzOscilloscope::SetTriggerForChannel unimplemented\n");
}

vector<uint64_t> RohdeSchwarzOscilloscope::GetSampleRatesNonInterleaved()
{
	LogWarning("RohdeSchwarzOscilloscope::GetSampleRatesNonInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> RohdeSchwarzOscilloscope::GetSampleRatesInterleaved()
{
	LogWarning("RohdeSchwarzOscilloscope::GetSampleRatesInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> RohdeSchwarzOscilloscope::GetInterleaveConflicts()
{
	LogWarning("RohdeSchwarzOscilloscope::GetInterleaveConflicts unimplemented\n");

	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> RohdeSchwarzOscilloscope::GetSampleDepthsNonInterleaved()
{
	LogWarning("RohdeSchwarzOscilloscope::GetSampleDepthsNonInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> RohdeSchwarzOscilloscope::GetSampleDepthsInterleaved()
{
	LogWarning("RohdeSchwarzOscilloscope::GetSampleDepthsInterleaved unimplemented\n");

	//FIXME
	vector<uint64_t> ret;
	return ret;
}
