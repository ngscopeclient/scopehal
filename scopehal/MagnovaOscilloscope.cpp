/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
 * Generic Magnova scope driver. Currently supports Batronix BMO models.
 */

#include "scopehal.h"
#include "MagnovaOscilloscope.h"
#include "base64.h"

#include "DropoutTrigger.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"
#include "GlitchTrigger.h"
#include "NthEdgeBurstTrigger.h"

#include <locale>
#include <stdarg.h>
#include <omp.h>
#include <thread>
#include <chrono>
#include <cinttypes>

using namespace std;

static const std::chrono::milliseconds c_trigger_delay(1000);	 // Delay required when forcing trigger

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MagnovaOscilloscope::MagnovaOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_digitalChannelCount(0)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_hasI2cTrigger(false)
	, m_hasSpiTrigger(false)
	, m_maxBandwidth(10000)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleRateValid(false)
	, m_sampleRate(1)
	, m_memoryDepthValid(false)
	, m_memoryDepth(1)
	, m_timebaseScaleValid(false)
	, m_timebaseScale(1)
	, m_triggerOffsetValid(false)
	, m_triggerOffset(0)
{
	//standard initialization
	FlushConfigCache();
	IdentifyHardware();
	DetectBandwidth();
	DetectAnalogChannels();
	DetectOptions();
	SharedCtorInit();

	//Figure out if scope is in low or high bit depth mode so we can download waveforms with the correct format
	GetADCMode(0);
}

string MagnovaOscilloscope::converse(const char* fmt, ...)
{
	string ret;
	char opString[128];
	va_list va;
	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	// Lock on transport at this level since magnova sometimes returns an \n before the actual reply
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	ret = m_transport->SendCommandQueuedWithReply(opString, false);
	if(ret.length() == 0)
		ret = m_transport->ReadReply(); // Sometimes the Magnova returns en empty string and then the actual reply
	return ret;
}

void MagnovaOscilloscope::sendOnly(const char* fmt, ...)
{
	char opString[128];
	va_list va;

	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	m_transport->SendCommandQueued(opString);
}

bool MagnovaOscilloscope::sendWithAck(const char* fmt, ...)
{
	string ret;
	char opString[128];
	va_list va;
	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

    std::string result(opString);
    result += ";*OPC?";

	// Lock on transport at this level since magnova sometimes returns an \n before the actual reply
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	ret = m_transport->SendCommandQueuedWithReply(result.c_str(), false);
	if(ret.length() == 0)
		ret = m_transport->ReadReply(); // Sometimes the Magnova returns en empty string and then the actual reply
	return (ret == "1");
}

void MagnovaOscilloscope::flush()
{
	m_transport->ReadReply();
}

void MagnovaOscilloscope::protocolError(bool flush, const char* fmt, va_list ap)
{
    char opString[128];
    vsnprintf(opString, sizeof(opString), fmt, ap);
    LogError("Protocol error%s: %s.\n", flush ? ", flushing read stream" : "", opString);
    if(flush) m_transport->ReadReply();
}

void MagnovaOscilloscope::protocolError(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    protocolError(false, fmt, ap);
    va_end(ap);
}

void MagnovaOscilloscope::protocolErrorWithFlush(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    protocolError(true, fmt, ap);
    va_end(ap);
}


void MagnovaOscilloscope::SharedCtorInit()
{
	//Add the external trigger input
	m_extTrigChannel =
		new OscilloscopeChannel(
			this,
			"EX",
			"",
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_TRIGGER,
			m_channels.size());
	m_channels.push_back(m_extTrigChannel);

	//Add the function generator output
	if(m_hasFunctionGen)
	{
		m_awgChannel = new FunctionGeneratorChannel(this, "AWG", "#ff00ffff", m_channels.size());
		m_channels.push_back(m_awgChannel);
		m_awgChannel->SetDisplayName("AWG");
	}
	else
		m_awgChannel = nullptr;

	//Clear the state-change register to we get rid of any history we don't care about
	PollTrigger();

	//Enable deduplication for vertical axis commands once we know what we're dealing with
	m_transport->DeduplicateCommand("OFFSET");
	m_transport->DeduplicateCommand("SCALE");
}

void MagnovaOscilloscope::ParseFirmwareVersion()
{
	//Check if version requires size workaround (1.3.9R6 and older)
	m_fwMajorVersion = 0;
	m_fwMinorVersion = 0;
	m_fwPatchVersion = 0;

	sscanf(m_fwVersion.c_str(), "%d.%d.%d",
		&m_fwMajorVersion,
		&m_fwMinorVersion,
		&m_fwPatchVersion);
	LogDebug("Found version %d.%d.%d\n",m_fwMajorVersion,m_fwMinorVersion,m_fwPatchVersion);
}

void MagnovaOscilloscope::IdentifyHardware()
{
	//Ask for the ID
	string reply = converse("*IDN?");
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

	//Look up model info
	m_modelid = MODEL_UNKNOWN;

	if(m_vendor.compare("Batronix") == 0)
	{
		if(m_model.compare("Magnova") == 0)
		{
			m_modelid = MODEL_MAGNOVA_BMO;
			ParseFirmwareVersion();
		}
		else
		{
			LogWarning("Model \"%s\" is unknown, available sample rates/memory depths may not be properly detected\n",
				m_model.c_str());
		}
	}
	else
	{
		LogWarning("Vendor \"%s\" is unknown\n", m_vendor.c_str());
	}
}

void MagnovaOscilloscope::DetectBandwidth()
{
	m_maxBandwidth = 0;
	switch(m_modelid)
	{
		case MODEL_MAGNOVA_BMO:
			m_maxBandwidth = 350;
			break;
		default:
			LogWarning("No bandwidth detected for model \"%s\".\n", m_vendor.c_str());
			break;
	}
}

void MagnovaOscilloscope::DetectOptions()
{	// No OPT command for now on Magnova
	// string options = converse("*OPT?");
	m_hasFunctionGen = true;
	m_hasLA = true;
	AddDigitalChannels(16);
}

/**
	@brief Creates digital channels for the oscilloscope
 */
void MagnovaOscilloscope::AddDigitalChannels(unsigned int count)
{
	m_digitalChannelCount = count;
	m_analogAndDigitalChannelCount = m_analogChannelCount + m_digitalChannelCount;
	m_digitalChannelBase = m_channels.size();

	char chn[32];
	for(unsigned int i = 0; i < count; i++)
	{
		snprintf(chn, sizeof(chn), "D%u", i);
		auto chan = new OscilloscopeChannel(
			this,
			chn,
			GetDefaultChannelColor(m_channels.size()),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_COUNTS),
			Stream::STREAM_TYPE_DIGITAL,
			m_channels.size());
		m_channels.push_back(chan);
		m_digitalChannels.push_back(chan);
	}
}

/**
	@brief Figures out how many analog channels we have, and add them to the device

 */
void MagnovaOscilloscope::DetectAnalogChannels()
{	// 4 Channels on Magnova scopes
	int nchans = 4;
	for(int i = 0; i < nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CH") + to_string(i+1);

		//Color the channels based on Magnova standard color sequence
		//yellow-pink-cyan-green-lightgreen
		string color = "#ffffff";
		switch(i % 4)
		{
			case 0:
				color = "#fbff00ff";
				break;

			case 1:
				color = "#f33404ff";
				break;

			case 2:
				color = "#0077ffff";
				break;

			case 3:
				color = "#04f810ff";
				break;
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
	}
	m_analogChannelCount = nchans;
	m_analogAndDigitalChannelCount = m_analogChannelCount + m_digitalChannelCount;
}

MagnovaOscilloscope::~MagnovaOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device information

string MagnovaOscilloscope::GetDriverNameInternal()
{
	return "magnova";
}

OscilloscopeChannel* MagnovaOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

void MagnovaOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if(m_trigger)
		delete m_trigger;
	m_trigger = NULL;

	m_channelVoltageRanges.clear();
	m_channelOffsets.clear();
	m_channelsEnabled.clear();
	m_channelDeskew.clear();
	m_channelDigitalThresholds.clear();
	m_probeIsActive.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_timebaseScaleValid = false;
	m_triggerOffsetValid = false;
	m_meterModeValid = false;
	m_awgEnabled.clear();
	m_awgDutyCycle.clear();
	m_awgRange.clear();
	m_awgOffset.clear();
	m_awgFrequency.clear();
	m_awgRiseTime.clear();
	m_awgFallTime.clear();
	m_awgShape.clear();
	m_awgImpedance.clear();
	m_adcModeValid = false;

	//Clear cached display name of all channels
	for(auto c : m_channels)
	{
		if(GetInstrumentTypesForChannel(c->GetIndex()) & Instrument::INST_OSCILLOSCOPE)
			c->ClearCachedDisplayName();
	}
}

/**
	@brief See what measurement capabilities we have
 */
unsigned int MagnovaOscilloscope::GetMeasurementTypes()
{
	unsigned int type = 0;
	return type;
}

/**
	@brief See what features we have
 */
unsigned int MagnovaOscilloscope::GetInstrumentTypes() const
{
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasFunctionGen)
		type |= INST_FUNCTION;
	return type;
}

uint32_t MagnovaOscilloscope::GetInstrumentTypesForChannel(size_t i) const
{
	if(m_awgChannel && (m_awgChannel->GetIndex() == i) )
		return Instrument::INST_FUNCTION;

	//If we get here, it's an oscilloscope channel
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool MagnovaOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//Early-out if status is in cache
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	//Analog
	if(i < m_analogChannelCount)
	{
		//See if the channel is enabled, hide it if not
		string reply;

		reply = converse(":CHAN%zu:STAT?", i + 1);
		{
			lock_guard<recursive_mutex> lock2(m_cacheMutex);
			m_channelsEnabled[i] = (reply.find("OFF") != 0);	//may have a trailing newline, ignore that
		}
	}
	else if(i < m_analogAndDigitalChannelCount)
	{
		//Digital => first check digital module is ON
		string module = converse(":DIG:STAT?");
		bool isOn = false;

		if(module == "ON")
		{	//See if the channel is on (digital channel numbers are 0 based)
			size_t nchan = i - m_analogChannelCount;
			string channel = converse(":DIG%zu:STAT?", nchan);
			isOn = (channel == "ON");
		}

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		// OFF can bee "SUPPORT_OFF" if all digital channels are off
		m_channelsEnabled[i] = isOn;
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	return m_channelsEnabled[i];
}

void MagnovaOscilloscope::EnableChannel(size_t i)
{
	bool wasInterleaving = IsInterleaving();

	//No need to lock the main mutex since sendOnly now pushes to the queue

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
	{
		sendWithAck(":CHAN%zu:STAT ON", i + 1);
	}
	else if(i < m_analogAndDigitalChannelCount)
	{
		//Digital channel (digital channel numbers are 0 based)
		sendWithAck(":DIG%d:STAT ON", i - m_analogChannelCount);
	}
	else if(i == m_extTrigChannel->GetIndex())
	{
		//Trigger can't be enabled
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelsEnabled[i] = true;

	//Sample rate and memory depth can change if interleaving state changed
	if(IsInterleaving() != wasInterleaving)
	{
		m_memoryDepthValid = false;
		m_timebaseScaleValid = false;
		m_sampleRateValid = false;
		m_triggerOffsetValid = false;
	}
}

bool MagnovaOscilloscope::CanEnableChannel(size_t i)
{
	// Can enable all channels except trigger
	return !(i == m_extTrigChannel->GetIndex());
}

void MagnovaOscilloscope::DisableChannel(size_t i)
{
	bool wasInterleaving = IsInterleaving();

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
	}

	if(i < m_analogChannelCount)
	{
		sendWithAck(":CHAN%zu:STAT OFF", i + 1);
	}
	else if(i < m_analogAndDigitalChannelCount)
	{
		//Digital channel
		//Disable this channel (digital channel numbers are 0 based)
		sendWithAck(":DIG%zu:STAT OFF", i - m_analogChannelCount);
	}
	else if(i == m_extTrigChannel->GetIndex())
	{
		//Trigger can't be enabled
	}

	//Sample rate and memory depth can change if interleaving state changed
	if(IsInterleaving() != wasInterleaving)
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_memoryDepthValid = false;
		m_timebaseScaleValid = false;
		m_sampleRateValid = false;
		m_triggerOffsetValid = false;
	}
}

vector<OscilloscopeChannel::CouplingType> MagnovaOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_MAGNOVA_BMO:
			ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
			ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
			ret.push_back(OscilloscopeChannel::COUPLE_GND);
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
	return ret;
}

OscilloscopeChannel::CouplingType MagnovaOscilloscope::GetChannelCoupling(size_t i)
{
	if(i >= m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	string replyType;
	string replyImp;

	m_probeIsActive[i] = false;

	replyType = Trim(converse(":CHAN%zu:COUP?", i + 1).substr(0, 2));
	replyImp = Trim(converse(":CHAN%zu:TERM?", i + 1).substr(0, 2));

	if(replyType == "AC")
		return (replyImp.find("ON") == 0) ? OscilloscopeChannel::COUPLE_AC_50 : OscilloscopeChannel::COUPLE_AC_1M;
	else if(replyType == "DC")
		return (replyImp.find("ON") == 0) ? OscilloscopeChannel::COUPLE_DC_50 : OscilloscopeChannel::COUPLE_DC_1M;
	else if(replyType == "GN")
		return OscilloscopeChannel::COUPLE_GND;

	//invalid
	protocolError("MagnovaOscilloscope::GetChannelCoupling got invalid coupling [%s] [%s]\n",
		replyType.c_str(),
		replyImp.c_str());
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void MagnovaOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(i >= m_analogChannelCount)
		return;

	//Get the old coupling value first.
	//This ensures that m_probeIsActive[i] is valid
	GetChannelCoupling(i);

	//If we have an active probe, don't touch the hardware config
	if(m_probeIsActive[i])
		return;

	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			sendOnly(":CHAN%zu:COUP AC", i + 1);
			sendOnly(":CHAN%zu:TERM OFF", i + 1);
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			sendOnly(":CHAN%zu:COUP DC", i + 1);
			sendOnly(":CHAN%zu:TERM OFF", i + 1);
			break;

		case OscilloscopeChannel::COUPLE_DC_50:
			sendOnly(":CHAN%zu:COUP DC", i + 1);
			sendOnly(":CHAN%zu:TERM ON", i + 1);
			break;

		case OscilloscopeChannel::COUPLE_AC_50:
			sendOnly(":CHAN%zu:COUP AC", i + 1);
			sendOnly(":CHAN%zu:TERM ON", i + 1);
			break;

		//treat unrecognized as ground
		case OscilloscopeChannel::COUPLE_GND:
		default:
			sendOnly(":CHAN%zu:COUP GND", i + 1);
			break;
	}
}

double MagnovaOscilloscope::GetChannelAttenuation(size_t i)
{
	if(i >= m_analogChannelCount)
		return 1;

	if(i == m_extTrigChannel->GetIndex())
		return 1;

	string reply;

	reply = converse(":CHAN%zu:DIV?", i + 1);

	int d;
	if(sscanf(reply.c_str(), "%d", &d) != 1)
	{
		protocolError("invalid channel attenuation value '%s'",reply.c_str());
	}
	return 1/d;
}

void MagnovaOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if(i >= m_analogChannelCount)
		return;

	if(atten <= 0)
		return;

	//Get the old coupling value first.
	//This ensures that m_probeIsActive[i] is valid
	GetChannelCoupling(i);

	//Don't allow changing attenuation on active probes
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_probeIsActive[i])
			return;
	}

	sendOnly(":CHAN%zu:DIV %d", i + 1, (int)(atten));
}

vector<unsigned int> MagnovaOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;

	switch(m_modelid)
	{
		case MODEL_MAGNOVA_BMO:
			ret.push_back(0);
			ret.push_back(20);
			ret.push_back(50);
			ret.push_back(100);
			ret.push_back(200);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	return ret;
}

unsigned int MagnovaOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i >= m_analogChannelCount)
		return 0;

	string reply;

	reply = converse(":CHAN%zu:FILT?", i + 1);
	if(reply == "NONe")
		return 0;
	else if(reply == "AMPLitude")
		return 0;
	else if(reply == "20000000")
		return 20;
	else if(reply == "50000000")
		return 50;
	else if(reply == "100000000")
		return 100;
	else if(reply == "200000000")
		return 200;

	protocolError("MagnovaOscilloscope::GetChannelBandwidthLimit got invalid bwlimit %s\n", reply.c_str());
	return 0;
}

void MagnovaOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	switch(limit_mhz)
	{
		case 0:
			sendOnly(":CHAN%zu:FILT NONe", i + 1);
			break;

		case 20:
			sendOnly(":CHAN%zu:FILT 20000000", i + 1);
			break;

		case 50:
			sendOnly(":CHAN%zu:FILT 50000000", i + 1);
			break;

		case 100:
			sendOnly(":CHAN%zu:FILT 100000000", i + 1);
			break;

		case 200:
			sendOnly(":CHAN%zu:FILT 200000000", i + 1);
			break;

		default:
			LogWarning("MagnovaOscilloscope::invalid bwlimit set request (%dMhz)\n", limit_mhz);
	}
}

bool MagnovaOscilloscope::CanInvert(size_t i)
{
	//All analog channels, and only analog channels, can be inverted
	return (i < m_analogChannelCount);
}

void MagnovaOscilloscope::Invert(size_t i, bool invert)
{
	if(i >= m_analogChannelCount)
		return;

	sendOnly(":CHAN%zu:INV %s", i + 1, invert ? "ON" : "OFF");
}

bool MagnovaOscilloscope::IsInverted(size_t i)
{
	if(i >= m_analogChannelCount)
		return false;

	string reply;

	reply = Trim(converse(":CHAN%zu:INV?", i + 1));
	return (reply == "ON");
}

void MagnovaOscilloscope::SetChannelDisplayName(size_t /* i */, string /* name */)
{
	// Not supported
	return;
}

string MagnovaOscilloscope::GetChannelDisplayName(size_t i)
{
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return "";
	// Not supported
	return chan->GetHwname();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

bool MagnovaOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

Oscilloscope::TriggerMode MagnovaOscilloscope::PollTrigger()
{
	//Read the Internal State Change Register
	string sinr = "";

	if(m_triggerForced)
	{
		// The force trigger completed, return the sample set
		m_triggerForced = false;
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	sinr = converse(":STAT?");

	//No waveform, but ready for one?
	/*if((sinr == "WAITing")||(sinr == "RUNNing"))
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}*/

	if((sinr == "TRIGgered"))
	{	// Magnova returns TRIGgered status during Single acquisition, we need to wait for STOPped
		return TRIGGER_MODE_RUN;
	}

	//Stopped, no data available
	if(sinr == "STOPped")
	{
		if(m_triggerArmed)
		{
			//Only mark the trigger as disarmed if this was a one-shot trigger.
			//If this is a repeating trigger, we're still armed from the client's perspective,
			//since AcquireData() will reset the trigger for the next acquisition.
			if(m_triggerOneShot)
				m_triggerArmed = false;

			return TRIGGER_MODE_TRIGGERED;
		}
		else
			return TRIGGER_MODE_STOP;
	}
	return TRIGGER_MODE_RUN;
}

std::optional<MagnovaOscilloscope::Metadata> MagnovaOscilloscope::parseMetadata(
    const std::vector<uint8_t>& data) {
    
    try {
        Metadata metadata;

        // Helper function to read little-endian float
        auto readFloat = [](const uint8_t* ptr) -> float {
            uint32_t value = static_cast<uint32_t>(ptr[0]) |
                           (static_cast<uint32_t>(ptr[1]) << 8) |
                           (static_cast<uint32_t>(ptr[2]) << 16) |
                           (static_cast<uint32_t>(ptr[3]) << 24);
            float result;
            std::memcpy(&result, &value, sizeof(float));
            return result;
        };

        // Helper function to read little-endian uint32
        auto readUint32 = [](const uint8_t* ptr) -> uint32_t {
            return static_cast<uint32_t>(ptr[0]) |
                   (static_cast<uint32_t>(ptr[1]) << 8) |
                   (static_cast<uint32_t>(ptr[2]) << 16) |
                   (static_cast<uint32_t>(ptr[3]) << 24);
        };

        // Read metadata in the correct order
        const uint8_t* ptr = data.data();

		// First three floats
		float time_delta = readFloat(ptr);
		ptr += sizeof(float);
		float start_time = readFloat(ptr);
		ptr += sizeof(float);
		float end_time = readFloat(ptr);
		ptr += sizeof(float);

		// Next two uint32s
		uint32_t sample_start = readUint32(ptr);
		ptr += sizeof(uint32_t);
		uint32_t sample_length = readUint32(ptr);
		ptr += sizeof(uint32_t);

		// Next two floats
		float vertical_start = readFloat(ptr);
		ptr += sizeof(float);
		float vertical_step = readFloat(ptr);
		ptr += sizeof(float);

		// Final uint32
		uint32_t sample_count = readUint32(ptr);

		// Assign all values
		metadata.timeDelta = time_delta;
		metadata.startTime = start_time;
		metadata.endTime = end_time;
		metadata.sampleStart = sample_start;
		metadata.sampleLength = sample_length;
		metadata.verticalStart = vertical_start;
		metadata.verticalStep = vertical_step;
		metadata.sampleCount = sample_count;

		return metadata;
    }
    catch (const std::exception& e) {
		protocolError("Error parsing metadata: %s.\n", e.what());
		return std::nullopt;
    }
}

size_t MagnovaOscilloscope::ReadWaveformBlock(std::vector<uint8_t>* data, std::function<void(float)> progress)
{
	//Read and discard data until we see the '#'
	uint8_t tmp;
	for(int i=0; i<20; i++)
	{
		m_transport->ReadRawData(1, &tmp);
		if(tmp == '#')
			break;

		//shouldn't ever get here
		if(i == 19)
		{	// This is a protocol error, flush pending rx data
			protocolErrorWithFlush("ReadWaveformBlock: threw away 20 bytes of data and never saw a '#'\n");
			// Stop aqcuisition after this protocol error
			Stop();
			return 0;
		}
	}

	//Read length of the length field
	m_transport->ReadRawData(1, &tmp);
	int lengthOfLength = tmp - '0';

	//Read the actual length field
	char textlen[10] = {0};
	m_transport->ReadRawData(lengthOfLength, (unsigned char*)textlen);
	uint32_t len = atoi(textlen);

	size_t readBytes = 0;
	data->resize(len);
	uint8_t* resultData = data->data();
	while(readBytes < len)
	{
		size_t newBytes = m_transport->ReadRawData(len-readBytes,resultData+readBytes,progress);
		if(newBytes == 0) break;
		readBytes += newBytes;
	}
	//LogDebug("Got length %zu from scope, expected bytes = %" PRIu32 ".\n",readBytes, len);

	return readBytes;
}

/**
	@brief Optimized function for checking channel enable status en masse with less round trips to the scope
 */
void MagnovaOscilloscope::BulkCheckChannelEnableState()
{
	vector<unsigned int> uncached;

	bool hasUncachedDigital = false;
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		//Check enable state in the cache.
		for(unsigned int i = 0; i < m_analogAndDigitalChannelCount; i++)
		{
			if(m_channelsEnabled.find(i) == m_channelsEnabled.end())
			{
				uncached.push_back(i);
				if(i >= m_analogChannelCount) hasUncachedDigital = true;
			}
		}
	}

	bool digitalModuleOn = false;
	if(hasUncachedDigital)
	{	//Digital => first check digital module is ON
		string module = converse(":DIG:STAT?");
		digitalModuleOn = (module == "ON");
	}
	for(auto i : uncached)
	{
		bool enabled;
		if((i < m_analogChannelCount))
		{	// Analog
			enabled = (converse(":CHAN%zu:STAT?", i + 1) == "ON");
		}
		else
		{	// Digital
			enabled = digitalModuleOn && (converse(":DIG%zu:STAT?", (i - m_analogChannelCount)) == "ON");
		}
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = enabled;
	}
}

/**
	@brief Returns the number of active analog channels and digital probes to determine Memory Depth available per channel
 */
unsigned int MagnovaOscilloscope::GetActiveChannelsCount()
{
	BulkCheckChannelEnableState();
	unsigned int result = 0;
	for(unsigned int i = 0; i <  m_analogChannelCount; i++)
	{	// Check all analog channels
		if(IsChannelEnabled(i)) result++;
	}
	bool probe0to7Active = false;
	bool probe8to15Active = false;
	unsigned int halfDigitalChannels = m_digitalChannelCount/2;
	for(unsigned int i = 0; i <  halfDigitalChannels; i++)
	{	// Check digital channels for bank1
		if(IsChannelEnabled(i+m_analogChannelCount)) probe0to7Active = true;
	}
	for(unsigned int i = halfDigitalChannels; i <  m_digitalChannelCount; i++)
	{	// Check digital channels for bank2
		if(IsChannelEnabled(i+m_analogChannelCount)) probe8to15Active = true;
	}
	if(probe0to7Active) result++;
	if(probe8to15Active) result++;
	return result;
}

/**
	@brief Returns true if the scope is in reduced sample rate
 */
bool MagnovaOscilloscope::IsReducedSampleRate()
{
	// ADC sample rate 1.6 GSa/s if
	// - only channel 1 and/or 2 are active
	// - only channel 1 or 2 and one digital probe are active
	// - only one or two digital probes are active
	// - only channel 1 and/or 2 are active plus one or two digital probes are active and time scale is ≤ 20 ns/div

	// ADC sample rate 1.0 GSa/s if
	// - Channel 3 and/or 4 are active
	// - The number of analog channels plus digital probes is 3 or more and time scale is > 20 ns/div.
	
	unsigned int activeChannels = GetActiveChannelsCount();
	if(IsChannelEnabled(2) || IsChannelEnabled(3))
	{	// Reduced if channel 3 or 4 is active
		return true;
	}
	else if(activeChannels >= 3)
	{	// Need to checm time scale
		uint64_t nsPerDiv = llround(GetTimebaseScale()*FS_PER_SECOND)/FS_PER_NANOSECOND;
		return nsPerDiv > 20;
	}
	return false;
}


/**
	@brief Returns the max memory depth for auto mode
 */
uint64_t MagnovaOscilloscope::GetMaxAutoMemoryDepth()
{
	if(m_memodyDepthMode == MEMORY_DEPTH_AUTO_FAST)
	{	// In fast mode, depth is limited to 20 Mpts
		return 20*1000*1000;
	}
	unsigned int activeChannels = GetActiveChannelsCount();
	if(activeChannels <= 1)
	{
		return 300*1000*1000;
	}
	else if(activeChannels == 2)
	{
		return 150*1000*1000;
	}
	else if(activeChannels == 3 || activeChannels == 4)
	{
		return 60*1000*1000;
	}
	else
	{
		return 30*1000*1000;
	}
}



time_t MagnovaOscilloscope::ExtractTimestamp(const std::string& timeString, double& basetime)
{
	//Timestamp returned by Magnova has the form 'hh,mm,ss.ssssss'
    int hh, mm;
    double ss;

	string input = timeString;
    for (char &c : input) {
        if (c == ',') c = ' ';
    }

    std::stringstream ssin(input);
    ssin >> hh >> mm >> ss;

	uint8_t seconds = floor(ss);
	basetime = ss - seconds;
	time_t tnow = time(NULL);
	struct tm tstruc;

#ifdef _WIN32
	localtime_s(&tstruc, &tnow);
#else
	localtime_r(&tnow, &tstruc);
#endif
	tstruc.tm_hour = hh;
	tstruc.tm_min = mm;
	tstruc.tm_sec = seconds;

    /* char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tstruc);
	LogDebug("Found time : %s\n",buffer);*/
	return mktime(&tstruc);
}

/**
	@brief Converts 16-bit ADC samples to floating point
 */
void MagnovaOscilloscope::Convert16BitSamples(float* pout, const uint16_t* pin, float gain, float offset, size_t count)
{
	//Divide large waveforms (>1M points) into blocks and multithread them
	if(count > 1000000)
	{
		//Round blocks to multiples of 64 samples for clean vectorization
		size_t numblocks = omp_get_max_threads();
		size_t lastblock = numblocks - 1;
		size_t blocksize = count / numblocks;
		blocksize = blocksize - (blocksize % 64);

		#pragma omp parallel for
		for(size_t i=0; i<numblocks; i++)
		{
			//Last block gets any extra that didn't divide evenly
			size_t nsamp = blocksize;
			if(i == lastblock)
				nsamp = count - i*blocksize;

			size_t off = i*blocksize;
			Convert16BitSamplesGeneric(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
		}
	}

	//Small waveforms get done single threaded to avoid overhead
	else
	{
		Convert16BitSamplesGeneric(pout, pin, gain, offset, count);
	}
}

/**
	@brief Converts raw ADC samples to floating point
 */
void MagnovaOscilloscope::Convert16BitSamplesGeneric(float* pout, const uint16_t* pin, float gain, float offset, size_t count)
{
	for(size_t j=0; j<count; j++)
		pout[j] = gain*pin[j] - offset;
}


vector<WaveformBase*> MagnovaOscilloscope::ProcessAnalogWaveform(
	const std::vector<uint8_t>& data,
	size_t datalen,
	uint32_t num_sequences,
	time_t ttime,
	double basetime,
	double* wavetime,
	int ch)
{
	vector<WaveformBase*> ret;

	auto metadata = parseMetadata(data);
	if(!metadata)
	{
		LogError("Could not parse metadta.\n");
		return ret;
	}

	// Get gain from vertical step
	float v_gain = metadata->verticalStep/0xFFFF;

	// Get offset from vertical start + add channel offset
	float v_off = (0 - metadata->verticalStart/* - GetChannelOffset(ch,0)*/);

	// Get interval from timedelta
	float interval = metadata->timeDelta * FS_PER_SECOND;

	// Offset is null
	//double h_off = 0;
	double h_off_frac = 0;


	//Raw waveform data
	size_t num_samples = metadata->sampleCount;

	// Sanity check datalength consistency
	size_t actual_num_samples = (datalen-32)/2;
	if(num_samples != actual_num_samples)
	{
		protocolError("Invlaid sample count from metadata: found %zu, expected %zu.\n",num_samples,actual_num_samples);
		num_samples = min(num_samples,actual_num_samples);
	}

	size_t num_per_segment = num_samples / num_sequences;

	// Skip metadata
    const uint8_t* ptr = data.data() + 32;
	const uint16_t* wdata = reinterpret_cast<const uint16_t*>(ptr);

	// float codes_per_div;

	/*LogDebug("\nV_Gain=%f, V_Off=%f, interval=%f, h_off=%f, h_off_frac=%f, datalen=%zu, basetime=%f\n",
		v_gain,
		v_off,
		interval,
		h_off,
		h_off_frac,
		datalen,
		basetime);*/

	for(size_t j = 0; j < num_sequences; j++)
	{
		//Set up the capture we're going to store our data into
		auto cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(ch)->GetHwname());
		cap->m_timescale = round(interval);

		cap->m_triggerPhase = h_off_frac;
		cap->m_startTimestamp = ttime;

		//Parse the time
		if(num_sequences > 1)
			cap->m_startFemtoseconds = static_cast<int64_t>((basetime + wavetime[j * 2]) * FS_PER_SECOND);
		else
			cap->m_startFemtoseconds = static_cast<int64_t>(basetime * FS_PER_SECOND);

		cap->Resize(num_per_segment);
		cap->PrepareForCpuAccess();

		//Convert raw ADC samples to volts
		Convert16BitSamples(
			cap->m_samples.GetCpuPointer(),
			(wdata + j * num_per_segment),
			v_gain,
			v_off,
			num_per_segment);

		cap->MarkSamplesModifiedFromCpu();
		ret.push_back(cap);
	}

	return ret;
}

vector<SparseDigitalWaveform*> MagnovaOscilloscope::ProcessDigitalWaveform(
	const std::vector<uint8_t>& data,
	size_t datalen,
	uint32_t num_sequences,
	time_t ttime,
	double basetime,
	double* wavetime,
	int /*ch*/)
{
	vector<SparseDigitalWaveform*> ret;

	auto metadata = parseMetadata(data);
	if(!metadata)
	{
		LogError("Could not parse metadta.\n");
		return ret;
	}


	//cppcheck-suppress invalidPointerCast
	float interval = metadata->timeDelta * FS_PER_SECOND;

	//Raw waveform data
	size_t numSamples = datalen*8;

	//LogTrace("\nDigital, interval=%f, datalen=%zu\n", interval,	datalen);

	//Get the client's local time.
	//All we need from this is to know whether DST is active
	tm now;
	time_t tnow;
	time(&tnow);
	localtime_r(&tnow, &now);

	// Sample ratio between digital and analog
	// TODO
	int64_t digitalToAnalogSampleRatio = 1; //m_acqPoints / m_digitalAcqPoints;

	//We have each channel's data from start to finish before the next (no interleaving).
	for(size_t numSeq = 0; numSeq < num_sequences; numSeq++)
	{
		SparseDigitalWaveform* cap = new SparseDigitalWaveform;
		// Since the LA sample rate is a fraction of the sample rate of the analog channels, timescale needs to be updated accordingly
		cap->m_timescale = round(interval)*digitalToAnalogSampleRatio;
		cap->PrepareForCpuAccess();

		//Capture timestamp
		cap->m_startTimestamp = ttime;
		//Parse the time
		if(num_sequences > 1)
			cap->m_startFemtoseconds = static_cast<int64_t>((basetime + wavetime[numSeq * 2]) * FS_PER_SECOND);
		else
			cap->m_startFemtoseconds = static_cast<int64_t>(basetime * FS_PER_SECOND);

		//Preallocate memory assuming no deduplication possible
		cap->Resize(numSamples);

		size_t k = 0;
		size_t sampleIndex = 0;
		bool sampleValue = false;
		bool lastSampleValue = false;


		// Skip metadata
		const uint8_t* ptr = data.data() + 32;
		//Read and de-duplicate the other samples
		const uint8_t* rawData = ptr;
		for (size_t curByteIndex = 0; curByteIndex < datalen; curByteIndex++)
		{
			char samples = rawData[curByteIndex];
			for (int ii = 0; ii < 8; ii++, samples >>= 1)
			{	// Check if the current scope sample bit is set.
				sampleValue = (samples & 0x1);
				if((sampleIndex > 0) && (lastSampleValue == sampleValue) && ((sampleIndex + 3) < numSamples))
				{	//Deduplicate consecutive samples with same value
					cap->m_durations[k]++;
				}
				else
				{	//Nope, it toggled - store the new value
					cap->m_offsets[k] = sampleIndex;
					cap->m_durations[k] = 1;
					cap->m_samples[k] = sampleValue;
					lastSampleValue = sampleValue;
					k++;
				}
				sampleIndex++;
			}
		}

		//Done, shrink any unused space
		cap->Resize(k);
		cap->m_offsets.shrink_to_fit();
		cap->m_durations.shrink_to_fit();
		cap->m_samples.shrink_to_fit();
		cap->MarkSamplesModifiedFromCpu();
		cap->MarkTimestampsModifiedFromCpu();

		//See how much space we saved
		//LogDebug("%zu samples deduplicated to %zu (%.1f %%)\n",	numSamples,	k, (k * 100.0f) / (numSamples));

		//Done, save data and go on to next
		ret.push_back(cap);
	}
	return ret;
}

bool MagnovaOscilloscope::AcquireData()
{
	// Transfer buffers
	std::vector<uint8_t> analogWaveformData[MAX_ANALOG];
	int analogWaveformDataSize[MAX_ANALOG] {0};
	std::vector<uint8_t> digitalWaveformDataBytes[MAX_DIGITAL];
	int digitalWaveformDataSize[MAX_DIGITAL] {0};

	//State for this acquisition (may be more than one waveform)
	uint32_t num_sequences = 1;
	map<int, vector<WaveformBase*>> pending_waveforms;
	double start = 0;
	time_t ttime = 0;
	double basetime = 0;
	vector<vector<WaveformBase*>> waveforms;
	vector<vector<SparseDigitalWaveform*>> digitalWaveforms;
	bool analogEnabled[MAX_ANALOG] = {false};
	bool digitalEnabled[MAX_DIGITAL] = {false};
	bool anyDigitalEnabled = false;
	double* pwtime = NULL;

	//Acquire the data (but don't parse it)

	// Get instrument time : format "23,35,11.280010"
	string isntrumentTime = converse(":SYST:TIME?");

	// Detect active channels
	BulkCheckChannelEnableState();
	for(unsigned int i = 0; i <  m_analogChannelCount; i++)
	{	// Check all analog channels
		analogEnabled[i] = IsChannelEnabled(i);
	}
	for(unsigned int i = 0; i <  m_digitalChannelCount; i++)
	{	// Check digital channels
		// Not supported for now by Magnova firmware
		/*digitalEnabled[i] = IsChannelEnabled(i+m_analogChannelCount);
		anyDigitalEnabled |= digitalEnabled[i];*/
		digitalEnabled[i] = false;
	}

	// Notify about download operation start
	ChannelsDownloadStarted();

	
	{	// Lock transport from now during all acquisition phase
		lock_guard<recursive_mutex> lock(m_transport->GetMutex());
		start = GetTime();

		// Get time from instrument
		ttime = ExtractTimestamp(isntrumentTime, basetime);

		//Read the data from each analog waveform
		for(unsigned int i = 0; i < m_analogChannelCount; i++)
		{
			if(analogEnabled[i])
			{	// Allocate buffer
				// Run the same loop for paginated and unpagnated mode, if unpaginated we will run it only once
				m_transport->SendCommand(":CHAN" + to_string(i + 1) + ":DATA:PACK? ALL,RAW");
				size_t readBytes = ReadWaveformBlock(&analogWaveformData[i], [i, this] (float progress) { ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, progress); });
				analogWaveformDataSize[i] = readBytes;
				ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
			}
		}
		if(anyDigitalEnabled)
		{
			// uint64_t digitalAcqPoints = GetDigitalAcqPoints();
			// uint64_t acqDigitalBytes = ceil(digitalAcqPoints/8); // 8 points per byte on digital channels
			// LogDebug("Digital acq : ratio = %lld, pages = %lld, page size = %lld , dig acq points = %lld, acq dig bytes = %lld.\n",(acqPoints / digitalAcqPoints),pages, pageSize,digitalAcqPoints, acqDigitalBytes);
			//Read the data from each digital waveform
			for(size_t i = 0; i < m_digitalChannelCount; i++)
			{
				if(digitalEnabled[i])
				{	// Allocate buffer
					m_transport->SendCommand(":DIG" + to_string(i + 1) + ":DATA:PACK? ALL,RAW");
					size_t readBytes = ReadWaveformBlock(&digitalWaveformDataBytes[i], [i, this] (float progress) { ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, progress); });
					digitalWaveformDataSize[i] = readBytes;
					ChannelsDownloadStatusUpdate(i + m_analogChannelCount, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
				}
			}
		}

		//At this point all data has been read so the scope is free to go do its thing while we crunch the results.
		//Re-arm the trigger if not in one-shot mode
		if(!m_triggerOneShot)
		{
			//LogDebug("Arming trigger for next acquisition!\n");
			sendOnly(":SINGLE");
			m_triggerArmed = true;
		}
		else
		{
			sendWithAck(":STOP");
			m_triggerArmed = false;
		}
	}

	//Process analog waveforms
	waveforms.resize(m_analogChannelCount);
	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if(analogEnabled[i])
		{
			waveforms[i] = ProcessAnalogWaveform(
				analogWaveformData[i],
				analogWaveformDataSize[i],
				num_sequences,
				ttime,
				basetime,
				pwtime,
				i);
		}
	}

	//Save analog waveform data
	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if(!analogEnabled[i])
			continue;

		//Done, update the data
		for(size_t j = 0; j < num_sequences; j++)
			pending_waveforms[i].push_back(waveforms[i][j]);
	}

	//Process digital waveforms
	digitalWaveforms.resize(m_digitalChannelCount);
	for(unsigned int i = 0; i < m_digitalChannelCount; i++)
	{
		if(digitalEnabled[i])
		{
			digitalWaveforms[i] = ProcessDigitalWaveform(
				digitalWaveformDataBytes[i],
				digitalWaveformDataSize[i],
				num_sequences,
				ttime,
				basetime,
				pwtime,
				i);
		}
	}

	//Save digital waveform data
	for(unsigned int i = 0; i < m_digitalChannelCount; i++)
	{
		if(!digitalEnabled[i])
			continue;

		//Done, update the data
		for(size_t j = 0; j < num_sequences; j++)
			pending_waveforms[i+m_analogChannelCount].push_back(digitalWaveforms[i][j]);
	}

	// Tell the download monitor that waveform download has finished
	ChannelsDownloadFinished();

	for(int i=0; i<MAX_ANALOG; i++)
	{	// Free memory
		analogWaveformData[i] = {};
	}
	for(int i=0; i<MAX_DIGITAL; i++)
	{	// Free memory
		digitalWaveformDataBytes[i] = {};
	}

	
	{	//Now that we have all of the pending waveforms, save them in sets across all channels
		lock_guard<mutex> lock(m_pendingWaveformsMutex);
		for(size_t i = 0; i < num_sequences; i++)
		{
			SequenceSet s;
			for(size_t j = 0; j < m_analogAndDigitalChannelCount; j++)
			{
				if(pending_waveforms.find(j) != pending_waveforms.end())
					s[GetOscilloscopeChannel(j)] = pending_waveforms[j][i];
			}
			m_pendingWaveforms.push_back(s);
		}
	}

	double dt = GetTime() - start;
	LogTrace("Waveform download and processing took %.3f ms\n", dt * 1000);
	return true;
}

void MagnovaOscilloscope::PrepareAcquisition()
{	// Make sure everything is up to date
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_timebaseScaleValid = false;
	m_triggerOffsetValid = false;
	m_channelOffsets.clear();
}

void MagnovaOscilloscope::Start()
{
	PrepareAcquisition();
	sendOnly(":STOP;:SINGLE");	 //always do single captures, just re-trigger

	//LogDebug("Arming trigger for acquisition start!\n");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void MagnovaOscilloscope::StartSingleTrigger()
{
	//LogDebug("Start single trigger\n");

	PrepareAcquisition();
	sendOnly(":STOP;:SINGLE");

	//LogDebug("Arming trigger for single acquisition!\n");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void MagnovaOscilloscope::Stop()
{
	if(!m_triggerArmed)
		return;

	m_transport->SendCommandImmediate(":STOP");

	m_triggerArmed = false;
	m_triggerOneShot = true;

	//Clear out any pending data (the user doesn't want it, and we don't want stale stuff hanging around)
	ClearPendingWaveforms();
}

void MagnovaOscilloscope::ForceTrigger()
{
	// Don't allow more than one force at a time
	if(m_triggerForced)
		return;

	m_triggerForced = true;

	PrepareAcquisition();
	sendOnly(":SINGLE");
	if(!m_triggerArmed)
		sendOnly(":SINGLE");

	//LogDebug("Arming trigger for forced acquisition!\n");
	m_triggerArmed = true;
	this_thread::sleep_for(c_trigger_delay);
}

float MagnovaOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	//not meaningful for trigger or digital channels
	if(i >= m_analogChannelCount)
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	string reply;

	reply = converse(":CHAN%zu:OFFSET?", i + 1);

	float offset;
	if(sscanf(reply.c_str(), "%f", &offset) != 1)
	{
		protocolError("invalid channel offset value '%s'",reply.c_str());
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void MagnovaOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	//not meaningful for trigger or digital channels
	if(i >= m_analogChannelCount)
		return;

	sendWithAck(":CHAN%zu:OFFSET %1.2E", i + 1, offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

float MagnovaOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	//not meaningful for trigger or digital channels
	if(i >= m_analogChannelCount)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	string reply;

	reply = converse(":CHAN%zu:SCALE?", i + 1);

	float volts_per_div;
	if(sscanf(reply.c_str(), "%f", &volts_per_div)!=1)
	{
		protocolError("invalid channel vlotage range value '%s'",reply.c_str());
	}

	float v = volts_per_div * 8;	//plot is 8 divisions high
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = v;
	return v;
}

void MagnovaOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	// Only for analog channels
	if(i >= m_analogChannelCount)
		return;

	float vdiv = range / 8;

	sendWithAck(":CHAN%zu:SCALE %.4f", i + 1, vdiv);

	//Don't update the cache because the scope is likely to round the value
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelVoltageRanges.erase(i);
}

vector<uint64_t> MagnovaOscilloscope::GetSampleRatesNonInterleaved()
{
	const uint64_t k = 1000;
	const uint64_t m = k*k;

	vector<uint64_t> ret;
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_MAGNOVA_BMO:
			// Call GetSampleDepth to update Memody Depth Mode 
			GetSampleDepth();
			if(m_memodyDepthMode == MEMORY_DEPTH_AUTO_MAX)
			{	// In auto modes reduce possible values to the one that match sample depth / coarse time scale
				if(IsReducedSampleRate())
				{
					ret = {25, 50, 100, 250, 500, 1*k, 2500, 5*k, 10*k, 25*k, 50*k, 100*k, 250*k, 500*k, 1*m, 2500*k, 5*m, 10*m, 25*m, 50*m, 125*m, 250*m, 500*m, 1000*m };
				}
				else
				{
					ret = {50, 100, 250, 500, 1*k, 2500, 5*k, 10*k, 25*k, 50*k, 100*k, 250*k, 500*k, 1*m, 2500*k, 5*m, 10*m, 25*m, 50*m, 100*m, 200*m, 400*m, 800*m, 1600*m };
				}
			}
			else if(m_memodyDepthMode == MEMORY_DEPTH_AUTO_FAST)
			{	// In auto modes reduce possible values to the one that match sample depth / coarse time scale
				if(IsReducedSampleRate())
				{
					ret = {2, 5, 10, 40, 50, 100, 400, 500, 1*k, 4*k, 5*k, 10*k, 40*k, 50*k, 100*k, 400*k, 500*k, 1*m, 2500*k, 5*m, 10*m, 25*m, 50*m, 125*m, 250*m, 500*m, 1000*m };
				}
				else
				{
					ret = {2, 5, 10, 40, 50, 100, 400, 500, 1*k, 4*k, 5*k, 10*k, 40*k, 50*k, 100*k, 400*k, 500*k, 1*m, 4*m, 5*m, 10*m, 40*m, 50*m, 100*m, 400*m, 800*m, 1600*m };
				}
			}
			else
			{	// All possible values
				ret = {1, 2, 4, 5, 10, 20, 25, 40, 50, 100, 200, 250, 400, 500, 1*k, 2*k, 2500, 4*k, 5*k, 10*k, 20*k, 25*k, 40*k, 50*k, 100*k, 200*k, 250*k, 400*k, 500*k, 1*m, 2*m, 2500*k, 4*m, 5*m, 10*m, 20*m, 25*m, 40*m, 50*m, 100*m, 125*m, 200*m, 250*m, 400*m, 500*m, 800*m, 1000*m, 1600*m };
			}
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	return ret;
}

vector<uint64_t> MagnovaOscilloscope::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

vector<uint64_t> MagnovaOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	// Memory depth can either be "Fixed" or "Auto" according to the scope's configuration
	// Let's check mode by getting memory depth value
	GetSampleDepth();
	switch(m_memodyDepthMode)
	{
		case MEMORY_DEPTH_AUTO_MAX:
		case MEMORY_DEPTH_AUTO_FAST:
			// In auto mode, memory depth can be (as tested on the scope, only for Extended Capture mode) :
			if(IsReducedSampleRate())
			{
				ret = {39, 42, 48, 60, 120, 240, 480, 1200, 2400, 4800, 12000, 24000, 48000, 120000, 240000, 480000, 1200000, 2400000, 4800000, 9600000, 12000000, 15000000, 19200000, 24000000, 30000000, 48000000, 60000000, 120000000, 150000000};
			}
			else
			{
				ret = {40, 46, 56, 77, 192, 384, 768, 1920, 3840, 7680, 19200, 38400, 76800, 192000, 384000, 768000, 1920000, 3840000, 7680000, 12000000, 19200000, 30000000, 38400000, 60000000, 76800000, 120000000, 150000000, 192000000, 240000000, 300000000};
			}
			break;
		case MEMORY_DEPTH_FIXED:
		default:
			// In fixed mode, sample depths depend on the number of active analog channels and digital probes :
			// 1 analog channel or digital probe: 327.2 Mpts
			// 2 analog channels / digital probes: 163.6 Mpts per channel
			// 3-4 analog channels / digital probes: 81.8 Mpts per channel
			// ≥ 5 analog channels / digital probes: 40.9 Mpts per channel
			unsigned int activeChannels = GetActiveChannelsCount();

			if(activeChannels <= 1)
			{
				ret = {20 * 1000, 50 * 1000, 100 * 1000, 200 * 1000, 500 * 1000, 1000 * 1000, 2000 * 1000, 5000 * 1000, 10 * 1000 * 1000, 20 * 1000 * 1000, 50 * 1000 * 1000, 100 * 1000 * 1000, 200 * 1000 * 1000, 327151616};
			}
			else if(activeChannels == 2)
			{
				ret = {10 * 1000, 25 * 1000, 50 * 1000, 100 * 1000, 250 * 1000, 500 * 1000, 1000 * 1000, 2500 * 1000, 5 * 1000 * 1000, 10 * 1000 * 1000, 25 * 1000 * 1000, 50 * 1000 * 1000, 100 * 1000 * 1000, 163575808};
			}
			else if(activeChannels == 3 || activeChannels == 4)
			{
				ret = {5 * 1000, 12500 , 25 * 1000, 50 * 1000, 125 * 1000, 250 * 1000, 500 * 1000, 1250 * 1000, 2500 * 1000, 5 * 1000 * 1000, 12500 * 1000, 25 * 1000 * 1000, 50 * 1000 * 1000, 81787904};
			}
			else
			{
				ret = {2500, 6250 , 12500, 25 * 1000, 62500, 125 * 1000, 250 * 1000, 625 * 1000, 1250 * 1000, 2500 * 1000, 6250 * 1000, 12500 * 1000, 25 * 1000 * 1000, 40893952};
			}
			break;
	}
	return ret;
}

vector<uint64_t> MagnovaOscilloscope::GetSampleDepthsInterleaved()
{
	return GetSampleDepthsNonInterleaved();
}

set<MagnovaOscilloscope::InterleaveConflict> MagnovaOscilloscope::GetInterleaveConflicts()
{
	set<InterleaveConflict> ret;

	switch(m_modelid)
	{
		// Magnova BMO interleaves if any of channel 3 or 4 is active
		case MODEL_MAGNOVA_BMO:
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(0), GetOscilloscopeChannel(2)));
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(0), GetOscilloscopeChannel(3)));
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(1), GetOscilloscopeChannel(2)));
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(1), GetOscilloscopeChannel(3)));
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	return ret;
}

uint64_t MagnovaOscilloscope::GetSampleRate()
{
    {
        lock_guard<recursive_mutex> lock(m_cacheMutex);
        if(m_sampleRateValid)
            return m_sampleRate;
    }
	double f;
	string reply;
	reply = converse(":ACQUIRE:SRATE?");

    lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(sscanf(reply.c_str(), "%lf", &f) == 1)
	{
		m_sampleRate = static_cast<int64_t>(f);
		m_sampleRateValid = true;
	}
	else
	{
		protocolError("invalid sample rate value '%s'",reply.c_str());
	}
	return m_sampleRate;
}

/**
 * Returns the timebase scale in s
 */
double MagnovaOscilloscope::GetTimebaseScale()
{
    {
        lock_guard<recursive_mutex> lock(m_cacheMutex);
        if(m_timebaseScaleValid)
            return m_timebaseScale;
    }
	double scale = stod(converse(":TIMebase:SCALe?"));
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_timebaseScale = scale;
	m_timebaseScaleValid = true;
	return m_timebaseScale;
}


uint64_t MagnovaOscilloscope::GetSampleDepth()
{
    {
        lock_guard<recursive_mutex> lock(m_cacheMutex);
        if(m_memoryDepthValid)
            return m_memoryDepth;
    }
	double f;
	// Possible values are : AUTo, AFASt, Integer in pts
	string reply = converse(":ACQUIRE:MDEPTH?");
	MemoryDepthMode mode =  (reply == "AUTo") ? MEMORY_DEPTH_AUTO_MAX : (reply == "AFASt") ? MEMORY_DEPTH_AUTO_FAST : MEMORY_DEPTH_FIXED;
	uint64_t depth;
	switch(mode)
	{
		case MEMORY_DEPTH_AUTO_MAX:
		case MEMORY_DEPTH_AUTO_FAST:
			{
			// Get Sample depth based on srate and timebase
			// Auto (Max): Memory length = recording time * sample rate. If the maximum memory is exceeded, the sample rate is halved until the memory length is <= maximum. 
			// TODO : Auto (Fast): Memory length = recording time * sample rate. If over 20 Mpts/CH, the sample rate is halved until the memory length is <= 20 Mpts.
			double scale = GetTimebaseScale();
			depth = llround(scale * 24 * GetSampleRate());
			if(depth < 77)
			{	// Special handling of small values
				if	   (depth == 48) 	depth = 60;
				else if(depth == 38) 	depth = 56;
				else if(depth == 24) 	depth = 48;
				else if(depth == 19) 	depth = 46;
				else if(depth == 12) 	depth = 42;
				else if(depth == 8)  	depth = 40;
				else if(depth == 5)  	depth = 39;
			}
			else
			{
				uint64_t maxDepth = GetMaxAutoMemoryDepth();
				if(depth > maxDepth)
				{
					depth = maxDepth;
				}
			}
			LogDebug("Auto memory depth activated, calculating Mdepth based on time scale %f and sample rate %" PRIu64 ": mdepth = %" PRIu64 ".\n",scale,GetSampleRate(),depth);
			}
			break;
		default:
		case MEMORY_DEPTH_FIXED:
			f = Unit(Unit::UNIT_SAMPLEDEPTH).ParseString(reply);
			depth = static_cast<int64_t>(f);
			break;
	}
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_memoryDepth = depth;
	m_memodyDepthMode = mode;
	m_memoryDepthValid = true;
	return m_memoryDepth;
}

void MagnovaOscilloscope::SetSampleDepth(uint64_t depth)
{
	{	//Need to lock the transport mutex when setting depth to prevent changing depth during an acquisition
		lock_guard<recursive_mutex> lock(m_transport->GetMutex());
		switch(m_modelid)
		{
			case MODEL_MAGNOVA_BMO:
				sendWithAck("ACQUIRE:MDEPTH %" PRIu64 "",depth);
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
	}
	//Don't update the cache because the scope is likely to round the value
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_memoryDepthValid = false;
	m_timebaseScaleValid= false;
	m_sampleRateValid = false;
	m_triggerOffsetValid = false;
}

void MagnovaOscilloscope::SetSampleRate(uint64_t rate)
{
	{ 	//Need to lock the transport mutex when setting rate to prevent changing rate during an acquisition
		lock_guard<recursive_mutex> lock(m_transport->GetMutex());

		double sampletime = GetSampleDepth() / (double)rate;
		double scale = sampletime / 24; // TODO: check that should be 12 or 24 (when in extended capture rate) ?

		switch(m_modelid)
		{
			case MODEL_MAGNOVA_BMO:
				{
					char tmp[128];
					snprintf(tmp, sizeof(tmp), "%1.0E", scale);
					if(tmp[0] == '3')
						tmp[0] = '2';
					sendWithAck(":TIMEBASE:SCALE %s", tmp);
				}
				break;

			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
	}
	//Don't update the cache because the scope is likely to round the value
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_timebaseScaleValid = false;
	m_triggerOffsetValid = false;
}

void MagnovaOscilloscope::EnableTriggerOutput()
{
	sendOnly(":TRIG:AOUT ON");
}

void MagnovaOscilloscope::SetUseExternalRefclk(bool external)
{
	switch(m_modelid)
	{
		case MODEL_MAGNOVA_BMO:
			sendOnly(":ACQuire:RCLock %s", external ? "EXT" : "INT");
			break;

		default:
			LogError("Unknown scope type\n");
			break;
	}

}

void MagnovaOscilloscope::SetTriggerOffset(int64_t offset)
{
	//Magnova's standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	//LogDebug("Set trigger offset to %f : rate = %" PRId64 ", depth = %" PRId64 " haldepth = %" PRId64 ", halfwidth = %" PRId64 ".\n",((float)offset)*SECONDS_PER_FS,rate,GetSampleDepth(),halfdepth,halfwidth);

	sendWithAck(":TIMebase:OFFSet %1.2E", (offset - halfwidth) * SECONDS_PER_FS);

	//Don't update the cache because the scope is likely to round the offset we ask for.
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_triggerOffsetValid = false;
}

int64_t MagnovaOscilloscope::GetTriggerOffset()
{
	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_triggerOffsetValid)
			return m_triggerOffset;
	}
	string reply;
	reply = converse(":TIMebase:OFFSet?");

	//Result comes back in scientific notation
	double sec;
	if(sscanf(reply.c_str(), "%le", &sec)!=1)
	{
		protocolError("invalid trigger offset value '%s'",reply.c_str());
	}
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_triggerOffset = static_cast<int64_t>(round(sec * FS_PER_SECOND));
	}

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_triggerOffset += halfwidth;

	//LogDebug("Get trigger offset to %lf : rate = %" PRId64 ", depth = %" PRId64 " haldepth = %" PRId64 ", halfwidth = %" PRId64 ", result = %" PRId64 ".\n",sec,rate,GetSampleDepth(),halfdepth,halfwidth,m_triggerOffset);

	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

void MagnovaOscilloscope::SetDeskewForChannel(size_t channel, int64_t skew)
{
	//Cannot deskew trigger channel
	if(channel >= m_analogAndDigitalChannelCount)
		return;
	if(channel < m_analogChannelCount)
	{
		sendOnly(":CHAN%zu:DESK %1.2E", channel, skew * SECONDS_PER_FS);
	}
	else
	{	// Digital channels
		sendOnly(":DIG:DESK%s %1.2E", GetDigitalChannelBankName(channel).c_str(), skew * SECONDS_PER_FS);
	}

	//Update cache
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew;
}

int64_t MagnovaOscilloscope::GetDeskewForChannel(size_t channel)
{
	//Cannot deskew trigger channel
	if(channel >= m_analogAndDigitalChannelCount)
		return 0;

	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDeskew.find(channel) != m_channelDeskew.end())
			return m_channelDeskew[channel];
	}

	//Read the deskew
	string reply;
	if(channel < m_analogChannelCount)
	{
		reply = converse(":CHAN%zu:DESK?", channel + 1);
	}
	else
	{	// Digital channels
		reply = converse(":DIG:DESK%s?", GetDigitalChannelBankName(channel).c_str());
	}

	//Value comes back as floating point ps
	float skew;
	if(sscanf(reply.c_str(), "%f", &skew)!=1)
	{
		protocolError("invalid channel deskew value '%s'",reply.c_str());
	}
	int64_t skew_ps = round(skew * FS_PER_SECOND);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew_ps;

	return skew_ps;
}

bool MagnovaOscilloscope::IsInterleaving()
{
	switch(m_modelid)
	{
		case MODEL_MAGNOVA_BMO:
			if((m_channelsEnabled[2] == true) || (m_channelsEnabled[3] == true))
			{	// Interleaving if Channel 3 or 4 are active
				return true;
			}
			return false;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			return false;
			// --------------------------------------------------
	}
}

bool MagnovaOscilloscope::SetInterleaving(bool /* combine*/)
{
	//Setting interleaving is not supported, it's always hardware managed
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Analog bank configuration

bool MagnovaOscilloscope::IsADCModeConfigurable()
{
	return false;
}

vector<string> MagnovaOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> v;
	return v;
}

size_t MagnovaOscilloscope::GetADCMode(size_t /*channel*/)
{
	return 0;
}

void MagnovaOscilloscope::SetADCMode(size_t /*channel*/, size_t /* mode */)
{
	return;
}

std::string MagnovaOscilloscope::GetChannelName(size_t channel)
{
	if(channel < m_digitalChannelBase)
	{
	 	return string("CHAN") + to_string(channel + 1);
	}
	else
	{
	 	return string("DIG") + to_string(channel - m_digitalChannelBase);
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

std::string MagnovaOscilloscope::GetDigitalChannelBankName(size_t channel)
{
 	return ((channel - m_digitalChannelBase) < 8) ? "0to7" : "8to15";
}


vector<Oscilloscope::DigitalBank> MagnovaOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;

	if(m_hasLA)
	{
		for(size_t n = 0; n < 2; n++)
		{
			DigitalBank bank;

			for(size_t i = 0; i < 8; i++)
				bank.push_back(m_digitalChannels[i + n * 8]);

			banks.push_back(bank);
		}
	}

	return banks;
}

Oscilloscope::DigitalBank MagnovaOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank ret;
	if(m_hasLA)
	{
		if(channel <= m_digitalChannels[7]->GetIndex())
		{
			for(size_t i = 0; i < 8; i++)
				ret.push_back(m_digitalChannels[i]);
		}
		else
		{
			for(size_t i = 0; i < 8; i++)
				ret.push_back(m_digitalChannels[i + 8]);
		}
	}
	return ret;
}

bool MagnovaOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool MagnovaOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float MagnovaOscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0;
}

float MagnovaOscilloscope::GetDigitalThreshold(size_t channel)
{
	if( (channel < m_digitalChannelBase) || (m_digitalChannelCount == 0) )
		return 0;

	string bank = GetDigitalChannelBankName(channel);
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDigitalThresholds.find(bank) != m_channelDigitalThresholds.end())
			return m_channelDigitalThresholds[bank];
	}

	float result;

	string reply = converse(":DIG:THRESHOLD%s?", bank.c_str());
	if(sscanf(reply.c_str(), "%f", &result)!=1)
	{
		protocolError("invalid digital threshold offset value '%s'",reply.c_str());
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelDigitalThresholds[bank] = result;
	return result;
}

void MagnovaOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
	LogWarning("SetDigitalHysteresis is not implemented\n");
}

void MagnovaOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	string bank = GetDigitalChannelBankName(channel);
	
	sendWithAck(":DIG:THRESHOLD%s %1.2E", bank.c_str(), level);

	//Don't update the cache because the scope is likely to round the offset we ask for.
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDigitalThresholds.erase(bank);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering


/**
	@brief Processes the slope for an edge or edge-derived trigger
 */
void MagnovaOscilloscope::GetTriggerSlope(Trigger* trig, string reply)

{
	auto dt = dynamic_cast<DropoutTrigger*>(trig);
	auto et = dynamic_cast<EdgeTrigger*>(trig);
	auto bt = dynamic_cast<NthEdgeBurstTrigger*>(m_trigger);
	reply = Trim(reply);


	if(reply == "RISing")
	{
		if(dt) dt->SetType(DropoutTrigger::EDGE_RISING);
		if(et) et->SetType(EdgeTrigger::EDGE_RISING);
		if(bt) bt->SetSlope(NthEdgeBurstTrigger::EDGE_RISING);
	}
	else if(reply == "FALLing")
	{
		if(dt) dt->SetType(DropoutTrigger::EDGE_FALLING);
		if(et) et->SetType(EdgeTrigger::EDGE_FALLING);
		if(bt) bt->SetSlope(NthEdgeBurstTrigger::EDGE_FALLING);
	}
	else if(reply == "ALTernate")
	{
		if(et) et->SetType(EdgeTrigger::EDGE_ALTERNATING);
	}
	else if(reply == "BOTH")
	{
		if(dt) dt->SetType(DropoutTrigger::EDGE_ANY);
		if(et) et->SetType(EdgeTrigger::EDGE_ANY);
	}
	else
		protocolError("Unknown trigger slope %s\n", reply.c_str());
}

/**
	@brief Parses a trigger condition
 */
Trigger::Condition MagnovaOscilloscope::GetCondition(string reply)
{
	reply = Trim(reply);

	if(reply == "LTHan")
		return Trigger::CONDITION_LESS;
	else if(reply == "GTHan")
		return Trigger::CONDITION_GREATER;
	else if(reply == "INSide")
		return Trigger::CONDITION_BETWEEN;
	else if(reply == "OUTSide")
		return Trigger::CONDITION_NOT_BETWEEN;

	//unknown
	protocolError("Unknown trigger condition [%s]\n", reply.c_str());
	return Trigger::CONDITION_LESS;
}

/**
	@brief Pushes settings for a trigger condition under a .Condition field
 */
void MagnovaOscilloscope::PushCondition(const string& path, Trigger::Condition cond)
{
	switch(cond)
	{
		case Trigger::CONDITION_LESS:
			sendOnly("%s LTHan", path.c_str());
			break;

		case Trigger::CONDITION_GREATER:
			sendOnly("%s GTHan", path.c_str());
			break;

		case Trigger::CONDITION_BETWEEN:
			sendOnly("%s INSide", path.c_str());
			break;

		case Trigger::CONDITION_NOT_BETWEEN:
			sendOnly("%s OUTSide", path.c_str());
			break;

		//Other values are not legal here, it seems
		default:
			break;
	}
}

void MagnovaOscilloscope::PushFloat(string path, float f)
{
	sendOnly("%s %1.5E", path.c_str(), f);
}

vector<string> MagnovaOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(DropoutTrigger::GetTriggerName());
	ret.push_back(EdgeTrigger::GetTriggerName());
	ret.push_back(PulseWidthTrigger::GetTriggerName());
	ret.push_back(RuntTrigger::GetTriggerName());
	ret.push_back(SlewRateTrigger::GetTriggerName());
	ret.push_back(UartTrigger::GetTriggerName());
	ret.push_back(WindowTrigger::GetTriggerName());
	ret.push_back(GlitchTrigger::GetTriggerName());
	ret.push_back(NthEdgeBurstTrigger::GetTriggerName());
	// TODO: Add missing triggers (DELay, SHOLd, PATTern + Decode-SPI/I2C/Parallel)
	return ret;
}

void MagnovaOscilloscope::PullTrigger()
{
	std::string reply;

	bool isUart = false;
	//Figure out what kind of trigger is active.
	reply = Trim(converse(":TRIGGER:TYPE?"));
	if(reply == "TIMeout")
		PullDropoutTrigger();
	else if(reply == "EDGe")
		PullEdgeTrigger();
	else if(reply == "RUNT")
		PullRuntTrigger();
	else if(reply == "SLOPe")
		PullSlewRateTrigger();
	else if(reply == "DECode")
	{
		PullUartTrigger();
		isUart = true;
	}
	else if(reply == "PULSe")
		PullPulseWidthTrigger();
	else if(reply == "WINDow")
		PullWindowTrigger();
	else if(reply == "INTerval")
		PullGlitchTrigger();
	else if(reply == "NEDGe")
		PullNthEdgeBurstTrigger();
	// Note that NEDGe, DELay, INTerval, SHOLd, PATTern + Decode-SPI/I2C/Parallel are not yet handled
	//Unrecognized trigger type
	else
	{
		LogWarning("Unsupported trigger type \"%s\", defaulting to Edge.\n", reply.c_str());
		reply = "EDGe";
		// Default to Edge
		PullEdgeTrigger();
	}

	//Pull the source (same for all types of trigger)
	PullTriggerSource(m_trigger, reply,isUart);
}

/**
	@brief Reads the source of a trigger from the instrument
 */
void MagnovaOscilloscope::PullTriggerSource(Trigger* trig, string triggerModeName, bool isUart)
{
	string reply;
	if(!isUart)
	{	
		reply = converse(":TRIGGER:%s:SOURCE?", triggerModeName.c_str());
	}
	else
	{	// No SCPI command on Magnova to get Trigget Group information for Decode Trigger => default to edge trigger source
		reply = converse(":TRIGGER:EDGe:SOURCE?");
		// Returns CHANnel1 or DIGital1
	}

	// Get channel number
    int i = reply.size() - 1;
    while (i >= 0 && std::isdigit(reply[i])) {
        i--;
    }
    std::string number = reply.substr(i + 1);
	bool isAnalog = (reply[0] == 'C');

	auto chan = GetOscilloscopeChannelByHwName((isAnalog ? "CH" :  "D") + number);
	trig->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		protocolError("Unknown trigger source \"%s\"\n", reply.c_str());
}

void MagnovaOscilloscope::PushTrigger()
{
	auto dt = dynamic_cast<DropoutTrigger*>(m_trigger);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto rt = dynamic_cast<RuntTrigger*>(m_trigger);
	auto st = dynamic_cast<SlewRateTrigger*>(m_trigger);
	auto ut = dynamic_cast<UartTrigger*>(m_trigger);
	auto wt = dynamic_cast<WindowTrigger*>(m_trigger);
	auto gt = dynamic_cast<GlitchTrigger*>(m_trigger);
	auto bt = dynamic_cast<NthEdgeBurstTrigger*>(m_trigger);

	if(dt)
	{
		sendOnly(":TRIGGER:TYPE TIMeout");
		sendOnly(":TRIGGER:TIMeout:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushDropoutTrigger(dt);
	}
	else if(pt)
	{
		sendOnly(":TRIGGER:TYPE PULSe");
		sendOnly(":TRIGGER:PULSe:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushPulseWidthTrigger(pt);
	}
	else if(rt)
	{
		sendOnly(":TRIGGER:TYPE RUNT");
		sendOnly(":TRIGGER:RUNT:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushRuntTrigger(rt);
	}
	else if(st)
	{
		sendOnly(":TRIGGER:TYPE SLOPe");
		sendOnly(":TRIGGER:SLOPe:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushSlewRateTrigger(st);
	}
	else if(ut)
	{
		sendOnly(":TRIGGER:TYPE DECode");
		// Trigger group not accessible for now via SCPI
		//sendOnly(":TRIGGER:UART:RXSOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		//sendOnly(":TRIGGER:UART:TXSOURCE %s", GetChannelName(m_trigger->GetInput(1).m_channel->GetIndex()).c_str());
		PushUartTrigger(ut);
	}
	else if(wt)
	{
		sendOnly(":TRIGGER:TYPE WINDow");
		sendOnly(":TRIGGER:WINDow:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushWindowTrigger(wt);
	}
	else if(gt)
	{
		sendOnly(":TRIGGER:TYPE INTerval");
		sendOnly(":TRIGGER:INTerval:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushGlitchTrigger(gt);
	}
	else if(bt)
	{
		sendOnly(":TRIGGER:TYPE NEDGe");
		sendOnly(":TRIGGER:NEDGe:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushNthEdgeBurstTrigger(bt);
	}


	else if(et)	   //must be last
	{
		sendOnly(":TRIGGER:TYPE EDGe");
		sendOnly(":TRIGGER:EDGe:SOURCE %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushEdgeTrigger(et, "EDGe");
	}

	else
		LogWarning("PushTrigger on an unimplemented trigger type.\n");
}

/**
	@brief Reads settings for a dropout trigger from the instrument
 */
void MagnovaOscilloscope::PullDropoutTrigger()
{
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<DropoutTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new DropoutTrigger(this);
	DropoutTrigger* dt = dynamic_cast<DropoutTrigger*>(m_trigger);

	// Check for digital source
	string reply = converse(":TRIGGER:TIMeout:SOURCE?");
	if(reply[0] == 'C')
	{	// Level only for analog source
		dt->SetLevel(stof(converse(":TRIGGER:TIMeout:LEVEL?")));
	}

	//Dropout time
	dt->SetDropoutTime(llround(stod(converse(":TRIGGER:TIMeout:TIME?"))*FS_PER_SECOND));

	//Edge type
	GetTriggerSlope(dt,converse(":TRIGGER:TIMeout:SLOPE?"));

	//Reset type
	dt->SetResetType(DropoutTrigger::RESET_NONE);
}

/**
	@brief Pushes settings for a dropout trigger to the instrument
 */
void MagnovaOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
{
	PushFloat(":TRIGGER:TIMeout:LEVEL", trig->GetLevel());
	PushFloat(":TRIGGER:TIMeout:TIME", trig->GetDropoutTime() * SECONDS_PER_FS);
	sendOnly(":TRIGGER:TIMeout:SLOPe %s", (trig->GetType() == DropoutTrigger::EDGE_RISING) ? "RISing" : "FALLing");
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void MagnovaOscilloscope::PullEdgeTrigger()
{
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new EdgeTrigger(this);
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

	// Check for digital source
	string reply = converse(":TRIGGER:EDGE:SOURCE?");
	if(reply[0] == 'C')
	{	// Level only for analog source
		et->SetLevel(stof(converse(":TRIGGER:EDGE:LEVEL?")));
	}

	//Slope
	GetTriggerSlope(et, Trim(converse(":TRIGGER:EDGE:SLOPE?")));
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void MagnovaOscilloscope::PushEdgeTrigger(EdgeTrigger* trig, const std::string trigType)
{
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			sendOnly(":TRIGGER:%s:SLOPE RISING", trigType.c_str());
			break;

		case EdgeTrigger::EDGE_FALLING:
			sendOnly(":TRIGGER:%s:SLOPE FALLING", trigType.c_str());
			break;

		case EdgeTrigger::EDGE_ANY:
			sendOnly(":TRIGGER:%s:SLOPE BOTH", trigType.c_str());
			break;

		case EdgeTrigger::EDGE_ALTERNATING:
			sendOnly(":TRIGGER:%s:SLOPE ALTernate", trigType.c_str());
			break;

		default:
			LogWarning("Invalid trigger type %d\n", trig->GetType());
			break;
	}
	//Level
	sendOnly(":TRIGGER:%s:LEVEL %1.2E", trigType.c_str(), trig->GetLevel());
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void MagnovaOscilloscope::PullPulseWidthTrigger()
{
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<PulseWidthTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new PulseWidthTrigger(this);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	Unit fs(Unit::UNIT_FS);

	// Check for digital source
	string reply = converse(":TRIGGER:PULSe:SOURCE?");
	if(reply[0] == 'C')
	{	// Level only for analog source
		pt->SetLevel(stof(converse(":TRIGGER:PULSe:LEVEL?")));
	}

	//Condition
	pt->SetCondition(GetCondition(converse(":TRIGGER:PULSe:TIMing?")));

	// Lower/upper not available on Magnova's pulse, only Threshod is available so let's map it lower bound
	pt->SetLowerBound(llround(stod((converse(":TRIGger:PULSe:THReshold?")))*FS_PER_SECOND));
	//Min range
	//pt->SetLowerBound(fs.ParseString(converse(":TRIGGER:PULSe:DURation:LOWer?")));

	//Max range
	//pt->SetUpperBound(fs.ParseString(converse(":TRIGGER:PULSe:DURation:UPPer?")));

	//Slope
	reply = Trim(converse(":TRIGGER:PULSe:POLarity?"));
	if(reply == "POSitive")
		pt->SetType(PulseWidthTrigger::EDGE_RISING);
	else if(reply == "NEGative")
		pt->SetType(PulseWidthTrigger::EDGE_FALLING);

}

/**
	@brief Pushes settings for a pulse width trigger to the instrument
 */
void MagnovaOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	PushFloat(":TRIGGER:PULSe:LEVEL", trig->GetLevel());
	PushCondition(":TRIGGER:PULSe:TIMing", trig->GetCondition());
	// Lower/upper not available on Magnova's pulse, only Threshod is available so let's map it lower bound
	PushFloat(":TRIGger:PULSe:THReshold", trig->GetLowerBound() * SECONDS_PER_FS);
	//PushFloat(":TRIGGER:PULSe:DURation:LOWer", trig->GetUpperBound() * SECONDS_PER_FS);
	//PushFloat(":TRIGGER:PULSe:DURation:UPPer", trig->GetLowerBound() * SECONDS_PER_FS);
	sendOnly(":TRIGGER:PULSe:POLarity %s", trig->GetType() != PulseWidthTrigger::EDGE_FALLING ? "POSitive" : "NEGative");
}

/**
	@brief Reads settings for a runt-pulse trigger from the instrument
 */
void MagnovaOscilloscope::PullRuntTrigger()
{
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RuntTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RuntTrigger(this);
	RuntTrigger* rt = dynamic_cast<RuntTrigger*>(m_trigger);

	string reply;

	//Lower bound
	rt->SetLowerBound(stof(converse(":TRIGGER:RUNT:LEVel1?")));

	//Upper bound
	rt->SetUpperBound(stof(converse(":TRIGGER:RUNT:LEVel2?")));

	//Lower bound
	rt->SetLowerInterval(llround(stod(converse(":TRIGGER:RUNT:DURation:LOWer?"))*FS_PER_SECOND));

	//Upper interval
	rt->SetUpperInterval(llround(stod(converse(":TRIGGER:RUNT:DURation:UPPer?"))*FS_PER_SECOND));

	//Slope
	reply = Trim(converse(":TRIGger:RUNT:POLarity?"));
	if(reply == "POSitive")
		rt->SetSlope(RuntTrigger::EDGE_RISING);
	else if(reply == "NEGative")
		rt->SetSlope(RuntTrigger::EDGE_FALLING);

	//Condition
	rt->SetCondition(GetCondition(converse(":TRIGGER:RUNT:TIMing?")));
}

/**
	@brief Pushes settings for a runt trigger to the instrument
 */
void MagnovaOscilloscope::PushRuntTrigger(RuntTrigger* trig)
{
	PushFloat(":TRIGGER:RUNT:LEVel1", trig->GetLowerBound());
	PushFloat(":TRIGGER:RUNT:LEVel2", trig->GetUpperBound());
	PushFloat(":TRIGGER:RUNT:DURation:LOWer", trig->GetLowerInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:RUNT:DURation:UPPer", trig->GetUpperInterval() * SECONDS_PER_FS);

	sendOnly(":TRIGger:RUNT:POLarity %s", (trig->GetSlope() != RuntTrigger::EDGE_FALLING) ? "POSitive" : "NEGative");
	PushCondition(":TRIGGER:RUNT:TIMing", trig->GetCondition());
}

/**
	@brief Reads settings for a slew rate trigger from the instrument
 */
void MagnovaOscilloscope::PullSlewRateTrigger()
{
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<SlewRateTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new SlewRateTrigger(this);
	SlewRateTrigger* st = dynamic_cast<SlewRateTrigger*>(m_trigger);

	//Lower bound
	st->SetLowerBound(stof(converse(":TRIGGER:SLOPe:LEVel1?")));

	//Upper bound
	st->SetUpperBound(stof(converse(":TRIGGER:SLOPe:LEVel2?")));

	//Lower interval
	st->SetLowerInterval(llround(stod(converse(":TRIGGER:SLOPe:DURation:LOWer?")) * FS_PER_SECOND));

	//Upper interval
	st->SetUpperInterval(llround(stod(converse(":TRIGGER:SLOPe:DURation:UPPer?")) * FS_PER_SECOND));

	//Slope
	string reply = Trim(converse(":TRIGger:SLOPe:TYPE?"));
	if(reply == "RISing")
		st->SetSlope(SlewRateTrigger::EDGE_RISING);
	else
		st->SetSlope(SlewRateTrigger::EDGE_FALLING);

	//Condition
	st->SetCondition(GetCondition(converse("TRIGGER:SLOPe:TIMing?")));
}

/**
	@brief Pushes settings for a slew rate trigger to the instrument
 */
void MagnovaOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
{
	PushCondition(":TRIGGER:SLOPE", trig->GetCondition());
	PushFloat(":TRIGGER:SLOPe:DURation:LOWer", trig->GetLowerInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:SLOPe:DURation:UPPer", trig->GetUpperInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:SLOPe:LEVel1", trig->GetLowerBound());
	PushFloat(":TRIGGER:SLOPe:LEVel2", trig->GetUpperBound());
	sendOnly(":TRIGger:SLOPe:TYPE %s", (trig->GetSlope() != SlewRateTrigger::EDGE_FALLING)	? "RISing" : "FALLing");
	PushCondition(":TRIGger:SLOPe:TIMing",trig->GetCondition());
}

/**
	@brief Reads settings for a UART trigger from the instrument
 */
void MagnovaOscilloscope::PullUartTrigger()
{
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<UartTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new UartTrigger(this);
	UartTrigger* ut = dynamic_cast<UartTrigger*>(m_trigger);

    string reply;
	string p1,p2;


	//Bit rate : Not available in Magnova SCPI implemetnation for now
	//ut->SetBitRate(stoi(converse(":TRIGGER:UART:BAUD?")));

	//Level : Not available in Magnova SCPI implemetnation for now
	//ut->SetLevel(stof(converse(":TRIGGER:UART:RXT?")));

	//Parity : Not available in Magnova SCPI implemetnation for now
	/*reply = Trim(converse(":TRIGGER:UART:PARITY?"));
	if(reply == "NONE")
		ut->SetParityType(UartTrigger::PARITY_NONE);
	else if(reply == "EVEN")
		ut->SetParityType(UartTrigger::PARITY_EVEN);
	else if(reply == "ODD")
		ut->SetParityType(UartTrigger::PARITY_ODD);
	else if(reply == "MARK")
		ut->SetParityType(UartTrigger::PARITY_MARK);
	else if(reply == "SPACe")
		ut->SetParityType(UartTrigger::PARITY_SPACE);*/


	//Idle polarity : Not available in Magnova SCPI implemetnation for now
	/*reply = Trim(converse(":TRIGGER:UART:IDLE?"));
	if(reply == "HIGH")
		ut->SetPolarity(UartTrigger::IDLE_HIGH);
	else if(reply == "LOW")
		ut->SetPolarity(UartTrigger::IDLE_LOW);*/

	//Stop bits : Not available in Magnova SCPI implemetnation for now
	//ut->SetStopBits(stof(Trim(converse(":TRIGGER:UART:STOP?"))));

	//Trigger type
	reply = Trim(converse(":TRIGger:DECode:UART:EVENt?"));
	if(reply == "FSTart")
		ut->SetMatchType(UartTrigger::TYPE_START);
	else if(reply == "FPCHeck")
		ut->SetMatchType(UartTrigger::TYPE_PARITY_ERR);
	else if(reply == "DATa")
		ut->SetMatchType(UartTrigger::TYPE_DATA);
	else // "IFCompletion" (invalid frame completion) / "FPCHeck" (failed parity check) / "IFSTart" (invalid frame start)
		LogWarning("Unsupported UART trigger condition '%s'", reply.c_str());

	// Check data length
	int length = stoi(converse(":TRIGger:DECode:UART:DATA:LENGth?"));
	bool ignoreP2 = true;
	// Data to match
	p1 = Trim(converse(":TRIGger:DECode:UART:DATA:WORD0?"));
	if(length >= 2)
	{
		p2 = Trim(converse(":TRIGger:DECode:UART:DATA:WORD1?"));
		ignoreP2 = false;
	}
	else
	{	// SetPatterns() needs an patter of at least the same size as p1
		p2 = "XXXXXXXX";
	}
	ut->SetPatterns(p1, p2, ignoreP2);
}

/**
	@brief Pushes settings for a UART trigger to the instrument
 */
void MagnovaOscilloscope::PushUartTrigger(UartTrigger* trig)
{
	string p1,p2;
	// Level : Not available in Magnova SCPI implemetnation for now
	// PushFloat(":TRIGGER:UART:LIMIT", trig->GetLevel());

	//Bit9State : Not available in Magnova SCPI implemetnation for now
	//PushFloat(":TRIGGER:UART:BAUD", trig->GetBitRate());
	//sendOnly(":TRIGGER:UART:BITORDER LSB");
	// Data length : : Not available in Magnova SCPI implemetnation for now
	// sendOnly(":TRIGGER:UART:DLENGTH 8");

	// Parity : Not available in Magnova SCPI implemetnation for now
	/*switch(trig->GetParityType())
	{
		case UartTrigger::PARITY_NONE:
			sendOnly(":TRIGGER:UART:PARITY NONE");
			break;

		case UartTrigger::PARITY_ODD:
			sendOnly(":TRIGGER:UART:PARITY ODD");
			break;

		case UartTrigger::PARITY_EVEN:
			sendOnly(":TRIGGER:UART:PARITY EVEN");
			break;

		case UartTrigger::PARITY_MARK:
			sendOnly(":TRIGGER:UART:PARITY MARK");
			break;
		case UartTrigger::PARITY_SPACE:
			sendOnly(":TRIGGER:UART:PARITY SPACE");
			break;
	}*/

	//Polarity : Not available in Magnova SCPI implemetnation for now
	//sendOnly(":TRIGGER:UART:IDLE %s", (trig->GetPolarity() == UartTrigger::IDLE_HIGH) ? "HIGH" : "LOW");
	// Stops bits : Not available in Magnova SCPI implemetnation for now
	/*float nstop = trig->GetStopBits();
	if(nstop == 1)
		sendOnly(":TRIGGER:UART:STOP 1");
	else if(nstop == 2)
		sendOnly(":TRIGGER:UART:STOP 2");
	else
		sendOnly(":TRIGGER:UART:STOP 1.5");*/

	//Pattern 
	int dataLength = 1;
	trig->SetRadix(SerialTrigger::RADIX_ASCII);
	// No public access to unformated Pattern 1 and 2 => use GetParameter() instread since we want the unformated string value
	p1 = trig->GetParameter("Pattern").ToString();
	p2 = trig->GetParameter("Pattern 2").ToString();
	if((p2 != ""))
	{
		dataLength++;
	}
	LogDebug("Found pattern1 = '%s' and pattern2 = '%s'.\n",p1.c_str(),p2.c_str());

	//Match type
	switch(trig->GetMatchType())
	{
		case UartTrigger::TYPE_START:
			sendOnly(":TRIGger:DECode:UART:EVENt FSTart");
			break;
		case UartTrigger::TYPE_PARITY_ERR:
			sendOnly(":TRIGger:DECode:UART:EVENt FPCHeck");
			break;
		case UartTrigger::TYPE_DATA:
			sendOnly(":TRIGger:DECode:UART:EVENt DATA");
			break;
		default:
		case UartTrigger::TYPE_STOP:
			LogWarning("Unsupported match type: %d\n",trig->GetMatchType());
			break;
	}
	sendOnly(":TRIGger:DECode:UART:DATA:LENGth %d",dataLength);
	sendOnly(":TRIGger:DECode:UART:DATA:WORD0 %s", p1.c_str());
	sendOnly(":TRIGger:DECode:UART:DATA:WORD1 %s", p2.c_str());
}

/**
	@brief Reads settings for a window trigger from the instrument
 */
void MagnovaOscilloscope::PullWindowTrigger()
{
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<WindowTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new WindowTrigger(this);
	WindowTrigger* wt = dynamic_cast<WindowTrigger*>(m_trigger);

	string type = converse(":TRIGger:WINDow:TYPE?");
	if(type == "ENTer")
		wt->SetWindowType(WindowTrigger::WINDOW_ENTER);
	else
		wt->SetWindowType(WindowTrigger::WINDOW_EXIT);

	//Lower bound
	wt->SetLowerBound(stof(converse(":TRIGger:WINDow:LEVel1?")));

	//Upper bound
	wt->SetUpperBound(stof(converse(":TRIGger:WINDow:LEVel2?")));
}

/**
	@brief Pushes settings for a window trigger to the instrument
 */
void MagnovaOscilloscope::PushWindowTrigger(WindowTrigger* trig)
{
	switch(trig->GetWindowType())
	{
		case WindowTrigger::WINDOW_ENTER:
			sendOnly(":TRIGger:WINDow:TYPE ENTer");
			break;
		case WindowTrigger::WINDOW_EXIT:
			sendOnly(":TRIGger:WINDow:TYPE LEAVe");
			break;
		default:
			LogWarning("Unsupported window type: %d\n",trig->GetWindowType());
			break;
	}
	PushFloat(":TRIGger:WINDow:LEVel1", trig->GetLowerBound());
	PushFloat(":TRIGger:WINDow:LEVel2", trig->GetUpperBound());
}

/**
	@brief Reads settings for a glitch trigger from the instrument
 */
void MagnovaOscilloscope::PullGlitchTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<GlitchTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new GlitchTrigger(this);
	GlitchTrigger* gt = dynamic_cast<GlitchTrigger*>(m_trigger);

	//Level
	// Check for digital source
	string reply = converse(":TRIGGER:INTerval:SOURCE?");
	if(reply[0] == 'C')
	{	// Level only for analog source
		gt->SetLevel(stof(converse(":TRIGGER:INTerval:LEVEL?")));
	}

	//Slope
	reply = Trim(converse(":TRIGGER:INTerval:POLarity?"));
	if(reply == "POSitive")
		gt->SetType(GlitchTrigger::EDGE_RISING);
	else if(reply == "NEGative")
		gt->SetType(GlitchTrigger::EDGE_FALLING);

	//Condition
	gt->SetCondition(GetCondition(converse(":TRIGGER:INTerval:TIMing?")));

	//Lower bound
	gt->SetLowerBound(llround(stod(converse(":TRIGger:INTerval:DURation:LOWer?"))*FS_PER_SECOND));

	//Upper interval
	gt->SetUpperBound(llround(stod(converse(":TRIGger:INTerval:DURation:UPPer?"))*FS_PER_SECOND));
}

/**
	@brief Pushes settings for a glitch trigger to the instrument
 */
void MagnovaOscilloscope::PushGlitchTrigger(GlitchTrigger* trig)
{
	PushFloat(":TRIGGER:INTerval:LEVEL", trig->GetLevel());
	sendOnly(":TRIGger:INTerval:POLarity %s", (trig->GetType() != GlitchTrigger::EDGE_FALLING) ? "POSitive" : "NEGative");
	PushCondition(":TRIGGER:INTerval:TIMing", trig->GetCondition());
	PushFloat(":TRIGGER:INTerval:DURation:LOWer", trig->GetLowerBound() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:INTerval:DURation:UPPer", trig->GetUpperBound() * SECONDS_PER_FS);
}


/**
	@brief Reads settings for an Nth-edge-burst trigger from the instrument
 */
void MagnovaOscilloscope::PullNthEdgeBurstTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<NthEdgeBurstTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new NthEdgeBurstTrigger(this);
	auto bt = dynamic_cast<NthEdgeBurstTrigger*>(m_trigger);

	//Level
	// Check for digital source
	string reply = converse(":TRIGGER:NEDGe:SOURCE?");
	if(reply[0] == 'C')
	{	// Level only for analog source
		bt->SetLevel(stof(converse(":TRIGGER:NEDGe:LEVEL?")));
	}

	//Slope
	GetTriggerSlope(bt,converse(":TRIGger:NEDGe:SLOPe?"));

	//Idle time 
	bt->SetIdleTime(llround(stod(converse(":TRIGger:NEDGe:IDLE?"))*FS_PER_SECOND));

	//Edge number
	bt->SetEdgeNumber(stoi(converse(":TRIGger:NEDGe:COUNt?")));
}

/**
	@brief Pushes settings for a Nth edge burst trigger to the instrument

	@param trig	The trigger
 */
void MagnovaOscilloscope::PushNthEdgeBurstTrigger(NthEdgeBurstTrigger* trig)
{
	PushFloat(":TRIGGER:NEDGe:LEVEL", trig->GetLevel());
	sendOnly(":TRIGger:NEDGe:SLOPE %s", (trig->GetSlope() != NthEdgeBurstTrigger::EDGE_FALLING) ? "RISing" : "FALLing");
	PushFloat(":TRIGger:NEDGe:IDLE", trig->GetIdleTime() * SECONDS_PER_FS);
	sendOnly(":TRIGger:NEDGe:COUNt %" PRIu64 "", trig->GetEdgeNumber());
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator mode

//Per docs, this is almost the same API as the SDG series generators.
//But the SAG102I and integrated generator have only a single output.
//This code can likely be ported to work with SDG* fairly easily, though.

vector<FunctionGenerator::WaveShape> MagnovaOscilloscope::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(SHAPE_SINE);
	ret.push_back(SHAPE_SQUARE);
	ret.push_back(SHAPE_NOISE);

	ret.push_back(SHAPE_DC);
	ret.push_back(SHAPE_STAIRCASE_UP);
	ret.push_back(SHAPE_STAIRCASE_DOWN);
	ret.push_back(SHAPE_STAIRCASE_UP_DOWN);
	ret.push_back(SHAPE_PULSE);

	//what's "trapezia"?
	ret.push_back(SHAPE_SAWTOOTH_UP);
	ret.push_back(SHAPE_SAWTOOTH_DOWN);
	ret.push_back(SHAPE_EXPONENTIAL_DECAY);
	ret.push_back(SHAPE_EXPONENTIAL_RISE);
	ret.push_back(SHAPE_LOG_DECAY);
	ret.push_back(SHAPE_LOG_RISE);
	ret.push_back(SHAPE_SQUARE_ROOT);
	ret.push_back(SHAPE_CUBE_ROOT);
	ret.push_back(SHAPE_QUADRATIC);
	ret.push_back(SHAPE_CUBIC);
	ret.push_back(SHAPE_SINC);
	ret.push_back(SHAPE_GAUSSIAN);
	ret.push_back(SHAPE_DLORENTZ);
	ret.push_back(SHAPE_HAVERSINE);
	ret.push_back(SHAPE_LORENTZ);
	ret.push_back(SHAPE_GAUSSIAN_PULSE);
	//What's Gmonopuls?
	//What's Tripuls?
	ret.push_back(SHAPE_CARDIAC);
	//What's quake?
	//What's chirp?
	//What's twotone?
	//What's snr?
	ret.push_back(SHAPE_HAMMING);
	ret.push_back(SHAPE_HANNING);
	ret.push_back(SHAPE_KAISER);
	ret.push_back(SHAPE_BLACKMAN);
	ret.push_back(SHAPE_GAUSSIAN_WINDOW);
	ret.push_back(SHAPE_TRIANGLE);
	ret.push_back(SHAPE_HARRIS);
	ret.push_back(SHAPE_BARTLETT);
	ret.push_back(SHAPE_TAN);
	ret.push_back(SHAPE_COT);
	ret.push_back(SHAPE_SEC);
	ret.push_back(SHAPE_CSC);
	ret.push_back(SHAPE_ASIN);
	ret.push_back(SHAPE_ACOS);
	ret.push_back(SHAPE_ATAN);
	ret.push_back(SHAPE_ACOT);

	return ret;
}

bool MagnovaOscilloscope::GetFunctionChannelActive(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgEnabled.find(chan) != m_awgEnabled.end())
			return m_awgEnabled[chan];
	}

	auto reply = m_transport->SendCommandQueuedWithReply(":FGEN:STAT?", false);

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(reply.find("OFF") != string::npos)
			m_awgEnabled[chan] = false;
		else
			m_awgEnabled[chan] = true;

		return m_awgEnabled[chan];
	}

}

void MagnovaOscilloscope::SetFunctionChannelActive(int chan, bool on)
{
	sendWithAck(":FGEN:STAT %s",(on ? "ON" : "OFF"));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgEnabled[chan] = on;
}

float MagnovaOscilloscope::GetFunctionChannelDutyCycle(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgDutyCycle.find(chan) != m_awgDutyCycle.end())
			return m_awgDutyCycle[chan];
	}

	string type = GetFunctionChannelShape(chan) == SHAPE_SQUARE ? "SQU" : "PULS";
	
	string duty = converse(":FGEN:WAV:%s:DUTY ?",type.c_str());

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	float dutyf;
	if(sscanf(duty.c_str(), "%f", &dutyf)!=1)
	{
		protocolError("invalid channel ducy cycle value '%s'",duty.c_str());
	}
	m_awgDutyCycle[chan] = (dutyf/100);
	return m_awgDutyCycle[chan];
}

void MagnovaOscilloscope::SetFunctionChannelDutyCycle(int chan, float duty)
{
	string type = GetFunctionChannelShape(chan) == SHAPE_SQUARE ? "SQU" : "PULS";
	sendWithAck(":FGEN:WAV:%s:DUTY %.4f",type.c_str(),round(duty * 100));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgDutyCycle.erase(chan);
}

float MagnovaOscilloscope::GetFunctionChannelAmplitude(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgRange.find(chan) != m_awgRange.end())
			return m_awgRange[chan];
	}

	string amp = converse(":FGEN:WAV:AMPL ?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	float ampf;
	if(sscanf(amp.c_str(), "%f", &ampf)!=1)
	{
		protocolError("invalid channel amplitude value '%s'",amp.c_str());
	}
	m_awgRange[chan] = ampf;

	return m_awgRange[chan];
}

void MagnovaOscilloscope::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	sendWithAck(":FGEN:WAV:AMPL %.4f",amplitude);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgRange.erase(chan);
}

float MagnovaOscilloscope::GetFunctionChannelOffset(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgOffset.find(chan) != m_awgOffset.end())
			return m_awgOffset[chan];
	}

	string offset = converse(":FGEN:WAV:OFFS ?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	float offsetf;
	if(sscanf(offset.c_str(), "%f", &offsetf)!=1)
	{
		protocolError("invalid channel attenuation value '%s'",offset.c_str());
	}
	m_awgOffset[chan] = offsetf;
	return m_awgOffset[chan];
}

void MagnovaOscilloscope::SetFunctionChannelOffset(int chan, float offset)
{
	sendWithAck(":FGEN:WAV:OFFS %.4f",offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgOffset.erase(chan);
}

float MagnovaOscilloscope::GetFunctionChannelFrequency(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgFrequency.find(chan) != m_awgFrequency.end())
			return m_awgFrequency[chan];
	}

	string freq = converse(":FGEN:WAV:FREQ ?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	float freqf;
	if(sscanf(freq.c_str(), "%f", &freqf)!=1)
	{
		protocolError("invalid channel frequency value '%s'",freq.c_str());
	}
	m_awgFrequency[chan] = freqf;
	return m_awgFrequency[chan];
}

void MagnovaOscilloscope::SetFunctionChannelFrequency(int chan, float hz)
{
	sendWithAck(":FGEN:WAV:FREQ %.4f",hz);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgFrequency.erase(chan);
}

FunctionGenerator::WaveShape MagnovaOscilloscope::GetFunctionChannelShape(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgShape.find(chan) != m_awgShape.end())
			return m_awgShape[chan];
	}

	//Query the basic wave parameters
	auto shape = m_transport->SendCommandQueuedWithReply(":FGEN:WAV:SHAP?", false);
	// TODO Arb not available yet in SCPI commands
	// auto areply = m_transport->SendCommandQueuedWithReply(":FGEN:WAV:ASHAP?", false);

	//Crack the replies
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(shape == "SINe")
			m_awgShape[chan] = FunctionGenerator::SHAPE_SINE;
		else if(shape == "SQUare")
			m_awgShape[chan] = FunctionGenerator::SHAPE_SQUARE;
		else if(shape == "RAMP")
		{
			LogWarning("wave type RAMP unimplemented\n");
		}
		else if(shape == "PULSe")
			m_awgShape[chan] = FunctionGenerator::SHAPE_PULSE;
		else if(shape == "NOISe")
			m_awgShape[chan] = FunctionGenerator::SHAPE_NOISE;
		else if(shape == "DC")
			m_awgShape[chan] = FunctionGenerator::SHAPE_DC;
		else if(shape == "PRBS")
		{
			m_awgShape[chan] = FunctionGenerator::SHAPE_PRBS_NONSTANDARD;
		}
		else if(shape == "ARBitrary")
		{
			m_awgShape[chan] = FunctionGenerator::SHAPE_CARDIAC;
			/*string name = areply.substr(areply.find("NAME,") + 5);

			if(name == "ExpFal")
				m_awgShape[chan] = FunctionGenerator::SHAPE_EXPONENTIAL_DECAY;
			else if(name == "ExpRise")
				m_awgShape[chan] = FunctionGenerator::SHAPE_EXPONENTIAL_RISE;
			else if(name == "LogFall")
				m_awgShape[chan] = FunctionGenerator::SHAPE_LOG_DECAY;
			else if(name == "LogRise")
				m_awgShape[chan] = FunctionGenerator::SHAPE_LOG_RISE;
			else if(name == "Sqrt")
				m_awgShape[chan] = FunctionGenerator::SHAPE_SQUARE_ROOT;
			else if(name == "Root3")
				m_awgShape[chan] = FunctionGenerator::SHAPE_CUBE_ROOT;
			else if(name == "X^2")
				m_awgShape[chan] = FunctionGenerator::SHAPE_SQUARE;
			else if(name == "X^3")
				m_awgShape[chan] = FunctionGenerator::SHAPE_CUBIC;
			else if(name == "Sinc")
				m_awgShape[chan] = FunctionGenerator::SHAPE_SINC;
			else if(name == "Gaussian")
				m_awgShape[chan] = FunctionGenerator::SHAPE_GAUSSIAN;
			else if(name == "StairUp")
				m_awgShape[chan] = FunctionGenerator::SHAPE_STAIRCASE_UP;
			//DLorentz
			else if(name == "Haversine")
				m_awgShape[chan] = FunctionGenerator::SHAPE_HAVERSINE;
			else if(name == "Lorentz")
				m_awgShape[chan] = FunctionGenerator::SHAPE_LORENTZ;
			else if(name == "Gauspuls")
				m_awgShape[chan] = FunctionGenerator::SHAPE_GAUSSIAN_PULSE;
			//TODO: Gmonopuls
			//TODO: Tripuls
			else if(name == "Cardiac")
				m_awgShape[chan] = FunctionGenerator::SHAPE_CARDIAC;
			//TODO: Quake
			//TODO: Chirp
			//TODO: Twotone
			else if(name == "StairDn")
				m_awgShape[chan] = FunctionGenerator::SHAPE_STAIRCASE_DOWN;
			//TODO: SNR
			else if(name == "Hamming")
				m_awgShape[chan] = FunctionGenerator::SHAPE_HAMMING;
			else if(name == "Hanning")
				m_awgShape[chan] = FunctionGenerator::SHAPE_HANNING;
			else if(name == "kaiser")
				m_awgShape[chan] = FunctionGenerator::SHAPE_KAISER;
			else if(name == "Blackman")
				m_awgShape[chan] = FunctionGenerator::SHAPE_BLACKMAN;
			else if(name == "Gausswin")
				m_awgShape[chan] = FunctionGenerator::SHAPE_GAUSSIAN_WINDOW;
			else if(name == "Triangle")
				m_awgShape[chan] = FunctionGenerator::SHAPE_TRIANGLE;
			else if(name == "BlackmanH")
				m_awgShape[chan] = FunctionGenerator::SHAPE_BLACKMAN;
			else if(name == "Bartlett-Hann")
				m_awgShape[chan] = FunctionGenerator::SHAPE_BARTLETT;
			else if(name == "Tan")
				m_awgShape[chan] = FunctionGenerator::SHAPE_TAN;
			else if(name == "StairUD")
				m_awgShape[chan] = FunctionGenerator::SHAPE_STAIRCASE_UP_DOWN;
			else if(name == "Cot")
				m_awgShape[chan] = FunctionGenerator::SHAPE_COT;
			else if(name == "Sec")
				m_awgShape[chan] = FunctionGenerator::SHAPE_SEC;
			else if(name == "Csc")
				m_awgShape[chan] = FunctionGenerator::SHAPE_CSC;
			else if(name == "Asin")
				m_awgShape[chan] = FunctionGenerator::SHAPE_ASIN;
			else if(name == "Acos")
				m_awgShape[chan] = FunctionGenerator::SHAPE_ACOS;
			else if(name == "Atan")
				m_awgShape[chan] = FunctionGenerator::SHAPE_ATAN;
			else if(name == "Acot")
				m_awgShape[chan] = FunctionGenerator::SHAPE_ACOT;
			//TODO: Trapezia
			else if(name == "Upramp")
				m_awgShape[chan] = FunctionGenerator::SHAPE_SAWTOOTH_UP;
			else if(name == "Dnramp")
				m_awgShape[chan] = FunctionGenerator::SHAPE_SAWTOOTH_DOWN;
			else
				LogWarning("Arb shape %s unimplemented\n", name.c_str());*/
		}
		else
			LogWarning("wave type %s unimplemented\n", shape.c_str());

		return m_awgShape[chan];
	}
}

void MagnovaOscilloscope::SetFunctionChannelShape(int chan, FunctionGenerator::WaveShape shape)
{
	string basicType;
	string arbType;

	switch(shape)
	{
		//Basic wave types
		case SHAPE_SINE:
			basicType = "SINE";
			break;

		case SHAPE_SQUARE:
			basicType = "SQUARE";
			break;

		//TODO: "ramp"

		case SHAPE_PULSE:
			basicType = "PULSE";
			break;

		case SHAPE_NOISE:
			basicType = "NOISE";
			break;

		case SHAPE_PRBS_NONSTANDARD:
			basicType = "PRBS";
			break;

		case SHAPE_DC:
			basicType = "DC";
			break;

		//Arb wave types
		case SHAPE_STAIRCASE_UP:
			basicType = "ARB";
			arbType = "StairUp";
			break;

		case SHAPE_STAIRCASE_DOWN:
			basicType = "ARB";
			arbType = "StairDn";
			break;

		case SHAPE_STAIRCASE_UP_DOWN:
			basicType = "ARB";
			arbType = "StairUD";
			break;

		case SHAPE_SAWTOOTH_UP:
			basicType = "ARB";
			arbType = "Upramp";
			break;

		case SHAPE_SAWTOOTH_DOWN:
			basicType = "ARB";
			arbType = "Dnramp";
			break;

		case SHAPE_EXPONENTIAL_DECAY:
			basicType = "ARB";
			arbType = "ExpFal";
			break;

		case SHAPE_EXPONENTIAL_RISE:
			basicType = "ARB";
			arbType = "ExpRise";
			break;

		case SHAPE_LOG_DECAY:
			basicType = "ARB";
			arbType = "LogFall";
			break;

		case SHAPE_LOG_RISE:
			basicType = "ARB";
			arbType = "LogRise";
			break;

		case SHAPE_SQUARE_ROOT:
			basicType = "ARB";
			arbType = "Sqrt";
			break;

		case SHAPE_CUBE_ROOT:
			basicType = "ARB";
			arbType = "Root3";
			break;

		case SHAPE_QUADRATIC:
			basicType = "ARB";
			arbType = "X^2";
			break;

		case SHAPE_CUBIC:
			basicType = "ARB";
			arbType = "X^3";
			break;

		case SHAPE_SINC:
			basicType = "ARB";
			arbType = "Sinc";
			break;

		case SHAPE_GAUSSIAN:
			basicType = "ARB";
			arbType = "Gaussian";
			break;

		case SHAPE_DLORENTZ:
			basicType = "ARB";
			arbType = "DLorentz";
			break;

		case SHAPE_HAVERSINE:
			basicType = "ARB";
			arbType = "Haversine";
			break;

		case SHAPE_LORENTZ:
			basicType = "ARB";
			arbType = "Lorentz";
			break;

		case SHAPE_GAUSSIAN_PULSE:
			basicType = "ARB";
			arbType = "Gauspuls";
			break;

		case SHAPE_CARDIAC:
			basicType = "ARB";
			arbType = "Cardiac";
			break;

		case SHAPE_HAMMING:
			basicType = "ARB";
			arbType = "Hamming";
			break;

		case SHAPE_HANNING:
			basicType = "ARB";
			arbType = "Hanning";
			break;

		case SHAPE_KAISER:
			basicType = "ARB";
			arbType = "kaiser";	//yes, lowercase is intentional
			break;

		case SHAPE_BLACKMAN:
			basicType = "ARB";
			arbType = "Blackman";
			break;

		case SHAPE_GAUSSIAN_WINDOW:
			basicType = "ARB";
			arbType = "Gausswin";
			break;

		case SHAPE_TRIANGLE:
			basicType = "ARB";
			arbType = "Triangle";
			break;

		case SHAPE_HARRIS:
			basicType = "ARB";
			arbType = "BlackmanH";
			break;

		case SHAPE_BARTLETT:
			basicType = "ARB";
			arbType = "Bartlett-Hann";
			break;

		case SHAPE_TAN:
			basicType = "ARB";
			arbType = "Tan";
			break;

		case SHAPE_COT:
			basicType = "ARB";
			arbType = "Cot";
			break;

		case SHAPE_SEC:
			basicType = "ARB";
			arbType = "Sec";
			break;

		case SHAPE_CSC:
			basicType = "ARB";
			arbType = "Csc";
			break;

		case SHAPE_ASIN:
			basicType = "ARB";
			arbType = "Asin";
			break;

		case SHAPE_ACOS:
			basicType = "ARB";
			arbType = "Acos";
			break;

		case SHAPE_ATAN:
			basicType = "ARB";
			arbType = "Atan";
			break;

		case SHAPE_ACOT:
			basicType = "ARB";
			arbType = "Acot";
			break;

		//unsupported, ignore
		default:
			return;
	}

	//Select type
	sendWithAck(":FGEN:WAV:SHAP %s", basicType.c_str());
	if(basicType == "ARB")
	{	// TODO when  available in Magnova firmware
		//sendWithAck(":FGEN:WAV:SHAP %s", (arbmap[arbType].substr(1)).c_str());
	}

	//Update cache
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	// Duty cycle  is reset when changing shape
	m_awgDutyCycle.erase(chan);
	m_awgShape[chan] = shape;
}

FunctionGenerator::OutputImpedance MagnovaOscilloscope::GetFunctionChannelOutputImpedance(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgImpedance.find(chan) != m_awgImpedance.end())
			return m_awgImpedance[chan];
	}

	string load = converse(":FGEN:LOAD ?");

	FunctionGenerator::OutputImpedance imp = (load == "50") ? IMPEDANCE_50_OHM : IMPEDANCE_HIGH_Z;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgImpedance[chan] = imp;
	return m_awgImpedance[chan];
}

void MagnovaOscilloscope::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	string imp;
	if(z == IMPEDANCE_50_OHM)
		imp = "50OHM";
	else
		imp = "HIZ";

	sendWithAck(":FGEN:LOAD %s",imp.c_str());

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgImpedance.erase(chan);
}

bool MagnovaOscilloscope::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return true;
}


float MagnovaOscilloscope::GetFunctionChannelRiseTime(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgRiseTime.find(chan) != m_awgRiseTime.end())
			return m_awgRiseTime[chan];
	}

	string time = converse(":FGEN:WAV:PULS:RTIME ?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	float timef;
	if(sscanf(time.c_str(), "%f", &timef)!=1)
	{
		protocolError("invalid channel rise time value '%s'",time.c_str());
	}
	m_awgRiseTime[chan] = timef * FS_PER_SECOND;
	return m_awgRiseTime[chan];
}

void MagnovaOscilloscope::SetFunctionChannelRiseTime(int chan, float fs)
{
	sendWithAck(":FGEN:WAV:PULS:RTIME %.10f",fs * SECONDS_PER_FS);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgRiseTime.erase(chan);
}

float MagnovaOscilloscope::GetFunctionChannelFallTime(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgFallTime.find(chan) != m_awgFallTime.end())
			return m_awgFallTime[chan];
	}

	string time = converse(":FGEN:WAV:PULS:FTIME ?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	float timef;
	if(sscanf(time.c_str(), "%f", &timef)!=1)
	{
		protocolError("invalid channel fall time value '%s'",time.c_str());
	}
	m_awgFallTime[chan] = timef * FS_PER_SECOND;
	return m_awgFallTime[chan];
}

void MagnovaOscilloscope::SetFunctionChannelFallTime(int chan, float fs)
{
	sendWithAck(":FGEN:WAV:PULS:FTIME %.10f",fs * SECONDS_PER_FS);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgFallTime.erase(chan);
}
