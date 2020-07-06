/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg, Mike Walters                                                            *
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
#include "TektronixOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TektronixOscilloscope::TektronixOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_triggerChannelValid(false)
	, m_triggerLevelValid(false)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
{
	/*
	//Last digit of the model number is the number of channels
	std::string model_number = m_model;
	model_number.erase(
		std::remove_if(
			model_number.begin(),
			model_number.end(),
			[]( char const& c ) -> bool { return !std::isdigit(c); }
		),
		model_number.end()
	);
	int nchans = std::stoi(model_number) % 10;
	*/
	int nchans = 4;

	// No header in the reply of queries
	m_transport->SendCommand("HEAD 0");

	// 8-bit signed data
	m_transport->SendCommand("DATA:ENC RIB;WID 1");

	m_transport->SendCommand("DATA:SOURCE CH1, CH2, CH3, CH4;START 0; STOP 100000");

	// FIXME: where to put this?
	m_transport->SendCommand("ACQ:STOPA SEQ;REPE 1");

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CH1");
		chname[2] += i; // FIXME

		//Color the channels based on Tektronix's standard color sequence (yellow-green-violet-pink)
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#ffff00";
				break;

			case 1:
				color = "#32ff00";
				break;

			case 2:
				color = "#5578ff";
				break;

			case 3:
				color = "#ff0084";
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
		//m_transport->SendCommand(":WAV:POIN:MODE RAW");
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

	//See what options we have
	m_transport->SendCommand("*OPT?");
	string reply = m_transport->ReadReply();

	vector<string> options;

	for (std::string::size_type prev_pos=0, pos=0;
		 (pos = reply.find(',', pos)) != std::string::npos;
		 prev_pos=++pos)
	{
		std::string opt( reply.substr(prev_pos, pos-prev_pos) );
		if (opt == "0")
			continue;
		if (opt.substr(opt.length()-3, 3) == "(d)")
			opt.erase(opt.length()-3);

		options.push_back(opt);
	}

	//Print out the option list and do processing for each
	LogDebug("Installed options:\n");
	if(options.empty())
		LogDebug("* None\n");
	for(auto opt : options)
	{
		LogDebug("* %s (unknown)\n", opt.c_str());
	}
}

TektronixOscilloscope::~TektronixOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TektronixOscilloscope::GetDriverNameInternal()
{
	return "tektronix";
}

unsigned int TektronixOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void TektronixOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelCouplings.clear();
	m_channelsEnabled.clear();
	m_triggerChannelValid = false;
	m_triggerLevelValid = false;
}

bool TektronixOscilloscope::IsChannelEnabled(size_t i)
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

//	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP?");
//	string reply = m_transport->ReadReply();
//
	string reply;
	reply = "1";
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

void TektronixOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
//	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP ON");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;
}

void TektronixOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
//	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP OFF");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = false;
}

OscilloscopeChannel::CouplingType TektronixOscilloscope::GetChannelCoupling(size_t i)
{
	OscilloscopeChannel::CouplingType coupling;

	// FIXME
	coupling = OscilloscopeChannel::COUPLE_DC_1M;
	m_channelCouplings[i] = coupling;
	return coupling;

#if 0
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}
	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
	string coup_reply = m_transport->ReadReply();
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP?");
	string imp_reply = m_transport->ReadReply();

	OscilloscopeChannel::CouplingType coupling;
	if(coup_reply == "AC")
		coupling = OscilloscopeChannel::COUPLE_AC_1M;
	else /*if(coup_reply == "DC")*/
	{
		if(imp_reply == "ONEM")
			coupling = OscilloscopeChannel::COUPLE_DC_1M;
		else /*if(imp_reply == "FIFT")*/
			coupling = OscilloscopeChannel::COUPLE_DC_50;
	}
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = coupling;
	return coupling;
#endif
}

void TektronixOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
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

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelCouplings[i] = type;
}

double TektronixOscilloscope::GetChannelAttenuation(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	// FIXME
	return 1.0;

#if 0
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");

	string reply = m_transport->ReadReply();
	double atten;
	sscanf(reply.c_str(), "%lf", &atten);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelAttenuations[i] = atten;
	return atten;
#endif
}

void TektronixOscilloscope::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//FIXME
}

int TektronixOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	int bwl = 0;
	m_channelBandwidthLimits[i] = bwl;
	return bwl;

#if 0
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL?");
	string reply = m_transport->ReadReply();
	int bwl;
	if(reply == "1")
		bwl = 25;
	else
		bwl = 0;

	m_channelBandwidthLimits[i] = bwl;
	return bwl;
#endif
}

void TektronixOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//FIXME
}

double TektronixOscilloscope::GetChannelVoltageRange(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	// FIXME
	return 8;

#if 0
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":RANGE?");

	string reply = m_transport->ReadReply();
	double range;
	sscanf(reply.c_str(), "%lf", &range);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
#endif
}

void TektronixOscilloscope::SetChannelVoltageRange(size_t /*i*/, double /*range*/)
{
	//FIXME
}

OscilloscopeChannel* TektronixOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

double TektronixOscilloscope::GetChannelOffset(size_t i)
{
	double offset = 0;
	m_channelOffsets[i] = offset;
	return offset;

#if 0
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
#endif
}

void TektronixOscilloscope::SetChannelOffset(size_t /*i*/, double /*offset*/)
{
	//FIXME
}

void TektronixOscilloscope::ResetTriggerConditions()
{
	//FIXME
}

Oscilloscope::TriggerMode TektronixOscilloscope::PollTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	// Based on example from 6000 Series Programmer's Guide
	// Section 10 'Synchronizing Acquisitions' -> 'Polling Synchronization With Timeout'
	m_transport->SendCommand("TRIG:STATE?");
	string ter = m_transport->ReadReply();

	if(ter == "SAV")
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	if(ter != "REA")
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//TODO: how to handle auto / normal trigger mode?
	return TRIGGER_MODE_RUN;
}

bool TektronixOscilloscope::AcquireData(bool toQueue)
{
	//LogDebug("Acquiring data\n");

	lock_guard<recursive_mutex> lock(m_mutex);
	LogIndenter li;

	unsigned int format;
	unsigned int type;
	size_t length;
	unsigned int average_count;
	double xincrement;
	double xorigin;
	double xreference;
	double yincrement;
	double yorigin;
	double yreference;
	map<int, vector<AnalogWaveform*> > pending_waveforms;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
		{
			if(!toQueue)
				m_channels[i]->SetData(NULL);
			continue;
		}

		// Set source & get preamble
		m_transport->SendCommand("DATA:SOURCE " + m_channels[i]->GetHwname());
		m_transport->SendCommand("WFMPRE:" + m_channels[i]->GetHwname() + "?");

//		string reply = m_transport->ReadReply();
//		sscanf(reply.c_str(), "%u,%u,%lu,%u,%lf,%lf,%lf,%lf,%lf,%lf",
//				&format, &type, &length, &average_count, &xincrement, &xorigin, &xreference, &yincrement, &yorigin, &yreference);

		for(int j=0;j<10;++j)
			m_transport->ReadReply();

		format = 0;
		type = 0;
		average_count = 0;
		xincrement = 1000;
		xorigin = 0;
		xreference = 0;
		yincrement = 0.01;
		yorigin = 0;
		yreference = 0;
		length = 500;

		//Figure out the sample rate
		int64_t ps_per_sample = round(xincrement * 1e12f);
		//LogDebug("%ld ps/sample\n", ps_per_sample);

		//LogDebug("length = %d\n", length);

		//Set up the capture we're going to store our data into
		//(no TDC data available on Tektronix scopes?)
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = ps_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startPicoseconds = (t - floor(t)) * 1e12f;

		//Ask for the data
		m_transport->SendCommand("CURV?");

		char tmp[16] = {0};

		//Read the length header
		m_transport->ReadRawData(2, (unsigned char*)tmp);
		tmp[2] = '\0';
		int numDigits = atoi(tmp+1);
		LogDebug("numDigits = %d", numDigits);

		// Read the number of points
		m_transport->ReadRawData(numDigits, (unsigned char*)tmp);
		tmp[numDigits] = '\0';
		int numPoints = atoi(tmp);
		LogDebug("numPoints = %d", numPoints);

		uint8_t* temp_buf = new uint8_t[numPoints / sizeof(uint8_t)];

		//Read the actual data
		m_transport->ReadRawData(numPoints, (unsigned char*)temp_buf);
		//Discard trailing newline
		m_transport->ReadRawData(1, (unsigned char*)tmp);

		//Format the capture
		cap->Resize(length);
		for(size_t j=0; j<length; j++)
		{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = yincrement * (temp_buf[j] - yreference) + yorigin;
		}

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
		m_transport->SendCommand("ACQ:STATE ON");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

void TektronixOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

bool TektronixOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

size_t TektronixOscilloscope::GetTriggerChannelIndex()
{
	m_triggerChannelValid = true;
	m_triggerChannel = 1;
	return m_triggerChannel;

#if 0
	//Check cache
	//No locking, worst case we return a result a few seconds old
	if(m_triggerChannelValid)
		return m_triggerChannel;

	lock_guard<recursive_mutex> lock(m_mutex);

	//Look it up
	m_transport->SendCommand("TRIG:SOUR?");
	string ret = m_transport->ReadReply();

	if(ret.find("CHAN") == 0)
	{
		m_triggerChannelValid = true;
		m_triggerChannel = atoi(ret.c_str()+4) - 1;
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
#endif
}

void TektronixOscilloscope::SetTriggerChannelIndex(size_t /*i*/)
{
	//FIXME
}

float TektronixOscilloscope::GetTriggerVoltage()
{
	double level = 0;
	m_triggerLevel = level;
	m_triggerLevelValid = true;
	return level;

#if 0
	//Check cache.
	//No locking, worst case we return a just-invalidated (but still fresh-ish) result.
	if(m_triggerLevelValid)
		return m_triggerLevel;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("TRIG:LEV?");
	string ret = m_transport->ReadReply();

	double level;
	sscanf(ret.c_str(), "%lf", &level);
	m_triggerLevel = level;
	m_triggerLevelValid = true;
	return level;
#endif
}

void TektronixOscilloscope::SetTriggerVoltage(float v)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[32];
	snprintf(tmp, sizeof(tmp), "TRIG:LEV %.3f", v);
	m_transport->SendCommand(tmp);

	//Update cache
	m_triggerLevelValid = true;
	m_triggerLevel = v;

}

Oscilloscope::TriggerType TektronixOscilloscope::GetTriggerType()
{
	//FIXME
	return Oscilloscope::TRIGGER_TYPE_RISING;
}

void TektronixOscilloscope::SetTriggerType(Oscilloscope::TriggerType /*type*/)
{
	//FIXME
}

void TektronixOscilloscope::SetTriggerForChannel(OscilloscopeChannel* /*channel*/, vector<TriggerType> /*triggerbits*/)
{
	//unimplemented, no LA support
}

vector<uint64_t> TektronixOscilloscope::GetSampleRatesNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleRatesInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> TektronixOscilloscope::GetInterleaveConflicts()
{
	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleDepthsNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleDepthsInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

uint64_t TektronixOscilloscope::GetSampleRate()
{
	//FIXME
	return 1;
}

uint64_t TektronixOscilloscope::GetSampleDepth()
{
	//FIXME
	return 1;
}

void TektronixOscilloscope::SetSampleDepth(uint64_t /*depth*/)
{
	//FIXME
}

void TektronixOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//FIXME
}

void TektronixOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t TektronixOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}
