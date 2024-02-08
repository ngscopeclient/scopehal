/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "KeysightDCA.h"
#include "DCAEdgeTrigger.h"

using namespace std;

#define ERROR_HARDWARE_MISSING -241

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

KeysightDCA::KeysightDCA(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
{

	// Color the channels based on Agilent's standard color sequence (yellow-green-violet-pink)
	vector<string> channel_colors = {
		"#ffff00",
		"#32ff00",
		"#5578ff",
		"#ff0084",
	};
	int nchans = 0;
	for(int i=0; i < 4; i++)
	{
		// Hardware name of the channel
		auto chname = string("CHAN1");
		chname[4] += i;

		if (!IsChannelPresent(chname))
			break;

		auto color = channel_colors[i];

		// Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			color,
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_ANALOG,
			i
		);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();
		ConfigureWaveform(chname);
		nchans++;
	}
	m_analogChannelCount = nchans;

	AddTriggerSource("FPAN", "Front Panel");
	if (IsModulePresent("LMOD"))
		AddTriggerSource("LMOD", "Left Module");
	if (IsModulePresent("RMOD"))
		AddTriggerSource("RMOD", "Right Module");
}

KeysightDCA::~KeysightDCA()
{
}

void KeysightDCA::ConfigureWaveform(string channel)
{
	//Configure transport format to raw 8-bit int
	m_transport->SendCommand(":WAV:SOUR " + channel);
	m_transport->SendCommand(":WAV:FORM BYTE");
}

void KeysightDCA::AddTriggerSource(string hw_name, string display_name)
{
	auto channel = new OscilloscopeChannel(
		this,
		hw_name,
		"",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_channels.push_back(channel);
	channel->SetDisplayName(display_name);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string KeysightDCA::GetDriverNameInternal()
{
	return "keysightdca";
}

unsigned int KeysightDCA::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t KeysightDCA::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

int KeysightDCA::GetLastError()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SYST:ERR?");
	auto reply = m_transport->ReadReply();
	return atoi(reply.c_str());
}

void KeysightDCA::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelCouplings.clear();
	m_channelAttenuations.clear();
	m_channelBandwidthLimits.clear();
	m_channelsEnabled.clear();

	m_sampleRateValid = false;
	m_sampleDepthValid = false;

	delete m_trigger;
	m_trigger = NULL;
}

bool KeysightDCA::IsAnalogChannel(size_t i)
{
	return GetOscilloscopeChannel(i)->GetType(0) == Stream::STREAM_TYPE_ANALOG;
}

bool KeysightDCA::IsChannelPresent(string name)
{
	// Check whether the channel exists (depends on what modules are installed).
	// Unfortunately there doesn't seem to be a way to query whether a channel exists,
	// so we just query its 'enabled' state and look for an error.
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("*CLS");
	m_transport->SendCommand(name + "?");
	m_transport->ReadReply();
	return GetLastError() != ERROR_HARDWARE_MISSING;
}

bool KeysightDCA::IsModulePresent(string name)
{

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("MODEL? " + name);
	auto reply = m_transport->ReadReply();
	return reply != "Not Present";
}

bool KeysightDCA::IsChannelEnabled(size_t i)
{
	if(!IsAnalogChannel(i))
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":DISP?");
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

void KeysightDCA::EnableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":DISP ON");
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;
}

void KeysightDCA::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":DISP OFF");
	}


	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = false;
}

vector<OscilloscopeChannel::CouplingType> KeysightDCA::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	return ret;
}

OscilloscopeChannel::CouplingType KeysightDCA::GetChannelCoupling(size_t i)
{
	if(!IsAnalogChannel(i))
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	return OscilloscopeChannel::COUPLE_DC_50;
}

void KeysightDCA::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{

}

double KeysightDCA::GetChannelAttenuation(size_t i)
{
	if(!IsAnalogChannel(i))
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":PROB?");
		reply = m_transport->ReadReply();
	}

	double atten = stod(reply);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelAttenuations[i] = atten;
	return atten;
}

void KeysightDCA::SetChannelAttenuation(size_t i, double atten)
{
	if(!IsAnalogChannel(i))
		return;

	{
		lock_guard<recursive_mutex> lock(m_mutex);
		PushFloat(GetOscilloscopeChannel(i)->GetHwname() + ":PROB", atten);
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelAttenuations[i] = atten;
}

unsigned int KeysightDCA::GetChannelBandwidthLimit(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}


	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":BAND?");
		reply = m_transport->ReadReply();
	}

	unsigned int bwl = 0;
	// TODO: figure out how to map these to numbers, or see if we need API changes
	if (reply == "HIGH") {

	} else if (reply == "MID") {

	} else if (reply == "LOW") {

	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelBandwidthLimits[i] = bwl;
	return bwl;
}

void KeysightDCA::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	// TODO
}

float KeysightDCA::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	if(!IsAnalogChannel(i))
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	string reply;

	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":RANGE?");

		reply = m_transport->ReadReply();
	}

	float range = stof(reply);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void KeysightDCA::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	PushFloat(GetOscilloscopeChannel(i)->GetHwname() + ":RANGE", range);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelVoltageRanges.erase(i);
}

OscilloscopeChannel* KeysightDCA::GetExternalTrigger()
{
	// TODO: scopehal doesn't currently support multiple ext. trigger channels
	return NULL;
}

float KeysightDCA::GetChannelOffset(size_t i, size_t /*stream*/)
{
	if(!IsAnalogChannel(i))
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":OFFS?");
		reply = m_transport->ReadReply();
	}

	float offset = stof(reply);
	offset = -offset;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void KeysightDCA::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	PushFloat(GetOscilloscopeChannel(i)->GetHwname() + ":OFFS", -offset);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelOffsets.erase(i);
}

Oscilloscope::TriggerMode KeysightDCA::PollTrigger()
{
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("*ESR?");
	string ter = m_transport->ReadReply();
	int cond = atoi(ter.c_str());

	// Check bit 0 ('OPC' bit)
	if((cond & (1 << 0)) == 0)
		return TRIGGER_MODE_RUN;
	else
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}
}

vector<int8_t> KeysightDCA::GetWaveformData(string channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":WAV:SOUR " + channel);
	m_transport->SendCommand(":WAV:DATA?");

	// Read the length header size
	char tmp[16] = {0};
	m_transport->ReadRawData(2, (unsigned char*)tmp);
	auto header_len = atoi(tmp+1);

	// Read data length
	m_transport->ReadRawData(header_len, (unsigned char*)tmp);
	auto data_len = atoi(tmp);

	// Read the actual data
	auto buf = vector<int8_t>(data_len);
	m_transport->ReadRawData(data_len, (unsigned char*)&buf[0]);

	// Discard trailing newline
	m_transport->ReadRawData(1, (unsigned char*)tmp);

	return buf;
}

KeysightDCA::WaveformPreamble KeysightDCA::GetWaveformPreamble(string channel)
{
	WaveformPreamble ret;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(":WAV:SOUR " + channel);
	m_transport->SendCommand(":WAV:PRE?");
	string reply = m_transport->ReadReply();
	sscanf(reply.c_str(), "%u,%u,%zu,%u,%lf,%lf,%lf,%lf,%lf,%lf",
			&ret.format, &ret.type, &ret.length, &ret.average_count,
			&ret.xincrement, &ret.xorigin, &ret.xreference,
			&ret.yincrement, &ret.yorigin, &ret.yreference);

	return ret;
}

bool KeysightDCA::AcquireData()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	LogIndenter li;

	map<int, vector<WaveformBase*> > pending_waveforms;
	for(size_t i=0; i < m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		auto chname = GetOscilloscopeChannel(i)->GetHwname();
		auto preamble = GetWaveformPreamble(chname);

		int64_t fs_per_sample = round(preamble.xincrement * FS_PER_SECOND);

		auto cap = new UniformAnalogWaveform;
		cap->m_timescale = fs_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
		cap->PrepareForCpuAccess();

		auto buf = GetWaveformData(chname);
		if(preamble.length != buf.size())
			LogError("Waveform preamble length (%zu) does not match data length (%zu)", preamble.length, buf.size());
		cap->Resize(buf.size());
		for(size_t j = 0; j < buf.size(); j++)
		{
			// Handle magic values representing clipped samples
			// TODO: handle '125' which represents missing samples
			if (buf[j] == 127)
				cap->m_samples[j] = std::numeric_limits<float>::infinity();
			else if (buf[j] == 126)
				// TODO: setting to negative infinity is not handled well by the UI
				cap->m_samples[j] = -1e30;
			else
				cap->m_samples[j] = preamble.yincrement * (buf[j] - preamble.yreference) + preamble.yorigin;
		}

		cap->MarkSamplesModifiedFromCpu();
		pending_waveforms[i].push_back(cap);
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;
	for(size_t i=0; i<num_pending; i++)
	{
		SequenceSet s;
		for (size_t j = 0; j < m_channels.size(); j++)
			if(IsChannelEnabled(j) && pending_waveforms.find(j) != pending_waveforms.end())
				s[GetOscilloscopeChannel(j)] = pending_waveforms[j][i];
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		m_transport->SendCommand("SING;*OPC");
		m_triggerArmed = true;
	}

	return true;
}

void KeysightDCA::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING;*OPC");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void KeysightDCA::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("SING;*OPC");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void KeysightDCA::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	// If STOP is sent too soon after SING, the OPC bit doesn't ever get set again.
	// Sending CLS (clear status) fixes this.
	m_transport->SendCommand("STOP;*CLS");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

void KeysightDCA::ForceTrigger()
{
	LogError("KeysightDCA::ForceTrigger not implemented\n");
}

bool KeysightDCA::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> KeysightDCA::GetSampleRatesNonInterleaved()
{
	// This scope supports any arbitrary rate up to ~200THz (20ps duration & 4096 samples),
	// so pick a range of round numbers to present to the UI
	vector<uint64_t> ret;
	uint64_t i = 1;
	while (i < 10e12) {
		ret.push_back(i);
		ret.push_back(i*2);
		ret.push_back(i*5);
		i *= 10;
	}
	ret.push_back(100e12);
	ret.push_back(200e12);
	return ret;
}

vector<uint64_t> KeysightDCA::GetSampleRatesInterleaved()
{
	return {};
}

set<Oscilloscope::InterleaveConflict> KeysightDCA::GetInterleaveConflicts()
{
	return {};
}

vector<uint64_t> KeysightDCA::GetSampleDepthsNonInterleaved()
{
	return {
		16,
		20,
		50,
		100,
		200,
		500,
		1000,
		2000,
		4000,
		4096,
	};
}

vector<uint64_t> KeysightDCA::GetSampleDepthsInterleaved()
{
	return {};
}

uint64_t KeysightDCA::GetSampleRate()
{
	if (m_sampleRateValid)
		return m_sampleRate;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("WAV:XINC?");
	double period = stof(m_transport->ReadReply());
	m_sampleRate = (1.0 / period);
	m_sampleRateValid = true;
	return m_sampleRate;
}

uint64_t KeysightDCA::GetSampleDepth()
{
	if (m_sampleDepthValid)
		return m_sampleDepth;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:POINTS?");
	uint64_t depth = stof(m_transport->ReadReply());
	m_sampleDepth = depth;
	m_sampleDepthValid = true;
	return depth;
}

void KeysightDCA::SetSampleRateAndDepth(uint64_t rate, uint64_t depth)
{
	// Calculate the duration of the requested capture in seconds
	auto duration = (double)depth / (double)rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	PushFloat("TIM:RANGE", duration);
	m_transport->SendCommand("ACQ:POINTS " + to_string(depth));
}

void KeysightDCA::SetSampleDepth(uint64_t depth)
{
	auto rate = GetSampleRate();
	SetSampleRateAndDepth(rate, depth);
	m_sampleDepth = depth;
	m_sampleDepthValid = true;
}

void KeysightDCA::SetSampleRate(uint64_t rate)
{
	auto depth = GetSampleDepth();
	SetSampleRateAndDepth(rate, depth);
	m_sampleRate = rate;
	m_sampleRateValid = true;
}

void KeysightDCA::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("TIM:POS " + to_string(offset) + "fs");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_triggerOffsetValid = false;
}

int64_t KeysightDCA::GetTriggerOffset()
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if (m_triggerOffsetValid)
			return m_triggerOffset;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("TIM:POS?");
	auto reply = m_transport->ReadReply();


	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_triggerOffset = stof(reply) * FS_PER_SECOND;
	m_triggerOffsetValid = true;
	return m_triggerOffset;
}

bool KeysightDCA::IsInterleaving()
{
	return false;
}

bool KeysightDCA::SetInterleaving(bool /*combine*/)
{
	return false;
}

void KeysightDCA::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	PullEdgeTrigger();
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void KeysightDCA::PullEdgeTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<DCAEdgeTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new DCAEdgeTrigger(this);
	auto et = dynamic_cast<DCAEdgeTrigger*>(m_trigger);

	lock_guard<recursive_mutex> lock(m_mutex);

	//Source
	m_transport->SendCommand("TRIG:SOUR?");
	string reply = m_transport->ReadReply();
	auto chan = GetOscilloscopeChannelByHwName(reply);
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

/**
	@brief Processes the slope for an edge or edge-derived trigger
 */
void KeysightDCA::GetTriggerSlope(DCAEdgeTrigger* trig, string reply)
{
	if (reply == "POS")
		trig->SetType(DCAEdgeTrigger::EDGE_RISING);
	else if (reply == "NEG")
		trig->SetType(DCAEdgeTrigger::EDGE_FALLING);
	else
		LogWarning("Unknown trigger slope %s\n", reply.c_str());
}

void KeysightDCA::PushTrigger()
{
	auto et = dynamic_cast<DCAEdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void KeysightDCA::PushEdgeTrigger(DCAEdgeTrigger* trig)
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

void KeysightDCA::PushFloat(string path, float f)
{
	m_transport->SendCommand(path + " " + to_string_sci(f));
}

void KeysightDCA::PushSlope(string path, DCAEdgeTrigger::EdgeType slope)
{
	string slope_str;
	switch(slope)
	{
		case DCAEdgeTrigger::EDGE_RISING:
			slope_str = "POS";
			break;
		case DCAEdgeTrigger::EDGE_FALLING:
			slope_str = "NEG";
			break;
		default:
			return;
	}
	m_transport->SendCommand(path + " " + slope_str);
}

vector<string> KeysightDCA::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(DCAEdgeTrigger::GetTriggerName());
	return ret;
}
