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
#include "RigolOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RigolOscilloscope::RigolOscilloscope()
	: m_triggerArmed(false)
	, m_triggerOneShot(false)
{
	//nothing in base class
}

RigolOscilloscope::~RigolOscilloscope()
{
}

/**
	@brief Connect to the scope and figure out what's going on
 */
void RigolOscilloscope::SharedCtorInit()
{
	//Ask for the ID
	SendCommand("*IDN?");
	string reply = ReadReply();
	char vendor[128] = "";
	char model[128] = "";
	char serial[128] = "";
	char version[128] = "";
	if(4 != sscanf(reply.c_str(), "%127[^,],%127[^,],%127[^,],%127s", vendor, model, serial, version))
	{
		LogError("Bad IDN response %s\n", reply.c_str());
		return;
	}
	m_vendor = vendor;
	m_model = model;
	m_serial = serial;
	m_fwVersion = version;

	//Last digit of the model number is the number of channels
	int model_number;
	if(1 != sscanf(model, "DS%d", &model_number))
	{
		LogError("Bad model number\n");
		return;
	}
	int nchans = model_number % 10;
	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN1");
		chname[4] += i;

		//Color the channels based on Rigol's standard color sequence (yellow-cyan-red-blue)
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#ffff00";
				break;

			case 1:
				color = "#00ffff";
				break;

			case 2:
				color = "#ff00ff";
				break;

			case 3:
				color = "#336699";
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
	}
	m_analogChannelCount = nchans;

	//Configure acquisition modes
	SendCommand("WAV:FORM BYTE");
	SendCommand("WAV:MODE RAW");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string RigolOscilloscope::GetName()
{
	return m_model;
}

string RigolOscilloscope::GetVendor()
{
	return m_vendor;
}

string RigolOscilloscope::GetSerial()
{
	return m_serial;
}

unsigned int RigolOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void RigolOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//no caching yet
	m_channelOffsets.clear();
}

bool RigolOscilloscope::IsChannelEnabled(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	SendCommand(m_channels[i]->GetHwname() + ":DISP?");
	string reply = ReadReply();
	if(reply == "0")
		return false;
	else
		return true;
}

void RigolOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	SendCommand(m_channels[i]->GetHwname() + ":DISP ON");
}

void RigolOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	SendCommand(m_channels[i]->GetHwname() + ":DISP OFF");
}

OscilloscopeChannel::CouplingType RigolOscilloscope::GetChannelCoupling(size_t i)
{
	//FIXME
	return OscilloscopeChannel::COUPLE_DC_1M;
}

void RigolOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	//FIXME
}

double RigolOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	SendCommand(m_channels[i]->GetHwname() + ":PROB?");

	string reply = ReadReply();
	double atten;
	sscanf(reply.c_str(), "%lf", &atten);
	return atten;
}

void RigolOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	//FIXME
}

int RigolOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	SendCommand(m_channels[i]->GetHwname() + ":BWL?");
	string reply = ReadReply();
	if(reply == "20M")
		return 20;
	else
		return 0;
}

void RigolOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	//FIXME
}

double RigolOscilloscope::GetChannelVoltageRange(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	SendCommand(m_channels[i]->GetHwname() + ":RANGE?");

	string reply = ReadReply();
	double range;
	sscanf(reply.c_str(), "%lf", &range);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void RigolOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	//FIXME
}

OscilloscopeChannel* RigolOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

double RigolOscilloscope::GetChannelOffset(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	SendCommand(m_channels[i]->GetHwname() + ":OFFS?");

	string reply = ReadReply();
	double offset;
	sscanf(reply.c_str(), "%lf", &offset);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void RigolOscilloscope::SetChannelOffset(size_t i, double offset)
{
	//FIXME
}

void RigolOscilloscope::ResetTriggerConditions()
{
	//FIXME
}

Oscilloscope::TriggerMode RigolOscilloscope::PollTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	SendCommand("TRIG:STAT?");
	string stat = ReadReply();

	if(stat == "TD")
		return TRIGGER_MODE_TRIGGERED;
	else if(stat == "RUN")
		return TRIGGER_MODE_RUN;
	else if(stat == "WAIT")
		return TRIGGER_MODE_WAIT;
	else if(stat == "AUTO")
		return TRIGGER_MODE_AUTO;
	else
	{
		//The "TD" state is not sticky on Rigol scopes, unlike the equivalent LeCroy status register bit.
		//The scope will go from "run" to "stop" state on trigger with only a momentary pass through "TD".
		//If we armed the trigger recently and we're now stopped, this means we must have triggered.
		if(m_triggerArmed)
		{
			m_triggerArmed = false;
			return TRIGGER_MODE_TRIGGERED;
		}

		//Nope, we're actually stopped
		return TRIGGER_MODE_STOP;
	}
}

bool RigolOscilloscope::AcquireData(bool toQueue)
{
	//workaround for high latency links to let the UI thread get the mutex
	usleep(1000);

	lock_guard<recursive_mutex> lock(m_mutex);

	LogDebug("Acquiring data\n");
	LogIndenter li;

	//Grab the analog waveform data
	int unused1;
	int unused2;
	int npoints;
	int unused3;
	double sec_per_sample;
	double xorigin;
	double unused4;
	double yincrement;
	double yreference;
	size_t maxpoints = 250*1000;
	unsigned char* temp_buf = new unsigned char[maxpoints];
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		LogDebug("Channel %zu\n", i);

		SendCommand(string("WAV:SOUR ") + m_channels[i]->GetHwname());

		//This is basically the same function as a LeCroy WAVEDESC, but much less detailed
		SendCommand("WAV:PRE?");
		string reply = ReadReply();
		sscanf(reply.c_str(), "%d,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf",
			&unused1,
			&unused2,
			&npoints,
			&unused3,
			&sec_per_sample,
			&xorigin,
			&unused4,
			&yincrement,
			&yreference);
		int64_t ps_per_sample = round(sec_per_sample * 1e12f);
		LogDebug("%ld ps per sample\n", ps_per_sample);
		LogDebug("%d points\n", npoints);

		//Downloading the waveform is a pain in the butt, because we can only pull 250K points at a time!
		for(size_t npoint=0; npoint < maxpoints; npoint += maxpoints)
		{
			//Ask for the data
			char tmp[128];
			snprintf(tmp, sizeof(tmp), "WAV:STAR %zu", npoint + 1);	//ONE based indexing WTF
			SendCommand(tmp);
			size_t end = npoint + maxpoints;
			if(end > npoints)
				end = npoints;
			snprintf(tmp, sizeof(tmp), "WAV:STOP %zu", end + 1);
			SendCommand(tmp);

			//Read the data block
			size_t blocksize = end - npoints;
			SendCommand("WAV:DATA?");
			ReadRawData(blocksize, temp_buf);

			LogDebug("%02x %02x %02x %02x",
				temp_buf[0],
				temp_buf[1],
				temp_buf[2],
				temp_buf[3]);
		}
	}

	//Clean up
	delete[] temp_buf;

	//TODO: support digital channels

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		SendCommand("TRIG_MODE SINGLE");
		m_triggerArmed = true;
	}

	return true;
}

void RigolOscilloscope::Start()
{
	LogDebug("Start single trigger\n");
	lock_guard<recursive_mutex> lock(m_mutex);
	SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RigolOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RigolOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	SendCommand("STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

bool RigolOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

size_t RigolOscilloscope::GetTriggerChannelIndex()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//This is nasty because there are separate commands to see what the trigger source is
	//depending on what the trigger type is!!!
	//FIXME: For now assume edge
	SendCommand("TRIG:EDG:SOUR?");
	string ret = ReadReply();

	for(size_t i=0; i<m_channels.size(); i++)
	{
		if(m_channels[i]->GetHwname() == ret)
			return i;
	}

	LogWarning("Unknown trigger source %s\n", ret.c_str());
	return 0;
}

void RigolOscilloscope::SetTriggerChannelIndex(size_t i)
{
	//FIXME
}

float RigolOscilloscope::GetTriggerVoltage()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//This is nasty because there are separate commands to see what the trigger source is
	//depending on what the trigger type is!!!
	//FIXME: For now assume edge
	SendCommand("TRIG:EDG:LEV?");
	string ret = ReadReply();

	double level;
	sscanf(ret.c_str(), "%lf", &level);
	//lock_guard<recursive_mutex> lock(m_cacheMutex);
	//m_channelOffsets[i] = offset;
	return level;
}

void RigolOscilloscope::SetTriggerVoltage(float v)
{
	//FIXME
}

Oscilloscope::TriggerType RigolOscilloscope::GetTriggerType()
{
	//FIXME
	return Oscilloscope::TRIGGER_TYPE_RISING;
}

void RigolOscilloscope::SetTriggerType(Oscilloscope::TriggerType type)
{
	//FIXME
}

void RigolOscilloscope::SetTriggerForChannel(OscilloscopeChannel* /*channel*/, vector<TriggerType> /*triggerbits*/)
{
	//unimplemented, no LA support
}

vector<uint64_t> RigolOscilloscope::GetSampleRatesNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> RigolOscilloscope::GetSampleRatesInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> RigolOscilloscope::GetInterleaveConflicts()
{
	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> RigolOscilloscope::GetSampleDepthsNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> RigolOscilloscope::GetSampleDepthsInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}
