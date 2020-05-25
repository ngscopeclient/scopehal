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
#include "RigolOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RigolOscilloscope::RigolOscilloscope(SCPITransport *transport)
	: SCPIOscilloscope(transport), m_triggerArmed(false), m_triggerOneShot(false)
{
	//Last digit of the model number is the number of channels
	if (1 != sscanf(m_model.c_str(), "DS%d", &model_number))
	{
		if (1 != sscanf(m_model.c_str(), "MSO%d", &model_number))
		{
			LogError("Bad model number\n");
			return;
		}
		else
		{
			model_name = "MSO";
			protocol = MSO5;
		}
	}
	else
	{
		model_name = "DS";
		protocol = MSO5;
	}

	int nchans = model_number % 10;
	for (int i = 0; i < nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN1");
		chname[4] += i;

		//Color the channels based on Rigol's standard color sequence (yellow-cyan-red-blue)
		string color = "#ffffff";
		switch (i)
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

	//Configure acquisition modes
	m_transport->SendCommand("WAV:FORM BYTE");
	m_transport->SendCommand("WAV:MODE RAW");
	if (protocol == MSO5)
		for (int i = 0; i < 4; i++)
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":VERN ON");
}

RigolOscilloscope::~RigolOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

unsigned int RigolOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

string RigolOscilloscope::GetDriverNameInternal()
{
	return "rigol";
}

void RigolOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelsEnabled.clear();
	m_triggerChannelValid = false;
	m_triggerLevelValid = false;
}

bool RigolOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if (i == m_extTrigChannel->GetIndex())
		return false;

	//TODO: handle digital channels, for now just claim they're off
	if (i >= m_analogChannelCount)
		return false;

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if (m_channelsEnabled.find(i) != m_channelsEnabled.end())
		return m_channelsEnabled[i];

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP?");
	string reply = m_transport->ReadReply();
	if (reply == "0")
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

void RigolOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP ON");
}

void RigolOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(m_channels[i]->GetHwname() + ":DISP OFF");
}

OscilloscopeChannel::CouplingType RigolOscilloscope::GetChannelCoupling(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
	string reply = m_transport->ReadReply();

	if (reply == "AC")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if (reply == "DC")
		return OscilloscopeChannel::COUPLE_DC_1M;
	else /* if(reply == "GND") */
		return OscilloscopeChannel::COUPLE_GND;
}

void RigolOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	switch (type)
	{
	case OscilloscopeChannel::COUPLE_AC_1M:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP AC");
		break;

	case OscilloscopeChannel::COUPLE_DC_1M:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
		break;

	case OscilloscopeChannel::COUPLE_GND:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP GND");
		break;

	default:
		LogError("Invalid coupling for channel\n");
	}
}

double RigolOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");

	string reply = m_transport->ReadReply();
	double atten;
	sscanf(reply.c_str(), "%lf", &atten);
	return atten;
}

void RigolOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	switch ((int)(atten * 10000 + 0.1)) // + 0.1 in case atten is for example 0.049999 or so, to round it to 0.05 which turns to an int of 500
	{
	case 1:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.0001");
		break;
	case 2:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.0002");
		break;
	case 5:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.0005");
		break;
	case 10:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.001");
		break;
	case 20:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.002");
		break;
	case 50:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.005");
		break;
	case 100:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.01");
		break;
	case 200:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.02");
		break;
	case 500:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.05");
		break;
	case 1000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.1");
		break;
	case 2000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.2");
		break;
	case 5000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 0.5");
		break;
	case 10000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 1");
		break;
	case 20000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 2");
		break;
	case 50000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 5");
		break;
	case 100000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 10");
		break;
	case 200000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 20");
		break;
	case 500000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 50");
		break;
	case 1000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 100");
		break;
	case 2000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 200");
		break;
	case 5000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 500");
		break;
	case 10000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 1000");
		break;
	case 20000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 2000");
		break;
	case 50000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 5000");
		break;
	case 100000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 10000");
		break;
	case 200000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 20000");
		break;
	case 500000000:
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB 50000");
		break;
	default:
		LogError("Invalid attenuation for channel\n");
	}
}

int RigolOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL?");
	string reply = m_transport->ReadReply();
	if (reply == "20M")
		return 20;
	else
		return 0;
}

void RigolOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	//FIXME
	lock_guard<recursive_mutex> lock(m_mutex);

	if (model_name == "MSO" & (model_number / 1000) == 5)
	{
		switch (model_number % 1000 / 10)
		{
		case 7:
		case 10:
			if (limit_mhz <= 20)
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL 20M");
			else
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL OFF");
			break;
		case 20:
			if (limit_mhz <= 20)
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL 20M");
			else if (limit_mhz <= 100)
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL 100M");
			else
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL OFF");
			break;
		case 35:
			if (limit_mhz <= 20)
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL 20M");
			else if (limit_mhz <= 100)
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL 100M");
			else if (limit_mhz <= 200)
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL 200M");
			else
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL OFF");
			break;
		default:
			LogError("Invalid model number\n");
		}
	}
	else
		LogError("Invalid model for Bandwidth Limit\n");
}

double RigolOscilloscope::GetChannelVoltageRange(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if (m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	if (protocol == DS)
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":RANGE?");
	else if (protocol == MSO5)
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":SCALE?");

	string reply = m_transport->ReadReply();
	double range;
	sscanf(reply.c_str(), "%lf", &range);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if (protocol == MSO5)
		range = 8 * range;
	m_channelVoltageRanges[i] = range;
	return range;
}

void RigolOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char buf[128];
	if (protocol == DS)
		snprintf(buf, sizeof(buf), "%s:RANGE %f", m_channels[i]->GetHwname().c_str(), range);
	else if (protocol == MSO5)
		snprintf(buf, sizeof(buf), "%s:SCALE %f", m_channels[i]->GetHwname().c_str(), range / 8);
	m_transport->SendCommand(buf);
	m_channelVoltageRanges[i] = range;

	//FIXME
}

OscilloscopeChannel *RigolOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

double RigolOscilloscope::GetChannelOffset(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if (m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFS?");

	string reply = m_transport->ReadReply();
	double offset;
	sscanf(reply.c_str(), "%lf", &offset);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void RigolOscilloscope::SetChannelOffset(size_t i, double offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char buf[128];
	snprintf(buf, sizeof(buf), "%s:OFFS %f", m_channels[i]->GetHwname().c_str(), offset);
	m_transport->SendCommand(buf);
}

void RigolOscilloscope::ResetTriggerConditions()
{
	//FIXME
}

Oscilloscope::TriggerMode RigolOscilloscope::PollTrigger()
{
	//workaround for high latency links to let the UI thread get the mutex
	usleep(1000);

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("TRIG:STAT?");
	string stat = m_transport->ReadReply();

	if (stat == "TD")
		return TRIGGER_MODE_TRIGGERED;
	else if (stat == "RUN")
		return TRIGGER_MODE_RUN;
	else if (stat == "WAIT")
		return TRIGGER_MODE_WAIT;
	else if (stat == "AUTO")
		return TRIGGER_MODE_AUTO;
	else
	{
		//The "TD" state is not sticky on Rigol scopes, unlike the equivalent LeCroy status register bit.
		//The scope will go from "run" to "stop" state on trigger with only a momentary pass through "TD".
		//If we armed the trigger recently and we're now stopped, this means we must have triggered.
		if (m_triggerArmed)
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
	//LogDebug("Acquiring data\n");

	//TODO
	bool enabled[4] = {true, true, true, true};

	//workaround for high latency links to let the UI thread get the mutex
	usleep(1000);

	lock_guard<recursive_mutex> lock(m_mutex);
	LogIndenter li;

	//Grab the analog waveform data
	int unused1;
	int unused2;
	int npoints;
	int unused3;
	double sec_per_sample;
	double xorigin;
	double xreference;
	double yincrement;
	double yorigin;
	double yreference;
	size_t maxpoints = 250 * 1000;
	if (protocol == DS)
		maxpoints = 250 * 1000;
	else if (protocol == MSO5);
		maxpoints = 25 * 10 * 1000; // You can use 250E6 points too, but it is very slow
	unsigned char *temp_buf = new unsigned char[maxpoints];
	map<int, vector<AnalogWaveform *>> pending_waveforms;
	for (size_t i = 0; i < m_analogChannelCount; i++)
	{
		if (!enabled[i])
		{
			if (!toQueue)
				m_channels[i]->SetData(NULL);
			continue;
		}

		//LogDebug("Channel %zu\n", i);

		m_transport->SendCommand(string("WAV:SOUR ") + m_channels[i]->GetHwname());

		//This is basically the same function as a LeCroy WAVEDESC, but much less detailed
		m_transport->SendCommand("WAV:PRE?");
		string reply = m_transport->ReadReply();
		//LogDebug("Preamble = %s\n", reply.c_str());
		sscanf(reply.c_str(), "%d,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf",
			   &unused1,
			   &unused2,
			   &npoints,
			   &unused3,
			   &sec_per_sample,
			   &xorigin,
			   &xreference,
			   &yincrement,
			   &yorigin,
			   &yreference);
		int64_t ps_per_sample = round(sec_per_sample * 1e12f);
		//LogDebug("X: %d points, %f origin, ref %f ps/sample %ld\n", npoints, xorigin, xreference, ps_per_sample);
		//LogDebug("Y: %f inc, %f origin, %f ref\n", yincrement, yorigin, yreference);

		//Set up the capture we're going to store our data into
		AnalogWaveform *cap = new AnalogWaveform;
		cap->m_timescale = ps_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startPicoseconds = (t - floor(t)) * 1e12f;

		//Downloading the waveform is a pain in the butt, because we can only pull 250K points at a time!
		for (size_t npoint = 0; npoint < maxpoints; npoint += maxpoints)
		{
			//Ask for the data
			char tmp[128];
			snprintf(tmp, sizeof(tmp), "WAV:STAR %zu", npoint + 1); //ONE based indexing WTF
			m_transport->SendCommand(tmp);
			size_t end = npoint + maxpoints;
			if (end > npoints)
				end = npoints;
			snprintf(tmp, sizeof(tmp), "WAV:STOP %zu", end + 1);
			m_transport->SendCommand(tmp);

			//Ask for the data block
			m_transport->SendCommand("WAV:DATA?");

			//Read block header
			unsigned char header[12] = {0};
			m_transport->ReadRawData(11, header);

			//Look up the block size
			//size_t blocksize = end - npoints;
			//LogDebug("Block size = %zu\n", blocksize);
			size_t header_blocksize;
			sscanf((char *)header, "#9%zu", &header_blocksize);
			//LogDebug("Header block size = %zu\n", header_blocksize);

			//Read actual block content and decode it
			//Scale: (value - Yorigin - Yref) * Yinc
			m_transport->ReadRawData(header_blocksize + 1, temp_buf); //why is there a trailing byte here??
			double ydelta = yorigin + yreference;
			cap->Resize(cap->m_samples.size() + header_blocksize);
			for (size_t j = 0; j < header_blocksize; j++)
			{
				float v = (static_cast<float>(temp_buf[j]) - ydelta) * yincrement;
				//LogDebug("V = %.3f, temp=%d, delta=%f, inc=%f\n", v, temp_buf[j], ydelta, yincrement);
				cap->m_offsets[npoint + j] = npoint + j;
				cap->m_durations[npoint + j] = 1;
				cap->m_samples[npoint + j] = v;
			}

			//Done, update the data
			if (npoint == 0 && !toQueue)
				m_channels[i]->SetData(cap);
			else
				pending_waveforms[i].push_back(cap);
		}
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 0;
	if (toQueue) //if saving to queue, the 0'th segment counts too
		num_pending++;
	for (size_t i = 0; i < num_pending; i++)
	{
		SequenceSet s;
		for (size_t j = 0; j < m_analogChannelCount; j++)
		{
			if (enabled[j])
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//Clean up
	delete[] temp_buf;

	//TODO: support digital channels

	//Re-arm the trigger if not in one-shot mode
	if (!m_triggerOneShot)
	{
		if (protocol == DS)
			m_transport->SendCommand("TRIG_MODE SINGLE");
		else if (protocol == MSO5)
			m_transport->SendCommand("SING");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

void RigolOscilloscope::Start()
{
	//LogDebug("Start single trigger\n");
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RigolOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RigolOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

bool RigolOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

size_t RigolOscilloscope::GetTriggerChannelIndex()
{
	//Check cache
	//No locking, worst case we return a result a few seconds old
	if (m_triggerChannelValid)
		return m_triggerChannel;

	lock_guard<recursive_mutex> lock(m_mutex);

	//This is nasty because there are separate commands to see what the trigger source is
	//depending on what the trigger type is!!!
	//FIXME: For now assume edge
	m_transport->SendCommand("TRIG:EDGE:SOUR?");
	string ret = m_transport->ReadReply();
	LogDebug("Trigger source: %s\n", ret.c_str());

	for (size_t i = 0; i < m_channels.size(); i++)
	{
		if (m_channels[i]->GetHwname() == ret)
		{
			m_triggerChannel = i;
			m_triggerChannelValid = true;
			return i;
		}
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
	//Check cache.
	//No locking, worst case we return a just-invalidated (but still fresh-ish) result.
	if (m_triggerLevelValid)
		return m_triggerLevel;

	lock_guard<recursive_mutex> lock(m_mutex);

	//This is nasty because there are separate commands to see what the trigger source is
	//depending on what the trigger type is!!!
	//FIXME: For now assume edge
	m_transport->SendCommand("TRIG:EDGE:LEV?");
	string ret = m_transport->ReadReply();

	double level;
	sscanf(ret.c_str(), "%lf", &level);
	m_triggerLevel = level;
	m_triggerLevelValid = true;
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

void RigolOscilloscope::SetTriggerForChannel(OscilloscopeChannel * /*channel*/, vector<TriggerType> /*triggerbits*/)
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

uint64_t RigolOscilloscope::GetSampleRate()
{
	//FIXME
	return 1;
}

uint64_t RigolOscilloscope::GetSampleDepth()
{
	//FIXME
	return 1;
}

void RigolOscilloscope::SetSampleDepth(uint64_t /*depth*/)
{
	//FIXME
}

void RigolOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//FIXME
}
