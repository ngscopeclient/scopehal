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

/*
 * Current State
 * =============
 * - Digital channels not implemented
 * - Only basic edge trigger supported. Coupling, hysteresis, B trigger not implemented
 *
 * RS Oscilloscope driver parts (c) 2021 Francisco Sedano, tested on RTM3004
 */


#include "scopehal.h"
#include "RSRTO6Oscilloscope.h"
#include "EdgeTrigger.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RSRTO6Oscilloscope::RSRTO6Oscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_hasAFG(false)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleRateValid(false)
	, m_sampleDepthValid(false)
	, m_triggerOffsetValid(false)
{
	LogDebug("m_model: %s\n", m_model.c_str());
	if (m_model != "RTO6")
	{
		LogFatal("rs.rto6 driver only appropriate for RTO6");
	}

	SCPISocketTransport* sockettransport = NULL;

	if (!(sockettransport = dynamic_cast<SCPISocketTransport*>(transport)))
	{
		LogFatal("rs.rto6 driver requires 'lan' transport");
	}

	// RTO6 always has four analog channels
	m_analogChannelCount = 4;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
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
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			color,
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_ANALOG,
			i);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();
	}

	// All RTO6 have external trigger; only edge is supported
	m_extTrigChannel = new OscilloscopeChannel(
		this,
		"EXT",
		"",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_channels.push_back(m_extTrigChannel);

	m_digitalChannelBase = m_channels.size();
	m_digitalChannelCount = 0;

	string reply = m_transport->SendCommandQueuedWithReply("*OPT?", false);
	vector<string> opts;
	stringstream s_stream(reply);
	while(s_stream.good()) {
		string substr;
		getline(s_stream, substr, ',');
		opts.push_back(substr);
	}

	for (auto app : opts)
	{
		if (app == "B1")
		{
			LogVerbose(" * RTO6 has logic analyzer/MSO option\n");
			m_digitalChannelCount = 16; // Always 16 (2x8 probe "pods") to my understanding
		}
		else if (app == "B6")
		{
			LogVerbose(" * RTO6 has func gen option\n");
			m_hasAFG = true;
		}
		else
		{
			LogDebug("(* Also has option '%s' (ignored))\n", app.c_str());
		}
	}

	// Set up digital channels (if any)
	for(unsigned int i=0; i<m_digitalChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = string("D0");
		chname[1] += i;

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			"#555555",
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_COUNTS),
			Stream::STREAM_TYPE_DIGITAL,
			m_channels.size());
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();
	}

	if (m_digitalChannelCount)
		m_transport->SendCommandQueued("DIG1:THCoupling OFF"); //Allow different threshold per-bank

	if (m_hasAFG)
	{
		m_transport->SendCommandQueued("WGEN1:SOURCE FUNCGEN"); //Don't currently support modulation or other modes
		m_transport->SendCommandQueued("WGEN2:SOURCE FUNCGEN");

		for (int i = 0; i < 2; i++)
		{
			m_firstAFGIndex = m_channels.size();
			auto ch = new FunctionGeneratorChannel(this, "WGEN" + to_string(i + 1), "#808080", m_channels.size());
			m_channels.push_back(ch);
		}
	}

	m_transport->SendCommandQueued("FORMat:DATA REAL,32"); //Report in f32
	m_transport->SendCommandQueued("ACQuire:COUNt 1"); //Limit to one acquired waveform per "SINGLE"
	m_transport->SendCommandQueued("EXPort:WAVeform:INCXvalues OFF"); //Don't include X values in data
	m_transport->SendCommandQueued("TIMebase:ROLL:ENABle OFF"); //No roll mode
	m_transport->SendCommandQueued("TRIGGER1:MODE NORMAL"); //No auto trigger
	m_transport->SendCommandQueued("ACQuire:CDTA ON"); //All channels have same timebase/etc
	m_transport->SendCommandQueued("PROBE1:SETUP:ATT:MODE MAN"); //Allow/use manual attenuation setting with unknown probes
	m_transport->SendCommandQueued("SYSTEM:KLOCK OFF"); //Don't lock front-panel
	m_transport->SendCommandQueued("*WAI");

	GetSampleDepth();
}

RSRTO6Oscilloscope::~RSRTO6Oscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string RSRTO6Oscilloscope::GetDriverNameInternal()
{
	return "rs.rto6";
}

unsigned int RSRTO6Oscilloscope::GetInstrumentTypes() const
{
	unsigned int resp = Instrument::INST_OSCILLOSCOPE;

	if (m_hasAFG)
		resp |= Instrument::INST_FUNCTION;

	return resp;
}

uint32_t RSRTO6Oscilloscope::GetInstrumentTypesForChannel(size_t i) const
{
	if(m_hasAFG && (i >= m_firstAFGIndex))
		return Instrument::INST_FUNCTION;

	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void RSRTO6Oscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelsEnabled.clear();
	m_channelCouplings.clear();
	m_channelAttenuations.clear();

	delete m_trigger;
	m_trigger = NULL;
}

OscilloscopeChannel* RSRTO6Oscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

bool RSRTO6Oscilloscope::IsChannelEnabled(size_t i)
{
	if(i == m_extTrigChannel->GetIndex())
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	bool resp;

	if (IsAnalog(i))
	{
		resp = m_transport->SendCommandQueuedWithReply(
							m_channels[i]->GetHwname() + ":STATE?") == "1";
	}
	else
	{
		resp = m_transport->SendCommandQueuedWithReply(
							"BUS1:PAR:BIT" + to_string(HWDigitalNumber(i)) + ":STATE?") == "1";
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelsEnabled[i] = resp;
	return m_channelsEnabled[i];
}

void RSRTO6Oscilloscope::EnableChannel(size_t i)
{
	if(i == m_extTrigChannel->GetIndex())
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	if (IsAnalog(i))
		m_transport->SendCommandImmediate(m_channels[i]->GetHwname() + ":STATE 1; *WAI");
	else
		m_transport->SendCommandImmediate("BUS1:PAR:BIT" + to_string(HWDigitalNumber(i)) + ":STATE 1; *WAI");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);

	if (IsAnalog(i))
		m_channelsEnabled[i] = true; // Digital channel may fail to enable if pod not connected
}

void RSRTO6Oscilloscope::DisableChannel(size_t i)
{
	if(i == m_extTrigChannel->GetIndex())
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	if (IsAnalog(i))
		m_transport->SendCommandImmediate(m_channels[i]->GetHwname() + ":STATE 0; *WAI");
	else
		m_transport->SendCommandImmediate("BUS1:PAR:BIT" + to_string(HWDigitalNumber(i)) + ":STATE 0; *WAI");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = false;
}

vector<OscilloscopeChannel::CouplingType> RSRTO6Oscilloscope::GetAvailableCouplings(size_t i)
{
	vector<OscilloscopeChannel::CouplingType> ret;

	if (IsAnalog(i))
	{
		ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
		ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	}

	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	return ret;
}

OscilloscopeChannel::CouplingType RSRTO6Oscilloscope::GetChannelCoupling(size_t i)
{
	if (!IsAnalog(i))
		return OscilloscopeChannel::COUPLE_DC_50;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	string reply = m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":COUP?");
	OscilloscopeChannel::CouplingType coupling;

	if(reply == "AC")
		coupling = OscilloscopeChannel::COUPLE_AC_1M;
	else if(reply == "DCL" || reply == "DCLimit")
		coupling = OscilloscopeChannel::COUPLE_DC_1M;
	else if(reply == "DC")
		coupling = OscilloscopeChannel::COUPLE_DC_50;
	else
	{
		LogWarning("invalid coupling value\n");
		coupling = OscilloscopeChannel::COUPLE_DC_50;
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = coupling;
	return coupling;
}

void RSRTO6Oscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if (!IsAnalog(i)) return;

	{
		// lock_guard<recursive_mutex> lock(m_mutex);
		switch(type)
		{
			case OscilloscopeChannel::COUPLE_DC_50:
				m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP DC");
				break;

			case OscilloscopeChannel::COUPLE_AC_1M:
				m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP AC");
				break;

			case OscilloscopeChannel::COUPLE_DC_1M:
				m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP DCLimit");
				break;

			default:
				LogError("Invalid coupling for channel\n");
		}
	}
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = type;
}

// PROBE1:SETUP:ATT:MODE?
//  If MAN: PROBE1:SETUP:GAIN:MANUAL?
//  If AUTO: PROBE1:SETUP:ATT?

double RSRTO6Oscilloscope::GetChannelAttenuation(size_t i)
{
	if (!IsAnalog(i))
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	string reply;
	reply = m_transport->SendCommandQueuedWithReply(
						"PROBE" + to_string(i+1) + ":SETUP:ATT:MODE?");

	double attenuation;

	if (reply == "MAN")
	{
		reply = m_transport->SendCommandQueuedWithReply(
						"PROBE" + to_string(i+1) + ":SETUP:GAIN:MANUAL?");
		attenuation = stod(reply);
	}
	else
	{
		reply = m_transport->SendCommandQueuedWithReply(
						"PROBE" + to_string(i+1) + ":SETUP:ATT?");
		attenuation = stod(reply);
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelAttenuations[i] = attenuation;
	return attenuation;
}

void RSRTO6Oscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if (!IsAnalog(i)) return;

	string reply;
	reply = m_transport->SendCommandQueuedWithReply(
						"PROBE" + to_string(i+1) + ":SETUP:ATT:MODE?");

	if (reply == "MAN")
	{
		m_transport->SendCommandQueued(
						"PROBE" + to_string(i+1) + ":SETUP:GAIN:MANUAL " + to_string(atten));

		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
	}
	else
	{
		// Can't override attenuation of known probe type
	}
}

std::string RSRTO6Oscilloscope::GetProbeName(size_t i)
{
	if (!IsAnalog(i))
		return "";

	return m_transport->SendCommandQueuedWithReply(
						"PROBE" + to_string(i+1) + ":SETUP:NAME?");
}

unsigned int RSRTO6Oscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if (!IsAnalog(i))
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	string reply;
	reply = m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":BANDWIDTH?");

	unsigned int bw = 0;

	if (reply == "FULL")
	{
		bw = 0;
	}
	else if (reply == "B200")
	{
		bw = 200;
	}
	else if (reply == "B20")
	{
		bw = 20;
	}
	else
	{
		LogWarning("Unknown reported bandwidth: %s\n", reply.c_str());
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelBandwidthLimits[i] = bw;
	return bw;
}

void RSRTO6Oscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	if (!IsAnalog(i)) return;

	LogDebug("Request bandwidth: %u\n", limit_mhz);

	string limit_str;

	if (limit_mhz == 0)
	{
		limit_str = "FULL";
		limit_mhz = 0;
	}
	else if (limit_mhz == 20)
	{
		limit_str = "B20";
		limit_mhz = 20;
	}
	else if (limit_mhz == 200)
	{
		limit_str = "B200";
		limit_mhz = 200;
	}
	else
	{
		LogWarning("Unsupported requested bandwidth\n");
		return;
	}

	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BANDWIDTH " + limit_str);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelBandwidthLimits[i] = limit_mhz;
}

vector<unsigned int> RSRTO6Oscilloscope::GetChannelBandwidthLimiters(size_t i)
{
	vector<unsigned int> ret;

	if (IsAnalog(i))
	{
		ret.push_back(20);
		ret.push_back(200);
	}

	ret.push_back(0);
	return ret;
}

float RSRTO6Oscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	if (!IsAnalog(i)) return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	string reply = m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":RANGE?");

	float range;
	sscanf(reply.c_str(), "%f", &range);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void RSRTO6Oscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	if (!IsAnalog(i)) return;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
	}

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:RANGE %.4f", m_channels[i]->GetHwname().c_str(), range);
	m_transport->SendCommandQueued(cmd);
}

float RSRTO6Oscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	if (!IsAnalog(i)) return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	// lock_guard<recursive_mutex> lock2(m_mutex);

	string reply = m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":OFFS?");

	float offset;
	sscanf(reply.c_str(), "%f", &offset);
	offset = -offset;
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void RSRTO6Oscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	if (!IsAnalog(i)) return;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
	}

	// lock_guard<recursive_mutex> lock(m_mutex);
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:OFFS %.4f", m_channels[i]->GetHwname().c_str(), -offset);
	m_transport->SendCommandQueued(cmd);
}

//////////////////////////////////////////////////////////////////////////////// <Digital>

vector<Oscilloscope::DigitalBank> RSRTO6Oscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;

	for (unsigned int i = 0; i < m_digitalChannelCount; i += 4)
	{
		DigitalBank bank;
		for (int n = 0; n < 4; n++)
			bank.push_back(static_cast<OscilloscopeChannel*>(m_channels[m_digitalChannelBase + i + n]));
		banks.push_back(bank);
	}

	return banks;
}

Oscilloscope::DigitalBank RSRTO6Oscilloscope::GetDigitalBank(size_t channel)
{
	return GetDigitalBanks()[HWDigitalNumber(channel) - (HWDigitalNumber(channel) % 4)];
}

bool RSRTO6Oscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
	// TODO: It is "sorta" configurable... but not as a settable value
	// See https://www.rohde-schwarz.com/webhelp/RTO6_HTML_UserManual_en/Content/5af36d65427b4b68.htm
}

bool RSRTO6Oscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float RSRTO6Oscilloscope::GetDigitalThreshold(size_t channel)
{
	// Cache?
	return stof(m_transport->SendCommandQueuedWithReply("DIG" + to_string(HWDigitalNumber(channel)) + ":THR?"));
}

void RSRTO6Oscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	m_transport->SendCommandQueuedWithReply("DIG" + to_string(HWDigitalNumber(channel)) + ":THR " + to_string(level));
}

//////////////////////////////////////////////////////////////////////////////// </Digital>

Oscilloscope::TriggerMode RSRTO6Oscilloscope::PollTrigger()
{
	// lock_guard<recursive_mutex> lock(m_mutex);
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	////////////////////////////////////////////////////////////////////////////
	string state = m_transport->SendCommandQueuedWithReply("ACQuire:CURRent?");

	if (state == "0")
	{
		return TRIGGER_MODE_RUN;
	}
	else
	{
		if (state != "1")
			LogWarning("ACQuire:CURRent? -> %s\n", state.c_str());

		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	// return m_triggerArmed ? TRIGGER_MODE_TRIGGERED : TRIGGER_MODE_STOP;
}

template <typename T> size_t RSRTO6Oscilloscope::AcquireHeader(T* cap, string chname)
{
	//This is basically the same function as a LeCroy WAVEDESC, but much less detailed
	string reply = m_transport->SendCommandImmediateWithReply(chname + ":DATA:HEAD?; *WAI");

	double xstart;
	double xstop;
	size_t length;
	int samples_per_interval;
	int rc = sscanf(reply.c_str(), "%lf,%lf,%zu,%d", &xstart, &xstop, &length, &samples_per_interval);
	if (samples_per_interval != 1)
		LogFatal("Don't understand samples_per_interval != 1");

	if (rc != 4 || length == 0)
	{
		/* No data - Skip query the scope and move on */
		return 0;
	}

	//Figure out the sample rate
	double capture_len_sec = xstop - xstart;
	double sec_per_sample = capture_len_sec / length;
	int64_t fs_per_sample = round(sec_per_sample * FS_PER_SECOND);
	LogDebug("%" PRId64 " fs/sample\n", fs_per_sample);

	size_t reported_srate = (FS_PER_SECOND / fs_per_sample);

	if (reported_srate != m_sampleRate)
	{
		LogWarning("Reported sample rate %zu != expected sample rate %" PRIu64 "; using what it said\n", reported_srate, m_sampleRate);
	}

	if (length != m_sampleDepth)
	{
		LogWarning("Reported depth %zu != expected depth %" PRIu64 "; using what I think is correct\n", length, m_sampleDepth);
		length = m_sampleDepth;
	}

	//Set up the capture we're going to store our data into (no high res timer on R&S scopes)

	cap->m_timescale = fs_per_sample;
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = time(NULL);
	double t = GetTime();
	cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;

	cap->Resize(length);
	cap->PrepareForCpuAccess();

	return length;
}

bool RSRTO6Oscilloscope::AcquireData()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->FlushCommandQueue();
	LogDebug(" ** AcquireData ** \n");
	LogIndenter li;

	GetSampleDepth();

	auto start_time = std::chrono::system_clock::now();

	// m_transport->SendCommandQueued("*DCL; *WAI");

	map<int, vector<WaveformBase*> > pending_waveforms;
	bool any_data = false;

	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		LogDebug("Starting acquisition phase for ch%zu\n", i);

		auto cap = new UniformAnalogWaveform;
		size_t length = AcquireHeader(cap, m_channels[i]->GetHwname());

		if (!length)
		{
			delete cap;
			pending_waveforms[i].push_back(NULL);
			continue;
		}

		any_data = true;

		size_t transferred = 0;
		// Request a reasonably-sized buffer as this may cause RAM allocation in recv(2)
		const size_t block_size = 50e6;

		unsigned char* dest_buf = (unsigned char*)cap->m_samples.GetCpuPointer();

		LogDebug(" - Begin transfer of %zu bytes\n", length);

		while (transferred != length)
		{
			size_t this_length = block_size;
			if (this_length > (length - transferred))
				this_length = length - transferred;

			string params =  " "+to_string(transferred)+","+to_string(this_length);

			if (transferred == 0 && this_length == length)
				params = "";

			LogDebug("[%3d%%] Query ...`DATA?%s` (B)\n", (int)(100*((float)transferred/(float)length)), params.c_str());

			//Ask for the data
			size_t len_bytes;
			unsigned char* samples = (unsigned char*)m_transport->SendCommandImmediateWithRawBlockReply(m_channels[i]->GetHwname() + ":DATA?"+params+"; *WAI", len_bytes);

			if (len_bytes != (this_length*sizeof(float)))
			{
				LogError("Unexpected number of bytes back; aborting acquisition");
				std::this_thread::sleep_for(std::chrono::microseconds(100000));
				m_transport->FlushRXBuffer();

				delete cap;

				for (auto* c : pending_waveforms[i])
				{
					delete c;
				}

				delete[] samples;

				return false;
			}

			unsigned char* cpy_target = dest_buf+(transferred*sizeof(float));
			// LogDebug("Copying %zuB from %p to %p\n", len_bytes, samples, cpy_target);

			memcpy(cpy_target, samples, len_bytes);
			transferred += this_length;
			delete[] samples;

			//Discard trailing newline
			uint8_t disregard;
			m_transport->ReadRawData(1, &disregard);
		}

		LogDebug("[100%%] Done\n");

		cap->MarkSamplesModifiedFromCpu();

		//Done, update the data
		pending_waveforms[i].push_back(cap);
	}

	bool didAcquireAnyDigitalChannels = false;

	for(size_t i=m_digitalChannelBase; i<(m_digitalChannelBase + m_digitalChannelCount); i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		if (!didAcquireAnyDigitalChannels)
		{
			while (m_transport->SendCommandImmediateWithReply("FORM?") != "ASC,0")
			{
				m_transport->SendCommandImmediate("FORM ASC; *WAI"); //Only possible to get data out in ASCII format
				std::this_thread::sleep_for(std::chrono::microseconds(1000000));
			}
			didAcquireAnyDigitalChannels = true;
		}

		string hwname = "DIG" + to_string(HWDigitalNumber(i));

		LogDebug("Starting acquisition for dig%d\n", HWDigitalNumber(i));

		auto cap = new SparseDigitalWaveform;
		size_t length = AcquireHeader(cap, hwname);

		if (!length)
		{
			delete cap;
			pending_waveforms[i].push_back(NULL);
			continue;
		}

		size_t expected_bytes = length * 2; // Commas between items + newline

		// Digital channels do not appear to support selecting a subset, so no 'chunking'

		LogDebug(" - Begin transfer of %zu bytes (*2)\n", length);

		// Since it's ascii the scope just sends it as a SCPI 'line' without the size block
		m_transport->SendCommandImmediate(hwname + ":DATA?; *WAI");
		unsigned char* samples = new unsigned char[expected_bytes];
		size_t read_bytes = m_transport->ReadRawData(expected_bytes, samples);

		if (read_bytes != expected_bytes)
		{
			LogWarning("Unexpected number of bytes back; aborting acquisiton\n");
			std::this_thread::sleep_for(std::chrono::microseconds(100000));
			m_transport->FlushRXBuffer();

			delete cap;

			for (auto* c : pending_waveforms[i])
			{
				delete c;
			}

			delete[] samples;

			return false;
		}

		bool last = samples[0] == '1';

		cap->m_offsets[0] = 0;
		cap->m_durations[0] = 1;
		cap->m_samples[0] = last;

		size_t k = 0;

		for(size_t m=1; m<length; m++)
		{
			bool sample = samples[m*2] == '1';

			//Deduplicate consecutive samples with same value
			//FIXME: temporary workaround for rendering bugs
			//if(last == sample)
			if( (last == sample) && ((m+5) < length) && (m > 5))
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

		cap->MarkSamplesModifiedFromCpu();
		cap->MarkTimestampsModifiedFromCpu();

		delete[] samples;

		//Done, update the data
		pending_waveforms[i].push_back(cap);
	}

	if (didAcquireAnyDigitalChannels)
		m_transport->SendCommandImmediate("FORMat:DATA REAL,32"); //Return to f32

	if (any_data)
	{
		//Now that we have all of the pending waveforms, save them in sets across all channels
		m_pendingWaveformsMutex.lock();
		size_t num_pending = 1;	//TODO: segmented capture support
		for(size_t i=0; i<num_pending; i++)
		{
			SequenceSet s;
			for(size_t j=0; j<m_channels.size(); j++)
			{
				if(IsChannelEnabled(j))
					s[m_channels[j]] = pending_waveforms[j][i];
			}
			m_pendingWaveforms.push_back(s);
		}
		m_pendingWaveformsMutex.unlock();
	}

	if(!any_data || !m_triggerOneShot)
	{
		m_transport->SendCommandImmediate("SINGle");
		std::this_thread::sleep_for(std::chrono::microseconds(100000));
		// If we don't wait here, sending the query for available waveforms will race and return 1 for the exitisting waveform and jam everything up.
		m_triggerArmed = true;
	}
	else
	{
		m_triggerArmed = false;
	}

	auto end_time = std::chrono::system_clock::now();

	LogDebug("Acquisition took %" PRId64 "\n", std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());

	return any_data;
}

void RSRTO6Oscilloscope::Start()
{
	LogDebug("Start\n");
	m_transport->SendCommandImmediate("SINGle");
	std::this_thread::sleep_for(std::chrono::microseconds(100000));
	// If we don't wait here, sending the query for available waveforms will race and return 1 for the exitisting waveform and jam everything up.
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RSRTO6Oscilloscope::StartSingleTrigger()
{
	LogDebug("Start oneshot\n");
	m_transport->SendCommandImmediate("SINGle");
	std::this_thread::sleep_for(std::chrono::microseconds(100000));
	// If we don't wait here, sending the query for available waveforms will race and return 1 for the exitisting waveform and jam everything up.
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RSRTO6Oscilloscope::Stop()
{
	m_triggerArmed = false;

	LogDebug("Stop!\n");
	m_transport->SendCommandImmediate("STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

void RSRTO6Oscilloscope::ForceTrigger()
{
	if (m_triggerArmed)
		m_transport->SendCommandImmediate("TRIGGER1:FORCE");
}

bool RSRTO6Oscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> RSRTO6Oscilloscope::GetSampleRatesNonInterleaved()
{
	LogWarning("RSRTO6Oscilloscope::GetSampleRatesNonInterleaved unimplemented\n");

	// FIXME -- Arbitrarily copied from Tek
	vector<uint64_t> ret;

	const int64_t k = 1000;
	const int64_t m = k*k;
	const int64_t g = k*m;

	uint64_t bases[] = { 1000, 1250, 2500, 3125, 5000, 6250 };
	vector<uint64_t> scales = {1, 10, 100, 1*k};

	for(auto b : bases)
		ret.push_back(b / 10);

	for(auto scale : scales)
	{
		for(auto b : bases)
			ret.push_back(b * scale);
	}

	// // MSO6 also supports these, or at least had them available in the picker before.
	// // TODO: Are these actually supported?

	// if (m_family == FAMILY_MSO6) {
	// 	for(auto b : bases) {
	// 		ret.push_back(b * 10 * k);
	// 	}
	// }

	// We break with the pattern on the upper end of the frequency range
	ret.push_back(12500 * k);
	ret.push_back(25 * m);
	ret.push_back(31250 * k);
	ret.push_back(62500 * k);
	ret.push_back(125 * m);
	ret.push_back(250 * m);
	ret.push_back(312500 * k);
	ret.push_back(625 * m);
	ret.push_back(1250 * m);
	ret.push_back(1562500 * k);
	ret.push_back(3125 * m);
	ret.push_back(6250 * m);
	ret.push_back(12500 * m);

	// Below are interpolated. 8 bits, not 12.
	//TODO: we can save bandwidth by using 8 bit waveform download for these

	ret.push_back(25 * g);

	// MSO5 supports these, TODO: Does MSO6?
	ret.push_back(25000 * m);
	ret.push_back(62500 * m);
	ret.push_back(125000 * m);
	ret.push_back(250000 * m);
	ret.push_back(500000 * m);

	return ret;
}

vector<uint64_t> RSRTO6Oscilloscope::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> RSRTO6Oscilloscope::GetInterleaveConflicts()
{
	LogWarning("RSRTO6Oscilloscope::GetInterleaveConflicts unimplemented\n");

	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> RSRTO6Oscilloscope::GetSampleDepthsNonInterleaved()
{
	LogWarning("RSRTO6Oscilloscope::GetSampleDepthsNonInterleaved unimplemented\n");

	//FIXME -- Arbitrarily copied from Tek
	vector<uint64_t> ret;

	const int64_t k = 1000;
	const int64_t m = k*k;
	// const int64_t g = k*m;

	ret.push_back(500);
	ret.push_back(1 * k);
	ret.push_back(2 * k);
	ret.push_back(5 * k);
	ret.push_back(10 * k);
	ret.push_back(20 * k);
	ret.push_back(50 * k);
	ret.push_back(100 * k);
	ret.push_back(200 * k);
	ret.push_back(500 * k);

	ret.push_back(1 * m);
	ret.push_back(2 * m);
	ret.push_back(5 * m);
	ret.push_back(10 * m);
	ret.push_back(20 * m);
	ret.push_back(50 * m);
	ret.push_back(62500 * k);
	ret.push_back(100 * m);
	ret.push_back(400 * m);
	ret.push_back(800 * m);

	return ret;
}

vector<uint64_t> RSRTO6Oscilloscope::GetSampleDepthsInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

uint64_t RSRTO6Oscilloscope::GetSampleRate()
{
	if(m_sampleRateValid)
	{
		LogDebug("GetSampleRate() queried and returned cached value %" PRIu64 "\n", m_sampleRate);
		return m_sampleRate;
	}

	m_sampleRate = stod(m_transport->SendCommandQueuedWithReply("ACQUIRE:SRATE?"));
	m_sampleRateValid = true;

	LogDebug("GetSampleRate() queried and got new value %" PRIu64 "\n", m_sampleRate);

	return 1;
}

uint64_t RSRTO6Oscilloscope::GetSampleDepth()
{
	if(m_sampleDepthValid)
	{
		LogDebug("GetSampleDepth() queried and returned cached value %" PRIu64 "\n", m_sampleDepth);
		return m_sampleDepth;
	}

	GetSampleRate();

	m_sampleDepth = stod(m_transport->SendCommandQueuedWithReply("TIMEBASE:RANGE?")) * (double)m_sampleRate;
	m_sampleDepthValid = true;

	LogDebug("GetSampleDepth() queried and got new value %" PRIu64 "\n", m_sampleDepth);

	return 1;
}

void RSRTO6Oscilloscope::SetSampleDepth(uint64_t depth)
{
	GetSampleRate();

	//Update the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_sampleDepth = depth;
		m_sampleDepthValid = true;
	}

	LogDebug("SetSampleDepth() setting to %" PRIu64 "\n", depth);

	m_transport->SendCommandQueued(string("TIMEBASE:RANGE ") + to_string((double)depth / (double)m_sampleRate));
}

void RSRTO6Oscilloscope::SetSampleRate(uint64_t rate)
{
	//Update the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_sampleRate = rate;
		m_sampleRateValid = true;
	}

	LogDebug("SetSampleRate() setting to %" PRIu64 "\n", rate);

	m_transport->SendCommandQueued(string("ACQUIRE:SRATE ") + to_string(rate));

	SetSampleDepth(m_sampleDepth);
}

void RSRTO6Oscilloscope::SetTriggerOffset(int64_t offset)
{
	//Update the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_triggerOffset = offset;
		m_triggerOffsetValid = false; // Probably will be rounded and/or clipped
	}

	m_transport->SendCommandQueued("TIMEBASE:HORIZONTAL:POSITION " + to_string(-((double)offset)*((double)SECONDS_PER_FS)));
}

int64_t RSRTO6Oscilloscope::GetTriggerOffset()
{
	if(m_triggerOffsetValid)
	{
		// LogDebug("GetTriggerOffset() queried and returned cached value %ld\n", m_triggerOffset);
		return m_triggerOffset;
	}

	string reply = m_transport->SendCommandQueuedWithReply("TIMEBASE:HORIZONTAL:POSITION?");

	m_triggerOffset = -stof(reply)*FS_PER_SECOND;
	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

bool RSRTO6Oscilloscope::IsInterleaving()
{
	return false;
}

bool RSRTO6Oscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void RSRTO6Oscilloscope::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	string resp = m_transport->SendCommandQueuedWithReply("TRIGGER1:TYPE?");

	if (resp == "EDGE")
		PullEdgeTrigger();
	else
	{
		LogWarning("Unknown Trigger Type. Forcing Edge.\n");

		delete m_trigger;

		m_trigger = new EdgeTrigger(this);
		EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

		et->SetType(EdgeTrigger::EDGE_RISING);
		et->SetInput(0, StreamDescriptor(GetChannelByHwName("CHAN1"), 0), true);
		et->SetLevel(1.0);
		PushTrigger();
		PullTrigger();
	}
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void RSRTO6Oscilloscope::PullEdgeTrigger()
{
	if( (m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new EdgeTrigger(this);
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

	string reply = m_transport->SendCommandQueuedWithReply("TRIGGER1:SOURCE?");
	et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

	reply = m_transport->SendCommandQueuedWithReply("TRIGGER1:EDGE:SLOPE?");
	if (reply == "POS")
		et->SetType(EdgeTrigger::EDGE_RISING);
	else if (reply == "NEG")
		et->SetType(EdgeTrigger::EDGE_FALLING);
	else if (reply == "EITH")
		et->SetType(EdgeTrigger::EDGE_ANY);
	else
	{
		LogWarning("Unknown edge type\n");
		et->SetType(EdgeTrigger::EDGE_ANY);
	}

	reply = m_transport->SendCommandQueuedWithReply("TRIGGER1:LEVEL?");
	et->SetLevel(stof(reply));
}

void RSRTO6Oscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);
	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void RSRTO6Oscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	m_transport->SendCommandQueued("TRIGGER1:EVENT SINGLE");
	m_transport->SendCommandQueued("TRIGGER1:TYPE EDGE");
	m_transport->SendCommandQueued(string("TRIGGER1:SOURCE ") + trig->GetInput(0).m_channel->GetHwname());

	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommandQueued("TRIGGER1:EDGE:SLOPE POSITIVE");
			break;

		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommandQueued("TRIGGER1:EDGE:SLOPE NEGATIVE");
			break;

		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommandQueued("TRIGGER1:EDGE:SLOPE EITHER");
			break;

		default:
			LogWarning("Unknown edge type\n");
			break;
	}

	m_transport->SendCommandQueued(string("TRIGGER1:LEVEL") /*+ to_string(trig->GetInput(0).m_channel->GetIndex())*/ + " " + to_string(trig->GetLevel()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator

vector<FunctionGenerator::WaveShape> RSRTO6Oscilloscope::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> res;
	for (const auto& i : m_waveShapeNames)
		res.push_back(i.second);

	return res;
}

#define TO_HW_STR(chan) to_string(chan - m_firstAFGIndex + 1)

//Configuration
bool RSRTO6Oscilloscope::GetFunctionChannelActive(int chan)
{
	return m_transport->SendCommandQueuedWithReply("WGEN" + TO_HW_STR(chan) +":ENABLE?") == "ON";
}

void RSRTO6Oscilloscope::SetFunctionChannelActive(int chan, bool on)
{
	m_transport->SendCommandQueued("WGEN" + TO_HW_STR(chan) +":ENABLE " + (on ? "ON" : "OFF"));
}

bool RSRTO6Oscilloscope::HasFunctionDutyCycleControls(int chan)
{
	return GetFunctionChannelShape(chan) == FunctionGenerator::SHAPE_SQUARE;
}

float RSRTO6Oscilloscope::GetFunctionChannelDutyCycle(int chan)
{
	return stof(m_transport->SendCommandQueuedWithReply("WGEN" + TO_HW_STR(chan) +":FUNC:SQUARE:DCYCLE?")) / 100.;
}

void RSRTO6Oscilloscope::SetFunctionChannelDutyCycle(int chan, float duty)
{
	m_transport->SendCommandQueued("WGEN" + TO_HW_STR(chan) +":FUNC:SQUARE:DCYCLE " + to_string(duty*100.));
}

float RSRTO6Oscilloscope::GetFunctionChannelAmplitude(int chan)
{
	return stof(m_transport->SendCommandQueuedWithReply("WGEN" + TO_HW_STR(chan) +":VOLTAGE?"));
}

void RSRTO6Oscilloscope::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	m_transport->SendCommandQueued("WGEN" + TO_HW_STR(chan) +":VOLTAGE " + to_string(amplitude));
}

float RSRTO6Oscilloscope::GetFunctionChannelOffset(int chan)
{
	return stof(m_transport->SendCommandQueuedWithReply("WGEN" + TO_HW_STR(chan) +":VOLTAGE:OFFSET?"));
}

void RSRTO6Oscilloscope::SetFunctionChannelOffset(int chan, float offset)
{
	m_transport->SendCommandQueued("WGEN" + TO_HW_STR(chan) +":VOLTAGE:OFFSET " + to_string(offset));
}

float RSRTO6Oscilloscope::GetFunctionChannelFrequency(int chan)
{
	return stof(m_transport->SendCommandQueuedWithReply("WGEN" + TO_HW_STR(chan) +":FREQUENCY?"));
}

void RSRTO6Oscilloscope::SetFunctionChannelFrequency(int chan, float hz)
{
	m_transport->SendCommandQueued("WGEN" + TO_HW_STR(chan) +":FREQUENCY " + to_string(hz));
}

FunctionGenerator::WaveShape RSRTO6Oscilloscope::GetFunctionChannelShape(int chan)
{
	string reply = m_transport->SendCommandQueuedWithReply("WGEN" + TO_HW_STR(chan) +":FUNCTION?");

	if (m_waveShapeNames.count(reply) == 0)
	{
		LogWarning("Unknown waveshape: %s\n", reply.c_str());
		return FunctionGenerator::SHAPE_SINE;
	}

	return m_waveShapeNames.at(reply);
}

void RSRTO6Oscilloscope::SetFunctionChannelShape(int chan, FunctionGenerator::WaveShape shape)
{
	for (const auto& i : m_waveShapeNames)
	{
		if (i.second == shape)
		{
			m_transport->SendCommandQueued("WGEN" + TO_HW_STR(chan) +":FUNCTION " + i.first);
			return;
		}
	}

	LogWarning("Unsupported WaveShape requested\n");
}

bool RSRTO6Oscilloscope::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return false;
}

FunctionGenerator::OutputImpedance RSRTO6Oscilloscope::GetFunctionChannelOutputImpedance(int chan)
{
	return (m_transport->SendCommandQueuedWithReply("WGEN" + TO_HW_STR(chan) +":OUTPUT?") == "FIFT") ?
			FunctionGenerator::IMPEDANCE_50_OHM : FunctionGenerator::IMPEDANCE_HIGH_Z;
}

void RSRTO6Oscilloscope::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	m_transport->SendCommandQueued("WGEN" + TO_HW_STR(chan) +":OUTPUT " +
		((z == FunctionGenerator::IMPEDANCE_50_OHM) ? "FIFTY" : "HIZ"));
}

const std::map<const std::string, const FunctionGenerator::WaveShape> RSRTO6Oscilloscope::m_waveShapeNames = {
		{"SIN", FunctionGenerator::SHAPE_SINE},
		{"SQU", FunctionGenerator::SHAPE_SQUARE},
		{"RAMP", FunctionGenerator::SHAPE_TRIANGLE},
		{"DC", FunctionGenerator::SHAPE_DC},
		{"PULS", FunctionGenerator::SHAPE_PULSE},
		{"SINC", FunctionGenerator::SHAPE_SINC},
		{"CARD", FunctionGenerator::SHAPE_CARDIAC},
		{"GAUS", FunctionGenerator::SHAPE_GAUSSIAN},
		{"LORN", FunctionGenerator::SHAPE_LORENTZ},
		{"EXPR", FunctionGenerator::SHAPE_EXPONENTIAL_RISE},
		{"EXPF", FunctionGenerator::SHAPE_EXPONENTIAL_DECAY},

		// {"", FunctionGenerator::SHAPE_ARB} // Not supported
};
