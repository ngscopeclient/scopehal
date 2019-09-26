/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
#include "AgilentOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AgilentOscilloscope::AgilentOscilloscope(SCPITransport* transport)
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

		// FIXME: ch4 should be pink & check other colours are correct
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

	//Configure transport format to raw 16-bit int, little endian
	//TODO: if instrument internal is big endian, skipping the bswap might improve download performance?
	//Might be faster to do it on a beefy x86 than the embedded side of things.
	m_transport->SendCommand(":WAV:FORM:DATA WORD");
	m_transport->SendCommand(":WAV:FORM:BYT LSBFirst");

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
		LogDebug("* %s (unknown)\n", opt.c_str());
	}
}

AgilentOscilloscope::~AgilentOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

unsigned int AgilentOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void AgilentOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelsEnabled.clear();
	m_triggerChannelValid = false;
	m_triggerLevelValid = false;
}

bool AgilentOscilloscope::IsChannelEnabled(size_t i)
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

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP?");
	string reply = m_transport->ReadReply();
	if(reply == "0")
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

void AgilentOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP ON");
}

void AgilentOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP OFF");
}

OscilloscopeChannel::CouplingType AgilentOscilloscope::GetChannelCoupling(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
	string coup_reply = m_transport->ReadReply();
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP?");
	string imp_reply = m_transport->ReadReply();

	if(coup_reply == "AC")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if(coup_reply == "DC")
	{
		if(imp_reply == "ONEM")
			return OscilloscopeChannel::COUPLE_DC_1M;
		else if(imp_reply == "FIFT")
			return OscilloscopeChannel::COUPLE_DC_50;
	}
}

void AgilentOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP FIFT");
			break;

		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP ONEM");
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP AC");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP ONEM");
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
			break;

		default:
			LogError("Invalid coupling for channel\n");
	}
}

double AgilentOscilloscope::GetChannelAttenuation(size_t i)
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");

	string reply = m_transport->ReadReply();
	double atten;
	sscanf(reply.c_str(), "%lf", &atten);
	return atten;
	*/
}

void AgilentOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	//FIXME
}

int AgilentOscilloscope::GetChannelBandwidthLimit(size_t i)
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
}

void AgilentOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	//FIXME
}

double AgilentOscilloscope::GetChannelVoltageRange(size_t i)
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

void AgilentOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	//FIXME
}

OscilloscopeChannel* AgilentOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

double AgilentOscilloscope::GetChannelOffset(size_t i)
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

void AgilentOscilloscope::SetChannelOffset(size_t i, double offset)
{
	//FIXME
}

void AgilentOscilloscope::ResetTriggerConditions()
{
	//FIXME
}

Oscilloscope::TriggerMode AgilentOscilloscope::PollTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(":TER?");
	string stat = m_transport->ReadReply();

	if(stat == "+1")
		return TRIGGER_MODE_RUN;
	else if( (stat == "STOP") || (stat == "BRE") )
		return TRIGGER_MODE_STOP;
	else if(stat == "+0")
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}
}

bool AgilentOscilloscope::AcquireData(bool toQueue)
{
	//LogDebug("Acquiring data\n");

	lock_guard<recursive_mutex> lock(m_mutex);
	LogIndenter li;

	int format;
	int type;
	size_t length;
	int average_count;
	double xincrement;
	double xorigin;
	double xreference;
	double yincrement;
	double yorigin;
	double yreference;
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


		// Set source & get preamble
		m_transport->SendCommand(":WAV:SOUR " + m_channels[i]->GetHwname());
		m_transport->SendCommand(":WAV:PRE?");
		string reply = m_transport->ReadReply();
		sscanf(reply.c_str(), "%u,%u,%lu,%u,%lf,%lf,%lf,%lf,%lf,%lf",
				&format, &type, &length, &average_count, &xincrement, &xorigin, &xreference, &yincrement, &yorigin, &yreference);

		//Figure out the sample rate
		int64_t ps_per_sample = round(xincrement * 1e12f);
		//LogDebug("%ld ps/sample\n", ps_per_sample);

		//LogDebug("length = %d\n", length);
		unsigned char* temp_buf = new unsigned char[length];

		//Set up the capture we're going to store our data into (no high res timer on R&S scopes)
		AnalogCapture* cap = new AnalogCapture;
		cap->m_timescale = ps_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startPicoseconds = (t - floor(t)) * 1e12f;

		//Ask for the data
		m_transport->SendCommand(":WAV:DATA?");

//		m_transport->ReadReply();
		//Read and discard the length header
		char tmp[16] = {0};
		m_transport->ReadRawData(2, (unsigned char*)tmp);
		int num_digits = atoi(tmp+1);
		//LogDebug("num_digits = %d", num_digits);
		m_transport->ReadRawData(num_digits, (unsigned char*)tmp);
		int actual_len = atoi(tmp);
		//LogDebug("actual_len = %d", actual_len);

		//Read the actual data
		m_transport->ReadRawData(length*sizeof(unsigned char), (unsigned char*)temp_buf);
		m_transport->ReadRawData(1, (unsigned char*)tmp);

		//Format the capture
		for(size_t i=0; i<length; i++)
			cap->m_samples.push_back(AnalogSample(i, 1, yincrement * ((float)temp_buf[i] - yreference)));

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
		m_transport->SendCommand(":SING");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

void AgilentOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void AgilentOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void AgilentOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

bool AgilentOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

size_t AgilentOscilloscope::GetTriggerChannelIndex()
{
	//Check cache
	//No locking, worst case we return a result a few seconds old
	if(m_triggerChannelValid)
		return m_triggerChannel;

	lock_guard<recursive_mutex> lock(m_mutex);

	//Look it up
	//m_transport->SendCommand("TRIG:A:SOUR?");
	//string ret = m_transport->ReadReply();

	m_triggerChannelValid = true;
	m_triggerChannel = 0;
	return m_triggerChannel;
	//This is a bit annoying because the hwname's used here are DIFFERENT than everywhere else!
	/*if(ret.find("CH") == 0)
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
	}*/
}

void AgilentOscilloscope::SetTriggerChannelIndex(size_t i)
{
	//FIXME
}

float AgilentOscilloscope::GetTriggerVoltage()
{
	//Check cache.
	//No locking, worst case we return a just-invalidated (but still fresh-ish) result.
	if(m_triggerLevelValid)
		return m_triggerLevel;

	lock_guard<recursive_mutex> lock(m_mutex);

	return 0.0f;

	m_transport->SendCommand("TRIG:A:LEV?");
	string ret = m_transport->ReadReply();

	double level;
	sscanf(ret.c_str(), "%lf", &level);
	m_triggerLevel = level;
	m_triggerLevelValid = true;
	return level;
}

void AgilentOscilloscope::SetTriggerVoltage(float v)
{
	//FIXME
}

Oscilloscope::TriggerType AgilentOscilloscope::GetTriggerType()
{
	//FIXME
	return Oscilloscope::TRIGGER_TYPE_RISING;
}

void AgilentOscilloscope::SetTriggerType(Oscilloscope::TriggerType type)
{
	//FIXME
}

void AgilentOscilloscope::SetTriggerForChannel(OscilloscopeChannel* /*channel*/, vector<TriggerType> /*triggerbits*/)
{
	//unimplemented, no LA support
}

vector<uint64_t> AgilentOscilloscope::GetSampleRatesNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> AgilentOscilloscope::GetSampleRatesInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> AgilentOscilloscope::GetInterleaveConflicts()
{
	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> AgilentOscilloscope::GetSampleDepthsNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> AgilentOscilloscope::GetSampleDepthsInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}
