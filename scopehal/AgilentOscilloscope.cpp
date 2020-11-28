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
#include "AgilentOscilloscope.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AgilentOscilloscope::AgilentOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
{
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

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN1");
		chname[4] += i;

		//Color the channels based on Agilent's standard color sequence (yellow-green-violet-pink)
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
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			color,
			1,
			i,
			true);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();

		//Configure transport format to raw 8-bit int
		m_transport->SendCommand(":WAV:SOUR " + chname);
		m_transport->SendCommand(":WAV:FORM BYTE");

		//Request all points when we download
		m_transport->SendCommand(":WAV:POIN:MODE RAW");
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
	m_extTrigChannel->SetDefaultDisplayName();

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

AgilentOscilloscope::~AgilentOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string AgilentOscilloscope::GetDriverNameInternal()
{
	return "agilent";
}

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
	m_channelCouplings.clear();
	m_channelsEnabled.clear();

	delete m_trigger;
	m_trigger = NULL;
}

bool AgilentOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//TODO: handle digital channels, for now just claim they're off
	if(i >= m_analogChannelCount)
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP?");
		reply = m_transport->ReadReply();
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
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
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP ON");
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;
}

void AgilentOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP OFF");
	}


	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = false;
}

OscilloscopeChannel::CouplingType AgilentOscilloscope::GetChannelCoupling(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	string coup_reply, imp_reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
		coup_reply = m_transport->ReadReply();
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP?");
		imp_reply = m_transport->ReadReply();
	}

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
}

void AgilentOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
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

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = type;
}

double AgilentOscilloscope::GetChannelAttenuation(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");
		reply = m_transport->ReadReply();
	}

	double atten = stod(reply);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelAttenuations[i] = atten;
	return atten;
}

void AgilentOscilloscope::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//FIXME
}

int AgilentOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}


	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL?");
		reply = m_transport->ReadReply();
	}

	int bwl;
	if(reply == "1")
		bwl = 25;
	else
		bwl = 0;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelBandwidthLimits[i] = bwl;
	return bwl;
}

void AgilentOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
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

	string reply;

	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":RANGE?");

		reply = m_transport->ReadReply();
	}

	double range = stod(reply);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void AgilentOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:RANGE %.4f", m_channels[i]->GetHwname().c_str(), range);
	m_transport->SendCommand(cmd);
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

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFS?");
		reply = m_transport->ReadReply();
	}

	double offset = stod(reply);
	offset = -offset;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void AgilentOscilloscope::SetChannelOffset(size_t i, double offset)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:OFFS %.4f", m_channels[i]->GetHwname().c_str(), -offset);
	m_transport->SendCommand(cmd);
}

Oscilloscope::TriggerMode AgilentOscilloscope::PollTrigger()
{
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	// Based on example from 6000 Series Programmer's Guide
	// Section 10 'Synchronizing Acquisitions' -> 'Polling Synchronization With Timeout'
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":OPER:COND?");
	string ter = m_transport->ReadReply();
	int cond = atoi(ter.c_str());

	// Check bit 3 ('Run' bit)
	if((cond & (1 << 3)) != 0)
		return TRIGGER_MODE_RUN;
	else
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}
}

bool AgilentOscilloscope::AcquireData()
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
			continue;

		// Set source & get preamble
		m_transport->SendCommand(":WAV:SOUR " + m_channels[i]->GetHwname());
		m_transport->SendCommand(":WAV:PRE?");
		string reply = m_transport->ReadReply();
		sscanf(reply.c_str(), "%u,%u,%zu,%u,%lf,%lf,%lf,%lf,%lf,%lf",
				&format, &type, &length, &average_count, &xincrement, &xorigin, &xreference, &yincrement, &yorigin, &yreference);

		//Figure out the sample rate
		int64_t fs_per_sample = round(xincrement * FS_PER_SECOND);
		//LogDebug("%ld ps/sample\n", ps_per_sample);

		//LogDebug("length = %d\n", length);

		//Set up the capture we're going to store our data into
		//(no TDC data available on Agilent scopes?)
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = fs_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;

		//Ask for the data
		m_transport->SendCommand(":WAV:DATA?");

		//Read the length header
		char tmp[16] = {0};
		m_transport->ReadRawData(2, (unsigned char*)tmp);
		int num_digits = atoi(tmp+1);
		//LogDebug("num_digits = %d", num_digits);
		m_transport->ReadRawData(num_digits, (unsigned char*)tmp);
		int actual_len = atoi(tmp);
		//LogDebug("actual_len = %d", actual_len);

		uint8_t* temp_buf = new uint8_t[actual_len / sizeof(uint8_t)];

		//Read the actual data
		m_transport->ReadRawData(actual_len, (unsigned char*)temp_buf);
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
		pending_waveforms[i].push_back(cap);

		//Clean up
		delete[] temp_buf;
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	//TODO: segmented capture mode
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

uint64_t AgilentOscilloscope::GetSampleRate()
{
	//FIXME
	return 1;
}

uint64_t AgilentOscilloscope::GetSampleDepth()
{
	//FIXME
	return 1;
}

void AgilentOscilloscope::SetSampleDepth(uint64_t /*depth*/)
{
	//FIXME
}

void AgilentOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//FIXME
}

void AgilentOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t AgilentOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}

bool AgilentOscilloscope::IsInterleaving()
{
	return false;
}

bool AgilentOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void AgilentOscilloscope::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Figure out what kind of trigger is active.
	m_transport->SendCommand("TRIG:MODE?");
	string reply = m_transport->ReadReply();
	if (reply == "EDGE")
		PullEdgeTrigger();
	else if (reply == "GLIT")
		PullPulseWidthTrigger();

	//Unrecognized trigger type
	else
	{
		LogWarning("Unknown trigger type \"%s\"\n", reply.c_str());
		m_trigger = NULL;
		return;
	}
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void AgilentOscilloscope::PullEdgeTrigger()
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
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);

	lock_guard<recursive_mutex> lock(m_mutex);

	//Source
	m_transport->SendCommand("TRIG:SOUR?");
	string reply = m_transport->ReadReply();
	auto chan = GetChannelByHwName(reply);
	et->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source %s\n", reply.c_str());

	//Level
	m_transport->SendCommand("TRIG:LEV?");
	reply = m_transport->ReadReply();
	et->SetLevel(stof(reply));

	//Edge slope
	m_transport->SendCommand("TRIG:SLOPE?");
	GetTriggerSlope(et, m_transport->ReadReply());
}

void AgilentOscilloscope::PullPulseWidthTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<PulseWidthTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new PulseWidthTrigger(this);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);

	lock_guard<recursive_mutex> lock(m_mutex);

	//Source
	m_transport->SendCommand("TRIG:GLIT:SOUR?");
	string reply = m_transport->ReadReply();
	auto chan = GetChannelByHwName(reply);
	pt->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source %s\n", reply.c_str());

	//Level
	m_transport->SendCommand("TRIG:GLIT:LEV?");
	pt->SetLevel(stof(m_transport->ReadReply()));

	//Condition
	m_transport->SendCommand("TRIG:GLIT:QUAL?");
	pt->SetCondition(GetCondition(m_transport->ReadReply()));

	//Slope
	m_transport->SendCommand("TRIG:GLIT:POL?");
	GetTriggerSlope(pt, m_transport->ReadReply());

	// Bounds
	//
	// In the BETWEEN condition the bounds are stored in a different variable
	// on the scope so check & set the correct one.
	if(pt->GetCondition() == Trigger::CONDITION_BETWEEN)
	{
		m_transport->SendCommand("TRIG:GLIT:RANG?");
		reply = m_transport->ReadReply();
		stringstream ss(reply);
		string upper_bound, lower_bound;

		if (!getline(ss, upper_bound, ',') || !getline(ss, lower_bound, ','))
			LogWarning("Malformed TRIG:GLIT:RANG response: %s\n", reply.c_str());
		else
		{
			pt->SetLowerBound(stof(lower_bound) * FS_PER_SECOND);
			pt->SetUpperBound(stof(upper_bound) * FS_PER_SECOND);
		}

	}
	else
	{
		//Lower bound
		m_transport->SendCommand("TRIG:GLIT:GRE?");
		pt->SetLowerBound(stof(m_transport->ReadReply()) * FS_PER_SECOND);

		//Upper bound
		m_transport->SendCommand("TRIG:GLIT:LESS?");
		pt->SetUpperBound(stof(m_transport->ReadReply()) * FS_PER_SECOND);
	}
}

/**
	@brief Processes the slope for an edge or edge-derived trigger
 */
void AgilentOscilloscope::GetTriggerSlope(EdgeTrigger* trig, string reply)
{
	if (reply == "POS")
		trig->SetType(EdgeTrigger::EDGE_RISING);
	else if (reply == "NEG")
		trig->SetType(EdgeTrigger::EDGE_FALLING);
	else if (reply == "EITH")
		trig->SetType(EdgeTrigger::EDGE_ANY);
	else if (reply == "ALT")
		trig->SetType(EdgeTrigger::EDGE_ALTERNATING);
	else
		LogWarning("Unknown trigger slope %s\n", reply.c_str());
}

/**
	@brief Parses a trigger condition
 */
Trigger::Condition AgilentOscilloscope::GetCondition(string reply)
{
	reply = Trim(reply);

	if(reply == "LESS")
		return Trigger::CONDITION_LESS;
	else if(reply == "GRE")
		return Trigger::CONDITION_GREATER;
	else if(reply == "RANG")
		return Trigger::CONDITION_BETWEEN;

	//unknown
	return Trigger::CONDITION_LESS;
}

void AgilentOscilloscope::PushTrigger()
{
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(pt)
		PushPulseWidthTrigger(pt);
	// Must go last
	else if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void AgilentOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Mode
	m_transport->SendCommand("TRIG:MODE EDGE");

	//Source
	m_transport->SendCommand(string("TRIG:SOURCE ") + trig->GetInput(0).m_channel->GetHwname());

	//Level
	PushFloat("TRIG:LEV", trig->GetLevel());

	//Slope
	PushSlope("TRIG:SLOPE", trig->GetType());
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument
 */
void AgilentOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("TRIG:MODE GLIT");
	m_transport->SendCommand("TRIG:GLIT:SOURCE " +
		trig->GetInput(0).m_channel->GetHwname());
	PushSlope("TRIG:GLIT:POL", trig->GetType());
	PushCondition("TRIG:GLIT:QUAL", trig->GetCondition());
	PushFloat("TRIG:GLIT:LEV", trig->GetLevel());
	if(trig->GetCondition() == Trigger::CONDITION_BETWEEN)
	{
		m_transport->SendCommand("TRIG:GLIT:RANG " +
			to_string_sci(trig->GetUpperBound() * SECONDS_PER_FS) +
			"," +
			to_string_sci(trig->GetLowerBound() * SECONDS_PER_FS));
	}
	else
	{
		PushFloat("TRIG:GLIT:LESS", trig->GetUpperBound() * SECONDS_PER_FS);
		PushFloat("TRIG:GLIT:GRE",  trig->GetLowerBound() * SECONDS_PER_FS);
	}
}

void AgilentOscilloscope::PushCondition(string path, Trigger::Condition cond)
{
	string cond_str;
	switch(cond)
	{
		case Trigger::CONDITION_LESS:
			cond_str = "LESS";
			break;
		case Trigger::CONDITION_GREATER:
			cond_str = "GRE";
			break;
		case Trigger::CONDITION_BETWEEN:
			cond_str = "RANG";
			break;
		default:
			return;
	}
	m_transport->SendCommand(path + " " + cond_str);
}

void AgilentOscilloscope::PushFloat(string path, float f)
{
	m_transport->SendCommand(path + " " + to_string_sci(f));
}

void AgilentOscilloscope::PushSlope(string path, EdgeTrigger::EdgeType slope)
{
	string slope_str;
	switch(slope)
	{
		case EdgeTrigger::EDGE_RISING:
			slope_str = "POS";
			break;
		case EdgeTrigger::EDGE_FALLING:
			slope_str = "NEG";
			break;
		case EdgeTrigger::EDGE_ANY:
			slope_str = "EITH";
			break;
		case EdgeTrigger::EDGE_ALTERNATING:
			slope_str = "ALT";
			break;
		default:
			return;
	}
	m_transport->SendCommand(path + " " + slope_str);
}

vector<string> AgilentOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(EdgeTrigger::GetTriggerName());
	ret.push_back(PulseWidthTrigger::GetTriggerName());
	return ret;
}

