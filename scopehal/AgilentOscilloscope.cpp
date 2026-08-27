/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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

/**
	@file
	@brief Implementation of AgilentOscilloscope
	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "AgilentOscilloscope.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "NthEdgeBurstTrigger.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Connect to an oscilloscope

	@param transport	SCPITransport connected to the scope
 */
AgilentOscilloscope::AgilentOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport, true, 30000000) //Some models (DSOX2024A at least) take ~10 seconds to respond after network connection
	, SCPIInstrument(transport)
	, m_isSimulator(false)
{
	//Default to ancient firmware
	m_ftype = FIRMWARE_OLD;

	//For old scopes, last digit of the model number is the number of channels
	string model_number = m_model;
	model_number.erase(
		std::remove_if(
			model_number.begin(),
			model_number.end(),
			[]( char const& c ) -> bool { return !std::isdigit(c); }
		),
		model_number.end()
	);
	int nchans = stoi(model_number) % 10;

	//If this is a Keysight scope look at the firmware version
	if(strtolower(m_vendor).find("keysight") == 0)
	{
		LogTrace("Keysight scope not older Agilent, checking infiniium version\n");

		int firmwareMajor = 0;
		sscanf(m_fwVersion.c_str(), "%d.", &firmwareMajor);
		LogTrace("Firmware major version is %d\n", firmwareMajor);
		if(firmwareMajor >= 10)
			m_ftype = FIRMWARE_INFINIIUM_10;

		//If the model name is N8900A it's a simulator
		if(m_model == "N8900A")
		{
			LogNotice("Infiniium offline N8900A detected, disabling trigger and some frontend controls\n");
			m_isSimulator = true;
		}
	}

	//:SYST:CAP:CHAN? COUNT available starting in version 10.x infiniium software
	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		auto schans = Trim(m_transport->SendCommandQueuedWithReply(":SYST:CAP:CHAN? COUNT"));
		schans = str_replace("\"", "", schans);
		nchans = stoi(schans);
		LogTrace("Found %d analog channels\n", nchans);
	}

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN1");
		chname[4] += i;

		//Keysight color scheme up to 8 channels
		string color = "#ffffff";
		if(m_ftype >= FIRMWARE_INFINIIUM_10)
		{
			switch(i)
			{
				case 0:
					color = "#f9ff00";
					break;

				case 1:
					color = "#30fb00";
					break;

				case 2:
					color = "#25a0fb";
					break;

				case 3:
					color = "#ff005b";
					break;

				case 4:
					color = "#00fcfc";
					break;

				case 5:
					color = "#ff00e5";
					break;

				case 6:
					color = "#ad6aff";
					break;

				case 7:
					color = "#ff9000";
					break;
			}
		}

		//Color the channels based on Agilent's standard color sequence (yellow-green-violet-pink)
		else
		{
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
	m_analogChannelCount = nchans;

	//Add the external trigger input
	m_extTrigChannel = new OscilloscopeChannel(
		this,
		"EX",
		"",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_channels.push_back(m_extTrigChannel);
	m_extTrigChannel->SetDefaultDisplayName();

	//See what options we have
	string reply = m_transport->SendCommandQueuedWithReply("*OPT?");

	set<string> options;

	for (std::string::size_type prev_pos=0, pos=0;
	     (pos = reply.find(',', pos)) != std::string::npos;
	     prev_pos=++pos)
	{
		std::string opt( reply.substr(prev_pos, pos-prev_pos) );
		if (opt == "0")
			continue;
		if(opt.substr(opt.length() - 3, 3) == "(d)")
			opt.erase(opt.length() - 3);
		if(opt.substr(opt.length() - 1, 1) == "*")
			opt.erase(opt.length() - 1);

		options.insert(opt);
	}

	//Print out the option list and do processing for each
	LogDebug("Installed options:\n");
	if(options.empty())
		LogDebug("* None\n");
	for(auto& opt : options)
		LogDebug("* %s\n", opt.c_str());

	// If the MSO option is enabled, add digital channels
	if (options.find("MSO") != options.end())
	{
		// MSO-X 2000 series has fixed 8 digital channels regardless of analog channel count
		if (model_number.find("20") == 0)
			m_digitalChannelCount = 8;
		else
			m_digitalChannelCount = nchans * 4;
		m_digitalChannelBase = m_channels.size();
		for(unsigned int i = 0; i < m_digitalChannelCount; i++)
		{
			//Create the channel
			auto chan = new OscilloscopeChannel(
				this,
				"DIG" + to_string(i),
				"#00ffff",
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_VOLTS),
				Stream::STREAM_TYPE_DIGITAL,
				m_channels.size());
			m_channels.push_back(chan);
			chan->SetDefaultDisplayName();
		}
	}
	else
		m_digitalChannelCount = 0;

	//Clear config cache
	m_sampleRateValid = false;
	m_sampleRate = 0;
	m_sampleDepthValid = false;
	m_sampleDepth = 0;

	//Do some initial configuration
	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		m_transport->SendCommandQueued("ACQ:POIN:AUTO OFF");
		m_transport->SendCommandQueued("ACQ:SRATE:AUTO OFF");

		m_transport->SendCommandQueued("WAV:FORM WORD");
		m_transport->SendCommandQueued("WAV:STR ON");
		m_transport->SendCommandQueued("WAV:BYT LSBF");

		//TODO: ACQ:SRATE:TESTLIMITS? for min/max sample rate
		//TODO: ACQ:POINTS:TESTLIMITS? for min/max memory depth
	}

	//Create Vulkan objects for the waveform conversion
	InitVulkanQueue("AgilentOscilloscope");

	m_conversion16BitPipeline = make_unique<ComputePipeline>(
		"shaders/Convert16BitSamplesDual.spv", 2, sizeof(ConvertRawSamplesShaderArgs) );

	m_rawSampleData.SetCpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_LIKELY);
	m_rawSampleData.SetGpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_UNLIKELY);
}

AgilentOscilloscope::~AgilentOscilloscope()
{
}

/**
	@brief Set up the operating mode for a channel

	@param channel	The channel to configure
 */
void AgilentOscilloscope::ConfigureWaveform(const string& channel)
{
	//Select the channel to apply settings to
	//NOTE: this also enables the channel
	m_transport->SendCommandQueued(":WAV:SOUR " + channel);

	if(m_ftype == FIRMWARE_OLD)
	{
		//Configure transport format to raw 8-bit int
		m_transport->SendCommandQueued(":WAV:FORM BYTE");

		//Request all points when we download
		m_transport->SendCommandQueued(":WAV:POIN:MODE RAW");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant driver name "agilent".
string AgilentOscilloscope::GetDriverNameInternal()
{
	return "agilent";
}

unsigned int AgilentOscilloscope::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t AgilentOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
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
	m_channelAttenuations.clear();
	m_channelBandwidthLimits.clear();
	m_channelsEnabled.clear();
	m_probeTypes.clear();

	m_sampleRateValid = false;
	m_sampleDepthValid = false;

	delete m_trigger;
	m_trigger = nullptr;
}

/**
	@brief Check if a channel is analog

	@param i	Channel index

	@return		True if analog, false if digital
 */
bool AgilentOscilloscope::IsAnalogChannel(size_t i)
{
	return GetOscilloscopeChannel(i)->GetType(0) == Stream::STREAM_TYPE_ANALOG;
}

/**
	@brief Get the digital pod number given a channel number

	@param	i	Channel index

	@return	Pod number
 */
size_t AgilentOscilloscope::GetDigitalPodIndex(size_t i)
{
	return ((i - m_digitalChannelBase) / 8) + 1;
}

/**
	@brief Get the digital pod name given a channel number

	@param	i	Channel index

	@return	Pod name
 */
std::string AgilentOscilloscope::GetDigitalPodName(size_t i)
{
	return "POD" + to_string(GetDigitalPodIndex(i));
}

bool AgilentOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":DISP?");

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
	m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":DISP ON");

	if (IsAnalogChannel(i))
		ConfigureWaveform(GetOscilloscopeChannel(i)->GetHwname());
	else
		ConfigureWaveform(GetDigitalPodName(i));

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;
}

void AgilentOscilloscope::DisableChannel(size_t i)
{
	m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":DISP OFF");

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = false;
}

vector<OscilloscopeChannel::CouplingType> AgilentOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);
	return ret;
}

OscilloscopeChannel::CouplingType AgilentOscilloscope::GetChannelCoupling(size_t i)
{
	if(!IsAnalogChannel(i))
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	//We can batch query input config here
	OscilloscopeChannel::CouplingType coupling;
	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		auto cimp_reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":INP?");

		if(cimp_reply == "DC")
			coupling = OscilloscopeChannel::COUPLE_DC_1M;
		else if(cimp_reply == "DC50")
			coupling = OscilloscopeChannel::COUPLE_DC_50;
		else if(cimp_reply == "AC")
			coupling = OscilloscopeChannel::COUPLE_AC_1M;
		else	//probably LFR1 or LFR2
			coupling = OscilloscopeChannel::COUPLE_AC_1M;
	}

	else
	{
		auto coup_reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":COUP?");
		auto imp_reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":IMP?");

		if(coup_reply == "AC")
			coupling = OscilloscopeChannel::COUPLE_AC_1M;
		else /*if(coup_reply == "DC")*/
		{
			if(imp_reply == "ONEM")
				coupling = OscilloscopeChannel::COUPLE_DC_1M;
			else /*if(imp_reply == "FIFT")*/
				coupling = OscilloscopeChannel::COUPLE_DC_50;
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = coupling;
	return coupling;
}

void AgilentOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	// If there's a smart probe on this channel, the coupling is fixed to 50ohm so bail out.
	GetProbeType(i);
	if (m_probeTypes[i] == SmartProbe)
		return;

	//new Infiniium
	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		switch(type)
		{
			case OscilloscopeChannel::COUPLE_DC_50:
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":INP DC50");
				break;

			case OscilloscopeChannel::COUPLE_AC_1M:
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":INP AC");
				break;

			case OscilloscopeChannel::COUPLE_DC_1M:
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":INP DC");
				break;

			default:
				LogError("Invalid coupling for channel\n");
		}
	}

	//old Agilent
	else
	{
		switch(type)
		{
			case OscilloscopeChannel::COUPLE_DC_50:
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":IMP FIFT");
				break;

			case OscilloscopeChannel::COUPLE_AC_1M:
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":IMP ONEM");
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP AC");
				break;

			case OscilloscopeChannel::COUPLE_DC_1M:
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":IMP ONEM");
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
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
	if (i >= m_analogChannelCount)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		//auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":PROB?");

		LogWarning("AgilentOscilloscope::GetChannelAttenuation unimplemented for infiniium\n");

		//TODO: implement this, seems complicated
		//can be CHANx:PROB:EXT:GAIN? but also other things
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = 1;
		return 1;
	}
	else
	{
		auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":PROB?");

		double atten = stod(reply);
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
		return atten;
	}
}

void AgilentOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	// If there's a SmartProbe or AutoProbe on this channel, the attenuation is fixed so bail out.
	GetProbeType(i);
	if (m_probeTypes[i] != None)
		return;

	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		LogWarning("AgilentOscilloscope::SetChannelAttenuation unimplemented for infiniium\n");
	}

	else
	{
		m_transport->SendCommandQueued(
			GetOscilloscopeChannel(i)->GetHwname() + ":PROB" + " " + to_string_sci(atten));

		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
	}
}

unsigned int AgilentOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if (i >= m_analogChannelCount)
		return 0;

	if(m_isSimulator)
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	string reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":BWL?");

	unsigned int bwl;
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
	LogWarning("AgilentOscilloscope::SetChannelBandwidthLimit unimplemented\n");
}

float AgilentOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	if(GetOscilloscopeChannel(i)->GetType(0) != Stream::STREAM_TYPE_ANALOG)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	string reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":RANGE?");

	float range = stof(reply);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void AgilentOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
	}

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:RANGE %.4f", GetOscilloscopeChannel(i)->GetHwname().c_str(), range);
	m_transport->SendCommandQueued(cmd);
}

OscilloscopeChannel* AgilentOscilloscope::GetExternalTrigger()
{
	//FIXME
	return nullptr;
}

float AgilentOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	if(!IsAnalogChannel(i))
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":OFFS?");

	float offset = stof(reply);
	offset = -offset;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void AgilentOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
	}

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:OFFS %.4f", GetOscilloscopeChannel(i)->GetHwname().c_str(), -offset);
	m_transport->SendCommandQueued(cmd);
}

Oscilloscope::TriggerMode AgilentOscilloscope::PollTrigger()
{
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		//If we are a simulator and the trigger is armed, report a hit so we download the waveform it has loaded
		if(m_isSimulator)
		{
			if(m_triggerArmed)
			{
				m_triggerArmed = false;
				return TRIGGER_MODE_TRIGGERED;
			}
			else
				return TRIGGER_MODE_STOP;
		}

		//Check if the acquisition is done
		auto done = m_transport->SendCommandQueuedWithReply(":ADER?");
		if(done.find("1") != string::npos)
			return TRIGGER_MODE_TRIGGERED;
		return TRIGGER_MODE_RUN;
	}

	//Old agilent
	else
	{
		// Based on example from 6000 Series Programmer's Guide
		// Section 10 'Synchronizing Acquisitions' -> 'Polling Synchronization With Timeout'
		auto ter = m_transport->SendCommandQueuedWithReply(":OPER:COND?");
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
}

/**
	@brief Download waveform data from a single channel

	@param channel	Name of the channel

	@return			Raw waveform data
 */
vector<uint8_t> AgilentOscilloscope::GetWaveformData(const string& channel)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	m_transport->SendCommandQueued(":WAV:SOUR " + channel);
	m_transport->SendCommandQueued(":WAV:DATA?");
	m_transport->FlushCommandQueue();

	// Read the length header size
	char tmp[16] = {0};
	m_transport->ReadRawData(2, (unsigned char*)tmp);
	auto header_len = atoi(tmp+1);

	// Read data length
	m_transport->ReadRawData(header_len, (unsigned char*)tmp);
	auto data_len = atoi(tmp);

	// Read the actual data
	auto buf = vector<uint8_t>(data_len);
	m_transport->ReadRawData(data_len, &buf[0]);

	// Discard trailing newline
	m_transport->ReadRawData(1, (unsigned char*)tmp);

	return buf;
}

/**
	@brief Get the waveform preamble for a single channel

	@param channel	Name of the channel

	@return			Preamble data
 */
AgilentOscilloscope::WaveformPreamble AgilentOscilloscope::GetWaveformPreamble(const string& channel)
{
	WaveformPreamble ret;
	string reply;

	m_transport->SendCommandQueued(":WAV:SOUR " + channel);
	// The DSO-X 2022A sometimes only replies '+0' which isn't documented in the Programmer's Guide. Retrying once seems
	// to solve it reliably. 19 is the shortest representable string length that conforms to the sscanf format
	for (int i = 0; i < 2 && reply.length() < 19; i++)
		reply = m_transport->SendCommandQueuedWithReply(":WAV:PRE?");
	sscanf(reply.c_str(), "%u,%u,%zu,%u,%lf,%lf,%lf,%lf,%lf,%lf",
			&ret.format, &ret.type, &ret.length, &ret.average_count,
			&ret.xincrement, &ret.xorigin, &ret.xreference,
			&ret.yincrement, &ret.yorigin, &ret.yreference);

	return ret;
}

/**
	@brief Convert raw data from a digital pod into waveform objects

	@param[out] pending_waveforms	Resulting digital waveforms
	@param[in]	data				The raw sample data
	@param[in]	preamble			Preamble of the waveform
	@param		chan_start			Index of the first channel in the pod
 */
void AgilentOscilloscope::ProcessDigitalWaveforms(
       map<int, vector<WaveformBase*>> &pending_waveforms,
       vector<uint8_t> &data,
       AgilentOscilloscope::WaveformPreamble &preamble,
       size_t chan_start)
{
	for (int i = 0; i < 8; i++)
	{
		auto channel = m_digitalChannelBase + chan_start + i;
		if (IsChannelEnabled(channel))
		{
			auto cap = AllocateDigitalWaveform(m_nickname + "." + GetChannel(m_digitalChannelBase + i)->GetHwname());
			int64_t fs_per_sample = round(preamble.xincrement * FS_PER_SECOND);
			cap->m_timescale = fs_per_sample;
			cap->m_startFemtoseconds = 0;
			cap->m_triggerPhase = 0;
			double t = GetTime();
			cap->m_startTimestamp = floor(t);
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;

			//Preallocate memory assuming no deduplication possible
			cap->Resize(data.size());
			cap->PrepareForCpuAccess();

			//Save the first sample (can't merge with sample -1 because that doesn't exist)
			size_t k = 0;
			cap->m_offsets[0] = 0;
			cap->m_durations[0] = 1;
			cap->m_samples[0] = (data[0] >> i) & 1;

			//Read and de-duplicate the other samples
			//TODO: can we vectorize this somehow?
			bool last = cap->m_samples[0];
			for (size_t j = 1; j < data.size(); j++)
			{
				bool sample = (data[j] >> i) & 1;

				//Deduplicate consecutive samples with same value
				//FIXME: temporary workaround for rendering bugs
				//if(last == sample)
				if ((last == sample) && ((j+3) < data.size()))
					cap->m_durations[k] ++;

				//Nope, it toggled - store the new value
				else
				{
					k++;
					cap->m_offsets[k] = j;
					cap->m_durations[k] = 1;
					cap->m_samples[k] = sample;
					last = sample;
				}

			}

			//Done, shrink any unused space
			cap->Resize(k);
			cap->m_offsets.shrink_to_fit();
			cap->m_durations.shrink_to_fit();
			cap->m_samples.shrink_to_fit();
			cap->MarkSamplesModifiedFromCpu();
			cap->MarkTimestampsModifiedFromCpu();

			pending_waveforms[channel].push_back(cap);
		}
	}
}

bool AgilentOscilloscope::AcquireData()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	LogIndenter li;

	map<int, vector<WaveformBase*> > pending_waveforms;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		auto chname = GetOscilloscopeChannel(i)->GetHwname();
		auto preamble = GetWaveformPreamble(chname);

		//Figure out the sample rate
		int64_t fs_per_sample = round(preamble.xincrement * FS_PER_SECOND);

		//Set up the capture we're going to store our data into
		//(no TDC data available on Agilent scopes?)
		auto cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(i)->GetHwname());
		cap->m_timescale = fs_per_sample;
		cap->m_triggerPhase = 0;
		double t = GetTime();
		cap->m_startTimestamp = floor(t);
		cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;

		float gain = preamble.yincrement;
		float offset = (gain * preamble.yreference) - preamble.yorigin;

		//Modern format
		if(m_ftype >= FIRMWARE_INFINIIUM_10)
		{
			cap->Resize(preamble.length);

			//Ask for the data
			m_transport->SendCommandQueued("WAV:DATA?");
			m_transport->FlushCommandQueue();

			//Read and discard two bytes of header before actual sample data
			uint8_t tmp[2];
			m_transport->ReadRawData(2, &tmp[0]);

			//Allocate scratch buffer and read data
			m_rawSampleData.PrepareForCpuAccess();
			m_rawSampleData.resize(preamble.length * 2);
			m_transport->ReadRawData(preamble.length * 2, m_rawSampleData.GetCpuPointer());
			m_rawSampleData.MarkModifiedFromCpu();

			//Read and discard newline at end of block
			m_transport->ReadRawData(1, &tmp[0]);

			//Waveform conversion
			m_cmdBuf->begin({});
			{
				NamedDebugRange shaderRange(*m_cmdBuf, "Convert16BitSamples");
				m_conversion16BitPipeline->BindBufferNonblocking(0, cap->m_samples, *m_cmdBuf, true);
				m_conversion16BitPipeline->BindBufferNonblocking(1, m_rawSampleData, *m_cmdBuf);

				ConvertRawSamplesShaderArgs args;
				args.size = cap->size();
				args.gain = gain;
				args.offset = offset;

				const uint32_t compute_block_count = GetComputeBlockCount(cap->size(), 64*2); //2 samples per thread
				m_conversion16BitPipeline->Dispatch(
					*m_cmdBuf, args,
					min(compute_block_count, 32768u),
					compute_block_count / 32768 + 1);

				cap->MarkModifiedFromGpu();
			}

			m_cmdBuf->end();
			m_queue->SubmitAndBlock(*m_cmdBuf);
		}

		else
		{
			cap->PrepareForCpuAccess();

			// Format the capture
			auto buf = GetWaveformData(chname);
			if(preamble.length != buf.size())
				LogError("Waveform preamble length (%zu) does not match data length (%zu)", preamble.length, buf.size());
			cap->Resize(buf.size());
			ConvertUnsigned8BitSamples(cap->m_samples.GetCpuPointer(), buf.data(), gain, offset, buf.size());

			cap->MarkSamplesModifiedFromCpu();
		}

		//Done, update the data
		pending_waveforms[i].push_back(cap);
	}

	if(m_digitalChannelCount > 0)
	{
		// Fetch waveform data for each pod containing enabled channels
		map<string, vector<uint8_t>> raw_waveforms;
		for(unsigned int i = 0; i < 8 && i < m_digitalChannelCount; i++)
		{
			if(IsChannelEnabled(i + m_digitalChannelBase))
			{
				raw_waveforms.insert({"POD1", GetWaveformData("POD1")});
				break;
			}
		}
		for(unsigned int i = 8; i < 16 && i < m_digitalChannelCount; i++)
		{
			if(IsChannelEnabled(i + m_digitalChannelBase))
			{
				raw_waveforms.insert({"POD2", GetWaveformData("POD2")});
				break;
			}
		}

		if (raw_waveforms.size() > 0) {
			auto preamble = GetWaveformPreamble("POD1");
			if (raw_waveforms.find("POD1") != raw_waveforms.end())
				ProcessDigitalWaveforms(pending_waveforms, raw_waveforms.at("POD1"), preamble, 0);
			if (raw_waveforms.find("POD2") != raw_waveforms.end())
				ProcessDigitalWaveforms(pending_waveforms, raw_waveforms.at("POD2"), preamble, 8);
		}
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	//TODO: segmented capture mode
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
		if(!m_isSimulator)
			m_transport->SendCommandQueued(":SING");
		m_triggerArmed = true;
	}

	return true;
}

void AgilentOscilloscope::Start()
{
	if(!m_isSimulator)
		m_transport->SendCommandQueued("SING");

	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void AgilentOscilloscope::StartSingleTrigger()
{
	if(!m_isSimulator)
		m_transport->SendCommandQueued("SING");

	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void AgilentOscilloscope::Stop()
{
	if(!m_isSimulator)
		m_transport->SendCommandQueued("STOP");

	m_triggerArmed = false;
	m_triggerOneShot = true;
}

void AgilentOscilloscope::ForceTrigger()
{
	if(!m_isSimulator)
	{
		m_transport->SendCommandQueued(":SING");
		m_transport->SendCommandQueued(":TRIG:FORC");
	}

	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool AgilentOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

static std::map<uint64_t, double> sampleRateToDuration
{
	// Map sample rates to corresponding maximum on-screen time duration setting
	{8000      , 500},
	{20000     , 200},
	{40000     , 100},
	{80000     , 50},
	{200000    , 20},
	{400000    , 10},
	{800000    , 5},
	{2000000   , 2},
	{4000000   , 1},
	{8000000   , 500e-3},
	{20000000  , 200e-3},
	{40000000  , 100e-3},
	{80000000  , 50e-3},
	{200000000 , 20e-3},
	{400000000 , 10e-3},
	{500000000 , 5e-3},
	{2000000000, 2e-3},
};

vector<uint64_t> AgilentOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	const int64_t k = 1000;
	const int64_t m = k*k;
	const int64_t g = k*m;

	//Modern scopes
	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		//Powers of two up to 256 Msps
		for(int64_t i=1; i<=256; i *= 2)
			ret.push_back(i * m);

		ret.push_back(500 * m);	//not 512

		//Powers of two up to 256 Gsps
		//TODO: stop lower on lower end scopes
		for(int64_t i=1; i<=256; i *= 2)
			ret.push_back(i * g);
	}

	//legacy stuff
	else
	{
		for (auto x: sampleRateToDuration)
			ret.push_back(x.first);
	}

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
	//Modern scopes
	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		vector<uint64_t> ret;

		const int64_t k = 1024;
		const int64_t m = k*k;
		//const int64_t g = k*m;

		for(int64_t i=1; i < 1024; i *= 2)
			ret.push_back(i * k);

		for(int64_t i=1; i < 2048; i *= 2)
			ret.push_back(i * m);

		return ret;
	}

	else
	{
		return
		{
			100,
			250,
			500,
			1000,
			2000,
			5000,
			10000,
			20000,
			50000,
			100000,
			200000,
			500000,
			1000000,
			2000000,
			4000000,
			8000000,
		};
	}
}

vector<uint64_t> AgilentOscilloscope::GetSampleDepthsInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

uint64_t AgilentOscilloscope::GetSampleRate()
{
	if (m_sampleRateValid)
		return m_sampleRate;

	uint64_t rate = stof(m_transport->SendCommandQueuedWithReply("ACQUIRE:SRATE?"));
	m_sampleRate = rate;
	m_sampleRateValid = true;
	return rate;
}

uint64_t AgilentOscilloscope::GetSampleDepth()
{
	if (m_sampleDepthValid)
		return m_sampleDepth;

	uint64_t depth = stof(m_transport->SendCommandQueuedWithReply("ACQUIRE:POINTS?"));
	m_sampleDepth = depth;
	m_sampleDepthValid = true;
	return depth;
}

/**
	@brief Simultaneously set sample rate and memory depth

	@param rate		Sample rate, in samples per second
	@param depth	Acquisition memory depth
 */
void AgilentOscilloscope::SetSampleRateAndDepth(uint64_t rate, uint64_t depth)
{
	// Look up the maximum capture duration for the requested sample rate
	auto d = sampleRateToDuration.find(rate);
	if (d == sampleRateToDuration.end())
		return;
	auto max_duration = d->second;

	// Calculate the duration of the requested capture in seconds
	auto duration = (double)depth / (double)rate;

	// Clamp the duration to make sure we achieve at least the requested sample rate
	duration = min(duration, max_duration);

	PushFloat("TIMEBASE:RANGE", duration);
	for (auto chan : m_channels)
	{
		auto ochan = dynamic_cast<OscilloscopeChannel*>(chan);
		if(!ochan)
			continue;
		if (ochan->GetType(0) == Stream::STREAM_TYPE_ANALOG)
		{
			m_transport->SendCommandQueued(":WAV:SOUR " + chan->GetHwname());

			// This will downsample the capture in case we ended up with a sample rate much higher than requested
			m_transport->SendCommandQueued(":WAV:POINTS " + to_string(depth));
		}
	}
}

void AgilentOscilloscope::SetSampleDepth(uint64_t depth)
{
	auto rate = GetSampleRate();
	SetSampleRateAndDepth(rate, depth);
	m_sampleDepth = depth;
	m_sampleDepthValid = true;
}

void AgilentOscilloscope::SetSampleRate(uint64_t rate)
{
	auto depth = GetSampleDepth();
	SetSampleRateAndDepth(rate, depth);
	m_sampleRate = rate;
	m_sampleRateValid = true;
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
	//Figure out what kind of trigger is active.
	auto reply = m_transport->SendCommandQueuedWithReply("TRIG:MODE?");
	if (reply == "EDGE")
		PullEdgeTrigger();
	else if (reply == "GLIT")
		PullPulseWidthTrigger();
	else if (reply == "EBUR")
		PullNthEdgeBurstTrigger();

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
	if( (m_trigger != nullptr) && (dynamic_cast<EdgeTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	if(m_trigger == nullptr)
		m_trigger = new EdgeTrigger(this);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);

	//modern
	if(m_ftype >= FIRMWARE_INFINIIUM_10)
	{
		auto reply = m_transport->SendCommandQueuedWithReply("TRIG:EDGE:SOUR?");
		auto chan = GetOscilloscopeChannelByHwName(reply);
		et->SetInput(0, StreamDescriptor(chan, 0), true);
		if(!chan)
			LogWarning("Unknown trigger source %s\n", reply.c_str());

		//Edge slope
		GetTriggerSlope(et, m_transport->SendCommandQueuedWithReply("TRIG:EDGE:SLOPE?"));

		et->SetLevel(stof(m_transport->SendCommandQueuedWithReply(string("TRIG:LEV? ") + reply)));
	}

	//old Agilent
	else
	{
		//Source
		auto reply = m_transport->SendCommandQueuedWithReply("TRIG:SOUR?");
		auto chan = GetOscilloscopeChannelByHwName(reply);
		et->SetInput(0, StreamDescriptor(chan, 0), true);
		if(!chan)
			LogWarning("Unknown trigger source %s\n", reply.c_str());

		//Edge slope
		GetTriggerSlope(et, m_transport->SendCommandQueuedWithReply("TRIG:SLOPE?"));

		//Level
		et->SetLevel(stof(m_transport->SendCommandQueuedWithReply("TRIG:LEV?")));
	}
}

/**
	@brief Reads settings for an Nth-edge-burst trigger from the instrument
 */
void AgilentOscilloscope::PullNthEdgeBurstTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != nullptr) && (dynamic_cast<NthEdgeBurstTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	if(m_trigger == nullptr)
		m_trigger = new NthEdgeBurstTrigger(this);
	auto bt = dynamic_cast<NthEdgeBurstTrigger*>(m_trigger);

	//Source
	string reply = m_transport->SendCommandQueuedWithReply("TRIG:EDGE:SOUR?");
	auto chan = GetOscilloscopeChannelByHwName(reply);
	bt->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source %s\n", reply.c_str());

	//Level
	bt->SetLevel(stof(m_transport->SendCommandQueuedWithReply("TRIG:EDGE:LEV?")));

	//Slope
	GetTriggerSlope(bt, m_transport->SendCommandQueuedWithReply("TRIG:EBUR:SLOP?"));

	//Idle time
	bt->SetIdleTime(stof(m_transport->SendCommandQueuedWithReply("TRIG:EBUR:IDLE?")) * FS_PER_SECOND);

	//Edge number
	bt->SetEdgeNumber(stoi(m_transport->SendCommandQueuedWithReply("TRIG:EBUR:COUN?")));
}

/**
	@brief Reads settings for a pulse width trigger from the instrument
 */
void AgilentOscilloscope::PullPulseWidthTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != nullptr) && (dynamic_cast<PulseWidthTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	if(m_trigger == nullptr)
		m_trigger = new PulseWidthTrigger(this);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);

	//Source
	string reply = m_transport->SendCommandQueuedWithReply("TRIG:GLIT:SOUR?");
	auto chan = GetOscilloscopeChannelByHwName(reply);
	pt->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source %s\n", reply.c_str());

	//Level
	pt->SetLevel(stof(m_transport->SendCommandQueuedWithReply("TRIG:GLIT:LEV?")));

	//Condition
	pt->SetCondition(GetCondition(m_transport->SendCommandQueuedWithReply("TRIG:GLIT:QUAL?")));

	//Slope
	GetTriggerSlope(pt, m_transport->SendCommandQueuedWithReply("TRIG:GLIT:POL?"));

	// Bounds
	//
	// In the BETWEEN condition the bounds are stored in a different variable
	// on the scope so check & set the correct one.
	if(pt->GetCondition() == Trigger::CONDITION_BETWEEN)
	{
		reply = m_transport->SendCommandQueuedWithReply("TRIG:GLIT:RANG?");
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
		pt->SetLowerBound(stof(m_transport->SendCommandQueuedWithReply("TRIG:GLIT:GRE?")) * FS_PER_SECOND);

		//Upper bound
		pt->SetUpperBound(stof(m_transport->SendCommandQueuedWithReply("TRIG:GLIT:LESS?")) * FS_PER_SECOND);
	}
}

/**
	@brief Processes the slope for an edge or edge-derived trigger

	@param trig		Trigger to configure
	@param reply	Response from the instrument
 */
void AgilentOscilloscope::GetTriggerSlope(EdgeTrigger* trig, const string& reply)
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
	@brief Processes the slope for an Nth edge burst trigger

	@param trig		Trigger to configure
	@param reply	Response from the instrument
 */
void AgilentOscilloscope::GetTriggerSlope(NthEdgeBurstTrigger* trig, const string& reply)
{
	if (reply == "POS")
		trig->SetSlope(NthEdgeBurstTrigger::EDGE_RISING);
	else if (reply == "NEG")
		trig->SetSlope(NthEdgeBurstTrigger::EDGE_FALLING);
	else
		LogWarning("Unknown trigger slope %s\n", reply.c_str());
}

/**
	@brief Converts a trigger condition from a string to a Trigger::Condition

	@param reply	Response from the instrument
 */
Trigger::Condition AgilentOscilloscope::GetCondition(const string& reply)
{
	auto sreply = Trim(reply);

	if(sreply == "LESS")
		return Trigger::CONDITION_LESS;
	else if(sreply == "GRE")
		return Trigger::CONDITION_GREATER;
	else if(sreply == "RANG")
		return Trigger::CONDITION_BETWEEN;

	//unknown
	return Trigger::CONDITION_LESS;
}

/**
	@brief Figures out what kind of probe is connected to a channel

	@param i	Channel index
 */
void AgilentOscilloscope::GetProbeType(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_probeTypes.find(i) != m_probeTypes.end())
			return;
	}

	auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":PROBE:ID?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if (reply == "AutoProbe")
		m_probeTypes[i] = AutoProbe;
	else if (reply == "NONE" || reply == "Unknown")
		m_probeTypes[i] = None;
	else
		m_probeTypes[i] = SmartProbe;
}

void AgilentOscilloscope::PushTrigger()
{
	auto bt = dynamic_cast<NthEdgeBurstTrigger*>(m_trigger);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(bt)
		PushNthEdgeBurstTrigger(bt);
	else if(pt)
		PushPulseWidthTrigger(pt);
	// Must go last
	else if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for an edge trigger to the instrument

	@param trig	The trigger
 */
void AgilentOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	//Mode
	m_transport->SendCommandQueued("TRIG:MODE EDGE");

	//Source
	m_transport->SendCommandQueued(string("TRIG:SOURCE ") + trig->GetInput(0).m_channel->GetHwname());

	//Level
	PushFloat("TRIG:LEV", trig->GetLevel());

	//Slope
	PushSlope("TRIG:SLOPE", trig->GetType());
}

/**
	@brief Pushes settings for a Nth edge burst trigger to the instrument

	@param trig	The trigger
 */
void AgilentOscilloscope::PushNthEdgeBurstTrigger(NthEdgeBurstTrigger* trig)
{
	m_transport->SendCommandQueued("TRIG:MODE EBUR");
	m_transport->SendCommandQueued("TRIG:EDGE:SOURCE " +
		trig->GetInput(0).m_channel->GetHwname());
	PushFloat("TRIG:EDGE:LEV", trig->GetLevel());
	PushSlope("TRIG:EBUR:SLOP", trig->GetSlope());
	PushFloat("TRIG:EBUR:IDLE", trig->GetIdleTime() * SECONDS_PER_FS);
	m_transport->SendCommandQueued("TRIG:EBUR:COUNT " + to_string(trig->GetEdgeNumber()));
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument

	@param trig	The trigger
 */
void AgilentOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	m_transport->SendCommandQueued("TRIG:MODE GLIT");
	m_transport->SendCommandQueued("TRIG:GLIT:SOURCE " +
		trig->GetInput(0).m_channel->GetHwname());
	PushSlope("TRIG:GLIT:POL", trig->GetType());
	PushCondition("TRIG:GLIT:QUAL", trig->GetCondition());
	PushFloat("TRIG:GLIT:LEV", trig->GetLevel());
	if(trig->GetCondition() == Trigger::CONDITION_BETWEEN)
	{
		m_transport->SendCommandQueued("TRIG:GLIT:RANG " +
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

/**
	@brief Send a trigger condition to the instrument

	@param path		SCPI path of the parameter to set
	@param cond		Trigger condition
 */
void AgilentOscilloscope::PushCondition(const string& path, Trigger::Condition cond)
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
	m_transport->SendCommandQueued(path + " " + cond_str);
}

/**
	@brief Send a floating-point value to the instrument

	@param path		SCPI path of the parameter to set
	@param f		The value to send
 */
void AgilentOscilloscope::PushFloat(const string& path, float f)
{
	m_transport->SendCommandQueued(path + " " + to_string_sci(f));
}

/**
	@brief Send a trigger slope to the instrument

	@param path		SCPI path of the parameter to set
	@param slope	The desired slope
 */
void AgilentOscilloscope::PushSlope(const string& path, EdgeTrigger::EdgeType slope)
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
	m_transport->SendCommandQueued(path + " " + slope_str);
}

/**
	@brief Send a trigger slope to the instrument

	@param path		SCPI path of the parameter to set
	@param slope	The desired slope
 */
void AgilentOscilloscope::PushSlope(const string& path, NthEdgeBurstTrigger::EdgeType slope)
{
	string slope_str;
	switch(slope)
	{
		case NthEdgeBurstTrigger::EDGE_RISING:
			slope_str = "POS";
			break;
		case NthEdgeBurstTrigger::EDGE_FALLING:
			slope_str = "NEG";
			break;
		default:
			return;
	}
	m_transport->SendCommandQueued(path + " " + slope_str);
}

vector<string> AgilentOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(EdgeTrigger::GetTriggerName());
	ret.push_back(PulseWidthTrigger::GetTriggerName());
	ret.push_back(NthEdgeBurstTrigger::GetTriggerName());
	return ret;
}
