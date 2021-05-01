/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
*                                                                                                                      *
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
 *
 * - Basic functionality for analog channels works.
 * - There is no feature detection because the scope does not support *OPT? (Request made)
 * - Digital channels are not implemented (code in here is leftover from LeCroy)
 * - Triggers are untested.
 * - Sampling lengths up to 10MSamples are supported. 50M and 100M need to be batched and will be
 *   horribly slow.
 *
 * SDS2000/5000/6000 port (c) 2021 Dave Marples. Note that this is only tested on SDS2000X+. If someone wants
 * to loan an SDS5000/6000 for testing that can be integrated. This file is derrived from the LeCroy driver.
 *
 * Note that this port replaces the previous Siglent driver, which was non-functional. That is available in the git
 * archive if needed.
 */

#include "scopehal.h"
#include "SiglentSCPIOscilloscope.h"
#include "base64.h"
#include <locale>
#include <immintrin.h>
#include <stdarg.h>
#include <omp.h>

#include "DropoutTrigger.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"

using namespace std;

static const struct
{
	const char* name;
	float val;
} c_threshold_table[] = {{"TTL", 1.5F}, {"CMOS", 2.5F}, {"LVCMOS33", 3.3F}, {"LVCMOS25", 1.5F}, {NULL, 0}};

static const int c_setting_delay = 50000;		   // Delay in uS required when setting parameters via SCPI
static const char* c_custom_thresh = "CUSTOM,";	   // Prepend string for custom digital threshold
static const float c_thresh_thresh = 0.01f;		   // Zero equivalence threshold for fp comparisons

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentSCPIOscilloscope::SiglentSCPIOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_hasFastSampleRate(false)
	, m_memoryDepthOption(0)
	, m_hasI2cTrigger(false)
	, m_hasSpiTrigger(false)
	, m_hasUartTrigger(false)
	, m_maxBandwidth(10000)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleRateValid(false)
	, m_sampleRate(1)
	, m_memoryDepthValid(false)
	, m_memoryDepth(1)
	, m_triggerOffsetValid(false)
	, m_triggerOffset(0)
	, m_interleaving(false)
	, m_interleavingValid(false)
	, m_highDefinition(false)
{
	//standard initialization
	FlushConfigCache();
	IdentifyHardware();
	DetectAnalogChannels();
	SharedCtorInit();
	DetectOptions();
}

string SiglentSCPIOscilloscope::converse(const char* fmt, ...)

{
	string ret;
	char opString[128];
	va_list va;
	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	m_transport->FlushRXBuffer();
	m_transport->SendCommand(opString);
	ret = m_transport->ReadReply();
	return ret;
}

void SiglentSCPIOscilloscope::sendOnly(const char* fmt, ...)

{
	char opString[128];
	va_list va;

	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	m_transport->FlushRXBuffer();
	m_transport->SendCommand(opString);
}

void SiglentSCPIOscilloscope::SharedCtorInit()
{
	m_digitalChannelCount = 0;

	//Add the external trigger input
	m_extTrigChannel =
		new OscilloscopeChannel(this, "Ext", OscilloscopeChannel::CHANNEL_TYPE_TRIGGER, "", 1, m_channels.size(), true);
	m_channels.push_back(m_extTrigChannel);

	//Desired format for waveform data
	//Only use increased bit depth if the scope actually puts content there!
	sendOnly(":WAVEFORM:WIDTH %s", m_highDefinition ? "WORD" : "BYTE");

	//Clear the state-change register to we get rid of any history we don't care about
	PollTrigger();
}

void SiglentSCPIOscilloscope::IdentifyHardware()
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
	m_maxBandwidth = 0;

	if(m_vendor.compare("Siglent Technologies") == 0)
	{
		if(m_model.compare(0, 4, "SDS2") == 0 && m_model.back() == 's')
		{
			m_modelid = MODEL_SIGLENT_SDS2000XP;

			m_maxBandwidth = 100;
			if(m_model.compare(4, 1, "2") == 0)
				m_maxBandwidth = 200;
			else if(m_model.compare(4, 1, "3") == 0)
				m_maxBandwidth = 350;
			if(m_model.compare(4, 1, "5") == 0)
				m_maxBandwidth = 500;
			return;
		}
		else if(m_model.compare(0, 4, "SDS5") == 0)
		{
			m_modelid = MODEL_SIGLENT_SDS5000X;

			m_maxBandwidth = 350;
			if(m_model.compare(5, 1, "5") == 0)
				m_maxBandwidth = 500;
			if(m_model.compare(5, 1, "0") == 0)
				m_maxBandwidth = 1000;
			return;
		}
	}
	LogWarning("Model \"%s\" is unknown, available sample rates/memory depths may not be properly detected\n",
		m_model.c_str());
}

void SiglentSCPIOscilloscope::DetectOptions()
{
	//AddDigitalChannels(16);

	/* SDS2000+ has no capability to find the options :-( */
	return;
}

/**
	@brief Creates digital channels for the oscilloscope
 */

void SiglentSCPIOscilloscope::AddDigitalChannels(unsigned int count)
{
	m_digitalChannelCount = count;
	m_digitalChannelBase = m_channels.size();

	char chn[32];
	for(unsigned int i = 0; i < count; i++)
	{
		snprintf(chn, sizeof(chn), "D%u", i);
		auto chan = new OscilloscopeChannel(this,
			chn,
			OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
			GetDefaultChannelColor(m_channels.size()),
			1,
			m_channels.size(),
			true);
		m_channels.push_back(chan);
		m_digitalChannels.push_back(chan);
	}
}

/**
	@brief Figures out how many analog channels we have, and add them to the device

 */
void SiglentSCPIOscilloscope::DetectAnalogChannels()
{
	int nchans = 1;

	// Char 7 of the model name is the number of channels
	if(m_model.length() > 7)
	{
		switch(m_model[6])
		{
			case '2':
				nchans = 2;
				break;
			case '4':
				nchans = 4;
				break;
		}
	}

	for(int i = 0; i < nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("C1");
		chname[1] += i;

		//Color the channels based on Siglents standard color sequence
		//yellow-pink-cyan-green-lightgreen
		string color = "#ffffff";
		switch(i % 4)
		{
			case 0:
				color = "#ffff00";
				break;

			case 1:
				color = "#ff6abc";
				break;

			case 2:
				color = "#00ffff";
				break;

			case 3:
				color = "#00c100";
				break;
		}

		//Create the channel
		m_channels.push_back(
			new OscilloscopeChannel(this, chname, OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, 1, i, true));
	}
	m_analogChannelCount = nchans;
}

SiglentSCPIOscilloscope::~SiglentSCPIOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device information

string SiglentSCPIOscilloscope::GetDriverNameInternal()
{
	return "siglent";
}

OscilloscopeChannel* SiglentSCPIOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

void SiglentSCPIOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if(m_trigger)
		delete m_trigger;
	m_trigger = NULL;

	m_channelVoltageRanges.clear();
	m_channelOffsets.clear();
	m_channelsEnabled.clear();
	m_channelDeskew.clear();
	m_channelDisplayNames.clear();
	m_probeIsActive.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;
	m_interleavingValid = false;
	m_meterModeValid = false;
}

/**
	@brief See what measurement capabilities we have
 */
unsigned int SiglentSCPIOscilloscope::GetMeasurementTypes()
{
	unsigned int type = 0;
	return type;
}

/**
	@brief See what features we have
 */
unsigned int SiglentSCPIOscilloscope::GetInstrumentTypes()
{
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasFunctionGen)
		type |= INST_FUNCTION;
	return type;
}

string SiglentSCPIOscilloscope::GetName()
{
	return m_model;
}

string SiglentSCPIOscilloscope::GetVendor()
{
	return m_vendor;
}

string SiglentSCPIOscilloscope::GetSerial()
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool SiglentSCPIOscilloscope::IsChannelEnabled(size_t i)
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

	//Need to lock the main mutex first to prevent deadlocks
	lock_guard<recursive_mutex> lock(m_mutex);
	lock_guard<recursive_mutex> lock2(m_cacheMutex);

	//Analog
	if(i < m_analogChannelCount)
	{
		//See if the channel is enabled, hide it if not
		string reply = converse(":CHANNEL%d:SWITCH?", i + 1);
		m_channelsEnabled[i] = (reply.find("OFF") == 0);	//may have a trailing newline, ignore that
	}

	//Digital
	else
	{
		//See if the channel is on
		size_t nchan = i - (m_analogChannelCount + 1);
		string str = converse(":DIGITAL:D%d?", nchan);
		m_channelsEnabled[i] = (str == "OFF") ? false : true;
	}

	return m_channelsEnabled[i];
}

void SiglentSCPIOscilloscope::EnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
	{
		sendOnly(":CHANNEL%d:SWITCH ON", i + 1);
	}

	//Trigger can't be enabled
	else if(i == m_extTrigChannel->GetIndex())
	{
	}

	//Digital channel
	else
	{
		sendOnly(":DIGITAL:D%d ON", i - (m_analogChannelCount + 1));
	}

	m_channelsEnabled[i] = true;
}

bool SiglentSCPIOscilloscope::CanEnableChannel(size_t i)
{
	// Can enable all channels except trigger
	return !(i == m_extTrigChannel->GetIndex());
}

void SiglentSCPIOscilloscope::DisableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_channelsEnabled[i] = false;

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
		sendOnly(":CHANNEL%d:TRACE OFF", i + 1);

	//Trigger can't be enabled
	else if(i == m_extTrigChannel->GetIndex())
	{
	}

	//Digital channel
	else
	{
		//Disable this channel
		sendOnly(":DIGITAL:D%d OFF", i - (m_analogChannelCount + 1));

		//If we have NO digital channels enabled, disable the appropriate digital bus

		//bool anyDigitalEnabled = false;
		//        for (uint32_t c=m_analogChannelCount+1+((chNum/8)*8); c<(m_analogChannelCount+1+((chNum/8)*8)+c_digiChannelsPerBus); c++)
		//          anyDigitalEnabled |= m_channelsEnabled[c];

		//        if(!anyDigitalEnabled)
		//sendOnly(":DIGITAL:BUS%d:DISP OFF",chNum/8);
	}
}

OscilloscopeChannel::CouplingType SiglentSCPIOscilloscope::GetChannelCoupling(size_t i)
{
	if(i >= m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	string replyType;
	string replyImp;

	lock_guard<recursive_mutex> lock(m_mutex);

	replyType = Trim(converse(":CHANNEL%d:COUPLING?", i + 1).substr(0, 2));
	replyImp = Trim(converse(":CHANNEL%d:IMPEDANCE?", i + 1).substr(0, 3));

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_probeIsActive[i] = false;

	if(replyType == "AC")
		return (replyImp == "FIFT") ? OscilloscopeChannel::COUPLE_AC_50 : OscilloscopeChannel::COUPLE_AC_1M;
	else if(replyType == "DC")
		return (replyImp == "FIFT") ? OscilloscopeChannel::COUPLE_DC_50 : OscilloscopeChannel::COUPLE_DC_1M;
	else if(replyType == "GN")
		return OscilloscopeChannel::COUPLE_GND;

	//invalid
	LogWarning("SiglentSCPIOscilloscope::GetChannelCoupling got invalid coupling [%s] [%s]\n",
		replyType.c_str(),
		replyImp.c_str());
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void SiglentSCPIOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(i >= m_analogChannelCount)
		return;

	//Get the old coupling value first.
	//This ensures that m_probeIsActive[i] is valid
	GetChannelCoupling(i);

	//If we have an active probe, don't touch the hardware config
	if(m_probeIsActive[i])
		return;

	lock_guard<recursive_mutex> lock(m_mutex);
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			sendOnly(":CHANNEL%d:COUPLING AC", i + 1);
			sendOnly(":CHANNEL%d:IMPEDANCE ONEMEG", i + 1);
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			sendOnly(":CHANNEL%d:COUPLING DC", i + 1);
			sendOnly(":CHANNEL%d:IMPEDANCE ONEMEG", i + 1);
			break;

		case OscilloscopeChannel::COUPLE_DC_50:
			sendOnly(":CHANNEL%d:COUPLING DC", i + 1);
			sendOnly(":CHANNEL%d:IMPEDANCE FIFTY", i + 1);
			break;

		case OscilloscopeChannel::COUPLE_AC_50:
			sendOnly(":CHANNEL%d:COUPLING AC", i + 1);
			sendOnly(":CHANNEL%d:IMPEDANCE FIFTY", i + 1);
			break;

		//treat unrecognized as ground
		case OscilloscopeChannel::COUPLE_GND:
		default:
			sendOnly(":CHANNEL%d:COUPLING GND", i + 1);
			break;
	}
}

double SiglentSCPIOscilloscope::GetChannelAttenuation(size_t i)
{
	if(i > m_analogChannelCount)
		return 1;

	//TODO: support ext/10
	if(i == m_extTrigChannel->GetIndex())
		return 1;

	lock_guard<recursive_mutex> lock(m_mutex);

	string reply = converse(":CHANNEL%d:PROBE?", i + 1);

	double d;
	sscanf(reply.c_str(), "%lf", &d);
	return d;
}

void SiglentSCPIOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if(i >= m_analogChannelCount)
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

	lock_guard<recursive_mutex> lock(m_mutex);
	sendOnly(":CHANNEL%d:PROBE %lf", i + 1, atten);
}

vector<unsigned int> SiglentSCPIOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;

	//"no limit"
	ret.push_back(0);

	//Supported by all models
	ret.push_back(20);

	if(m_maxBandwidth > 200)
		ret.push_back(200);

	return ret;
}

int SiglentSCPIOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i > m_analogChannelCount)
		return 0;

	lock_guard<recursive_mutex> lock(m_mutex);
	string reply = converse(":CHANNEL%d:BWLIMIT?", i + 1);
	if(reply == "FULL")
		return 0;
	else if(reply == "20M")
		return 20;
	else if(reply == "200M")
		return 200;

	LogWarning("SiglentSCPIOscilloscope::GetChannelCoupling got invalid bwlimit %s\n", reply.c_str());
	return 0;
}

void SiglentSCPIOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	switch(limit_mhz)
	{
		case 0:
			sendOnly(":CHANNEL%d:BWLIMIT FULL", i + 1);
			break;

		case 20:
			sendOnly(":CHANNEL%d:BWLIMIT 20M", i + 1);
			break;

		case 200:
			sendOnly(":CHANNEL%d:BWLIMIT 200M", i + 1);
			break;

		default:
			LogWarning("SiglentSCPIOscilloscope::invalid bwlimit set request (%dMhz)\n", limit_mhz);
	}
}

bool SiglentSCPIOscilloscope::CanInvert(size_t i)
{
	//All analog channels, and only analog channels, can be inverted
	return (i < m_analogChannelCount);
}

void SiglentSCPIOscilloscope::Invert(size_t i, bool invert)
{
	if(i >= m_analogChannelCount)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);
	sendOnly(":CHANNEL%d:INVERT %s", i + 1, invert ? "ON" : "OFF");
}

bool SiglentSCPIOscilloscope::IsInverted(size_t i)
{
	if(i >= m_analogChannelCount)
		return false;

	lock_guard<recursive_mutex> lock(m_mutex);
	auto reply = Trim(converse(":CHANNEL%d:INVERT?", i + 1));
	return (reply == "ON");
}

void SiglentSCPIOscilloscope::SetChannelDisplayName(size_t i, string name)
{
	auto chan = m_channels[i];

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return;

	//Update cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelDisplayNames[m_channels[i]] = name;
	}

	//Update in hardware
	lock_guard<recursive_mutex> lock(m_mutex);
	if(i < m_analogChannelCount)
	{
		sendOnly(":CHANNEL%ld:LABEL:TEXT \"%s\"", i + 1, name.c_str());
		sendOnly(":CHANNEL%ld:LABEL ON", i + 1);
	}
	else
	{
		sendOnly(":DIGITAL:LABEL%ld \"%s\"", i - (m_analogChannelCount + 1), name.c_str());
	}
}

string SiglentSCPIOscilloscope::GetChannelDisplayName(size_t i)
{
	auto chan = m_channels[i];

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return m_extTrigChannel->GetHwname();

	//Check cache first
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDisplayNames.find(chan) != m_channelDisplayNames.end())
			return m_channelDisplayNames[chan];
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	//Analog and digital channels use completely different namespaces, as usual.
	//Because clean, orthogonal APIs are apparently for losers?
	string name;
	if(i < m_analogChannelCount)
	{
		name = converse(":CHANNEL%d:LABEL:TEXT?", i + 1);

		// Remove "'s around the name
		if(name.length() > 2)
			name = name.substr(1, name.length() - 2);
	}
	else
	{
		name = converse(":DIGITAL:LABEL%d?", i - (m_analogChannelCount + 1));
		// Remove "'s around the name
		if(name.length() > 2)
			name = name.substr(1, name.length() - 2);
	}

	//Default to using hwname if no alias defined
	if(name == "")
		name = chan->GetHwname();

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDisplayNames[chan] = name;

	return name;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

bool SiglentSCPIOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

Oscilloscope::TriggerMode SiglentSCPIOscilloscope::PollTrigger()

{
	//Read the Internal State Change Register
	string sinr;
	lock_guard<recursive_mutex> lock(m_mutex);
	sinr = converse(":TRIGGER:STATUS?");

	//No waveform, but ready for one?
	if((sinr == "Arm") || (sinr == "Ready"))
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//Stopped, no data available
	if(sinr == "Stop")
	{
		if(m_triggerArmed)
		{
			m_triggerArmed = false;
			return TRIGGER_MODE_TRIGGERED;
		}
		else
			return TRIGGER_MODE_STOP;
	}
	return TRIGGER_MODE_RUN;
}

int SiglentSCPIOscilloscope::ReadWaveformBlock(uint32_t maxsize, char* data)

{
	char packetSizeSequence[17];
	uint32_t getLength;

	// Get size of this sequence
	m_transport->ReadRawData(16, (unsigned char*)packetSizeSequence);
	packetSizeSequence[16] = 0;
	LogTrace("INITIAL PACKET [%s]\n", packetSizeSequence);
	getLength = atoi(&packetSizeSequence[7]);

	// Now get the data
	m_transport->ReadRawData((getLength > maxsize) ? maxsize : getLength, (unsigned char*)data);

	return getLength;
}

/**
	@brief Optimized function for checking channel enable status en masse with less round trips to the scope
 */
void SiglentSCPIOscilloscope::BulkCheckChannelEnableState()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Check enable state in the cache.
	vector<int> uncached;
	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if(m_channelsEnabled.find(i) == m_channelsEnabled.end())
			uncached.push_back(i);
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	for(auto i : uncached)
	{
		string reply = converse(":CHANNEL%d:SWITCH?", i + 1);
		if(reply == "OFF")
			m_channelsEnabled[i] = false;
		else if(reply == "ON")
			m_channelsEnabled[i] = true;
		else
			LogWarning("BulkCheckChannelEnableState: Unrecognised reply [%s]\n", reply.c_str());
	}

	//Check digital status
	for(unsigned int i = 0; i < m_digitalChannelCount; i++)
	{
		string reply = converse(":DIGITAL:D%d?", i);
		if(reply == "ON")
		{
			m_channelsEnabled[m_digitalChannels[i]->GetIndex()] = true;
		}
		else if(reply == "OFF")
		{
			m_channelsEnabled[m_digitalChannels[i]->GetIndex()] = false;
		}
		else
			LogWarning("BulkCheckChannelEnableState: Unrecognised reply [%s]\n", reply.c_str());
	}
}

bool SiglentSCPIOscilloscope::ReadWavedescs(
	char wavedescs[MAX_ANALOG][WAVEDESC_SIZE], bool* enabled, unsigned int& firstEnabledChannel, bool& any_enabled)
{
	BulkCheckChannelEnableState();
	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		enabled[i] = IsChannelEnabled(i);
		any_enabled |= enabled[i];
	}

	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if(enabled[i] || (!any_enabled && i == 0))
		{
			if(firstEnabledChannel == UINT_MAX)
				firstEnabledChannel = i;

			sendOnly(":WAVEFORM:SOURCE C%d", i + 1);
			sendOnly(":WAVEFORM:PREAMBLE?");
			if(WAVEDESC_SIZE != ReadWaveformBlock(WAVEDESC_SIZE, wavedescs[i]))
				LogError("ReadWaveformBlock for wavedesc %u failed\n", i);

			// I have no idea why this is needed, but it certainly is
			m_transport->ReadReply();
		}
	}

	return true;
}

void SiglentSCPIOscilloscope::RequestWaveforms(bool* enabled, uint32_t num_sequences, bool /* denabled */)
{
	//Ask for all analog waveforms
	// This routine does the asking, but doesn't catch the data as it comes back
	bool sent_wavetime = false;
	lock_guard<recursive_mutex> lock(m_mutex);

	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			sendOnly(":WAVEFORM:SOURCE C%d", i + 1);
			//If a multi-segment capture, ask for the trigger time data
			if((num_sequences > 1) && !sent_wavetime)
			{
				sendOnly("%s:HISTORY TIME?", m_channels[i]->GetHwname());
				sent_wavetime = true;
			}

			//Ask for the data
			sendOnly(":WAVEFORM:DATA?");
		}
	}

	//Ask for the digital waveforms
	//if(denabled)
	// 	sendOnly("Digital1:WF?");
}

time_t SiglentSCPIOscilloscope::ExtractTimestamp(unsigned char* wavedesc, double& basetime)
{
	/*
                TIMESTAMP is shown as Reserved In Siglent data format.
                This information is from LeCroy which uses the same wavedesc header.
		Timestamp is a somewhat complex format that needs some shuffling around.
		Timestamp starts at offset 296 bytes in the wavedesc
		(296-303)	double seconds
		(304)		byte minutes
		(305)		byte hours
		(306)		byte days
		(307)		byte months
		(308-309)	uint16 year

		TODO: during startup, query instrument for its current time zone
		since the wavedesc reports instment local time
	 */
	//Yes, this cast is intentional.
	//It assumes you're on a little endian system using IEEE754 64-bit float, but that applies to everything we support.
	//cppcheck-suppress invalidPointerCast
	double fseconds = *reinterpret_cast<const double*>(wavedesc + 296);
	uint8_t seconds = floor(fseconds);
	basetime = fseconds - seconds;
	time_t tnow = time(NULL);
	struct tm tstruc;

#ifdef _WIN32
	localtime_s(&tstruc, &tnow);
#else
	localtime_r(&tnow, &tstruc);
#endif

	//Convert the instrument time to a string, then back to a tm
	//Is there a better way to do this???
	//Naively poking "struct tm" fields gives incorrect results (scopehal-apps:#52)
	//Maybe because tm_yday is inconsistent?
	char tblock[64] = {0};
	snprintf(tblock,
		sizeof(tblock),
		"%d-%d-%d %d:%02d:%02d",
		*reinterpret_cast<uint16_t*>(wavedesc + 308),
		wavedesc[307],
		wavedesc[306],
		wavedesc[305],
		wavedesc[304],
		seconds);
	locale cur_locale;
	auto& tget = use_facet<time_get<char>>(cur_locale);
	istringstream stream(tblock);
	ios::iostate state;
	char format[] = "%F %T";
	tget.get(stream, time_get<char>::iter_type(), stream, state, &tstruc, format, format + strlen(format));
	return mktime(&tstruc);
}

vector<WaveformBase*> SiglentSCPIOscilloscope::ProcessAnalogWaveform(const char* data,
	size_t datalen,
	char* wavedesc,
	uint32_t num_sequences,
	time_t ttime,
	double basetime,
	double* wavetime,
                                                                     int /* ch */)
{
	vector<WaveformBase*> ret;

	//Parse the wavedesc headers
	auto pdesc = wavedesc;

	//cppcheck-suppress invalidPointerCast
	float v_gain = *reinterpret_cast<float*>(pdesc + 156);

	//cppcheck-suppress invalidPointerCast
	float v_off = *reinterpret_cast<float*>(pdesc + 160);

	//cppcheck-suppress invalidPointerCast
	float v_probefactor = *reinterpret_cast<float*>(pdesc + 328);

	//cppcheck-suppress invalidPointerCast
	float interval = *reinterpret_cast<float*>(pdesc + 176) * FS_PER_SECOND;

	//cppcheck-suppress invalidPointerCast
	double h_off = *reinterpret_cast<double*>(pdesc + 180) * FS_PER_SECOND;	   //fs from start of waveform to trigger

	double h_off_frac = fmodf(h_off, interval);	   //fractional sample position, in fs

	//double h_off_frac = 0;//((interval*datalen)/2)+h_off;

	if(h_off_frac < 0)
		h_off_frac = h_off;	   //interval + h_off_frac;	   //double h_unit = *reinterpret_cast<double*>(pdesc + 244);

	//Raw waveform data
	size_t num_samples;
	if(m_highDefinition)
		num_samples = datalen / 2;
	else
		num_samples = datalen;
	size_t num_per_segment = num_samples / num_sequences;
	int16_t* wdata = (int16_t*)&data[0];
	int8_t* bdata = (int8_t*)&data[0];

	// SDS2000X+ and SDS5000X have 30 codes per div. Todo; SDS6000X has 425.
	// We also need to accomodate probe attenuation here.
	v_gain = v_gain * v_probefactor / 30;

	// Vertical offset is also scaled by the probefactor
	v_off = v_off * v_probefactor;

	// Update channel voltages and offsets based on what is in this wavedesc
	// m_channelVoltageRanges[ch] = v_gain * v_probefactor * 30 * 8;
	// m_channelOffsets[ch] = v_off;
	// m_triggerOffset = ((interval * datalen) / 2) + h_off;
	// m_triggerOffsetValid = true;

	LogTrace("\nV_Gain=%f, V_Off=%f, interval=%f, h_off=%f, h_off_frac=%f, datalen=%ld\n",
		v_gain,
		v_off,
		interval,
		h_off,
		h_off_frac,
		datalen);

	for(size_t j = 0; j < num_sequences; j++)
	{
		//Set up the capture we're going to store our data into
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = round(interval);

		cap->m_triggerPhase = h_off_frac;
		cap->m_startTimestamp = ttime;
		cap->m_densePacked = true;

		//Parse the time
		if(num_sequences > 1)
			cap->m_startFemtoseconds = static_cast<int64_t>((basetime + wavetime[j * 2]) * FS_PER_SECOND);
		else
			cap->m_startFemtoseconds = static_cast<int64_t>(basetime * FS_PER_SECOND);

		cap->Resize(num_per_segment);

		//Convert raw ADC samples to volts
		//TODO: Optimized AVX conversion for 16-bit samples
		float* samps = reinterpret_cast<float*>(&cap->m_samples[0]);
		if(m_highDefinition)
		{
			int16_t* base = wdata + j * num_per_segment;

			for(unsigned int k = 0; k < num_per_segment; k++)
			{
				cap->m_offsets[k] = k;
				cap->m_durations[k] = 1;
				samps[k] = base[k] * v_gain - v_off;
			}
		}
		else
		{
			if(g_hasAvx2)
			{
				//Divide large waveforms (>1M points) into blocks and multithread them
				//TODO: tune split
				if(num_per_segment > 1000000)
				{
					//Round blocks to multiples of 32 samples for clean vectorization
					size_t numblocks = omp_get_max_threads();
					size_t lastblock = numblocks - 1;
					size_t blocksize = num_per_segment / numblocks;
					blocksize = blocksize - (blocksize % 32);
#pragma omp parallel for
					for(size_t i = 0; i < numblocks; i++)
					{
						//Last block gets any extra that didn't divide evenly
						size_t nsamp = blocksize;
						if(i == lastblock)
							nsamp = num_per_segment - i * blocksize;

						Convert8BitSamplesAVX2((int64_t*)&cap->m_offsets[i * blocksize],
							(int64_t*)&cap->m_durations[i * blocksize],
							samps + i * blocksize,
							bdata + j * num_per_segment + i * blocksize,
							v_gain,
							v_off,
							nsamp,
							i * blocksize);
					}
				}

				//Small waveforms get done single threaded to avoid overhead
				else
				{
					Convert8BitSamplesAVX2((int64_t*)&cap->m_offsets[0],
						(int64_t*)&cap->m_durations[0],
						samps,
						bdata + j * num_per_segment,
						v_gain,
						v_off,
						num_per_segment,
						0);
				}
			}
			else
			{
				Convert8BitSamples((int64_t*)&cap->m_offsets[0],
					(int64_t*)&cap->m_durations[0],
					samps,
					bdata + j * num_per_segment,
					v_gain,
					v_off,
					num_per_segment,
					0);
			}
		}

		ret.push_back(cap);
	}

	return ret;
}

/**
	@brief Converts 8-bit ADC samples to floating point
 */
void SiglentSCPIOscilloscope::Convert8BitSamples(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase)
{
	for(unsigned int k = 0; k < count; k++)
	{
		offs[k] = ibase + k;
		durs[k] = 1;
		pout[k] = pin[k] * gain - offset;
	}
}

/**
	@brief Optimized version of Convert8BitSamples()
 */
__attribute__((target("avx2"))) void SiglentSCPIOscilloscope::Convert8BitSamplesAVX2(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase)
{
	unsigned int end = count - (count % 32);

	int64_t __attribute__((aligned(32))) ones_x4[] = {1, 1, 1, 1};
	int64_t __attribute__((aligned(32))) fours_x4[] = {4, 4, 4, 4};
	int64_t __attribute__((aligned(32))) count_x4[] = {ibase + 0, ibase + 1, ibase + 2, ibase + 3};

	__m256i all_ones = _mm256_load_si256(reinterpret_cast<__m256i*>(ones_x4));
	__m256i all_fours = _mm256_load_si256(reinterpret_cast<__m256i*>(fours_x4));
	__m256i counts = _mm256_load_si256(reinterpret_cast<__m256i*>(count_x4));

	__m256 gains = {gain, gain, gain, gain, gain, gain, gain, gain};
	__m256 offsets = {offset, offset, offset, offset, offset, offset, offset, offset};

	for(unsigned int k = 0; k < end; k += 32)
	{
		//Load all 32 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m256i raw_samples = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k));

		//Fill duration
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 4), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 8), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 12), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 16), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 20), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 24), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 28), all_ones);

		//Extract the low and high 16 samples from the block
		__m128i block01_x8 = _mm256_extracti128_si256(raw_samples, 0);
		__m128i block23_x8 = _mm256_extracti128_si256(raw_samples, 1);

		//Swap the low and high halves of these vectors
		//Ugly casting needed because all permute instrinsics expect float/double datatypes
		__m128i block10_x8 = _mm_castpd_si128(_mm_permute_pd(_mm_castsi128_pd(block01_x8), 1));
		__m128i block32_x8 = _mm_castpd_si128(_mm_permute_pd(_mm_castsi128_pd(block23_x8), 1));

		//Divide into blocks of 8 samples and sign extend to 32 bit
		__m256i block0_int = _mm256_cvtepi8_epi32(block01_x8);
		__m256i block1_int = _mm256_cvtepi8_epi32(block10_x8);
		__m256i block2_int = _mm256_cvtepi8_epi32(block23_x8);
		__m256i block3_int = _mm256_cvtepi8_epi32(block32_x8);

		//Fill offset
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 4), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 8), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 12), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 16), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 20), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 24), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 28), counts);
		counts = _mm256_add_epi64(counts, all_fours);

		//Convert the 32-bit int blocks to float.
		//Apparently there's no direct epi8 to ps conversion instruction.
		__m256 block0_float = _mm256_cvtepi32_ps(block0_int);
		__m256 block1_float = _mm256_cvtepi32_ps(block1_int);
		__m256 block2_float = _mm256_cvtepi32_ps(block2_int);
		__m256 block3_float = _mm256_cvtepi32_ps(block3_int);

		//Woo! We've finally got floating point data. Now we can do the fun part.
		block0_float = _mm256_mul_ps(block0_float, gains);
		block1_float = _mm256_mul_ps(block1_float, gains);
		block2_float = _mm256_mul_ps(block2_float, gains);
		block3_float = _mm256_mul_ps(block3_float, gains);

		block0_float = _mm256_sub_ps(block0_float, offsets);
		block1_float = _mm256_sub_ps(block1_float, offsets);
		block2_float = _mm256_sub_ps(block2_float, offsets);
		block3_float = _mm256_sub_ps(block3_float, offsets);

		//All done, store back to the output buffer
		_mm256_store_ps(pout + k, block0_float);
		_mm256_store_ps(pout + k + 8, block1_float);
		_mm256_store_ps(pout + k + 16, block2_float);
		_mm256_store_ps(pout + k + 24, block3_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(unsigned int k = end; k < count; k++)
	{
		offs[k] = ibase + k;
		durs[k] = 1;
		pout[k] = pin[k] * gain - offset;
	}
}

map<int, DigitalWaveform*> SiglentSCPIOscilloscope::ProcessDigitalWaveform(string& data)
{
	map<int, DigitalWaveform*> ret;

	// Digital channels not yet implemented
	return ret;

	//See what channels are enabled
	string tmp = data.substr(data.find("SelectedLines=") + 14);
	tmp = tmp.substr(0, 16);
	bool enabledChannels[16];
	for(int i = 0; i < 16; i++)
		enabledChannels[i] = (tmp[i] == '1');

	//Quick and dirty string searching. We only care about a small fraction of the XML
	//so no sense bringing in a full parser.
	tmp = data.substr(data.find("<HorPerStep>") + 12);
	tmp = tmp.substr(0, tmp.find("</HorPerStep>"));
	float interval = atof(tmp.c_str()) * FS_PER_SECOND;
	//LogDebug("Sample interval: %.2f fs\n", interval);

	tmp = data.substr(data.find("<NumSamples>") + 12);
	tmp = tmp.substr(0, tmp.find("</NumSamples>"));
	size_t num_samples = atoi(tmp.c_str());
	//LogDebug("Expecting %d samples\n", num_samples);

	//Extract the raw trigger timestamp (nanoseconds since Jan 1 2000)
	tmp = data.substr(data.find("<FirstEventTime>") + 16);
	tmp = tmp.substr(0, tmp.find("</FirstEventTime>"));
	int64_t timestamp;
	if(1 != sscanf(tmp.c_str(), "%ld", &timestamp))
		return ret;

	//Get the client's local time.
	//All we need from this is to know whether DST is active
	tm now;
	time_t tnow;
	time(&tnow);
	localtime_r(&tnow, &now);

	//Convert Jan 1 2000 in the client's local time zone (assuming this is the same as instrument time) to Unix time.
	//Note that the instrument time zone conversion seems to be broken and not handle DST offsets right.
	//Move the epoch by an hour if we're currently in DST to compensate.
	tm epoch;
	epoch.tm_sec = 0;
	epoch.tm_min = 0;
	epoch.tm_hour = 0;
	epoch.tm_mday = 1;
	epoch.tm_mon = 0;
	epoch.tm_year = 100;
	epoch.tm_wday = 6;	  //Jan 1 2000 was a Saturday
	epoch.tm_yday = 0;
	epoch.tm_isdst = now.tm_isdst;
	time_t epoch_stamp = mktime(&epoch);

	//Pull out nanoseconds from the timestamp and convert to femtoseconds since that's the scopehal fine time unit
	const int64_t ns_per_sec = 1000000000;
	int64_t start_ns = timestamp % ns_per_sec;
	int64_t start_fs = 1000000 * start_ns;
	int64_t start_sec = (timestamp - start_ns) / ns_per_sec;
	time_t start_time = epoch_stamp + start_sec;

	//Pull out the actual binary data (Base64 coded)
	tmp = data.substr(data.find("<BinaryData>") + 12);
	tmp = tmp.substr(0, tmp.find("</BinaryData>"));

	//Decode the base64
	base64_decodestate bstate;
	base64_init_decodestate(&bstate);
	unsigned char* block = new unsigned char[tmp.length()];	   //base64 is smaller than plaintext, leave room
	base64_decode_block(tmp.c_str(), tmp.length(), (char*)block, &bstate);

	//We have each channel's data from start to finish before the next (no interleaving).
	//TODO: Multithread across waveforms
	unsigned int icapchan = 0;
	for(unsigned int i = 0; i < m_digitalChannelCount; i++)
	{
		if(enabledChannels[i])
		{
			DigitalWaveform* cap = new DigitalWaveform;
			cap->m_timescale = interval;
			cap->m_densePacked = true;

			//Capture timestamp
			cap->m_startTimestamp = start_time;
			cap->m_startFemtoseconds = start_fs;

			//Preallocate memory assuming no deduplication possible
			cap->Resize(num_samples);

			//Save the first sample (can't merge with sample -1 because that doesn't exist)
			size_t base = icapchan * num_samples;
			size_t k = 0;
			cap->m_offsets[0] = 0;
			cap->m_durations[0] = 1;
			cap->m_samples[0] = block[base];

			//Read and de-duplicate the other samples
			//TODO: can we vectorize this somehow?
			bool last = block[base];
			for(size_t j = 1; j < num_samples; j++)
			{
				bool sample = block[base + j];

				//Deduplicate consecutive samples with same value
				//FIXME: temporary workaround for rendering bugs
				//if(last == sample)
				if((last == sample) && ((j + 3) < num_samples))
					cap->m_durations[k]++;

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

			//See how much space we saved
			/*
			LogDebug("%s: %zu samples deduplicated to %zu (%.1f %%)\n",
				m_digitalChannels[i]->GetDisplayName().c_str(),
				num_samples,
				k,
				(k * 100.0f) / num_samples);
			*/

			//Done, save data and go on to next
			ret[m_digitalChannels[i]->GetIndex()] = cap;
			icapchan++;
		}

		//No data here for us!
		else
			ret[m_digitalChannels[i]->GetIndex()] = NULL;
	}
	delete[] block;
	return ret;
}

bool SiglentSCPIOscilloscope::AcquireData()
{
	//State for this acquisition (may be more than one waveform)
	uint32_t num_sequences = 1;
	map<int, vector<WaveformBase*>> pending_waveforms;
	double start = GetTime();
	time_t ttime = 0;
	double basetime = 0;
	bool denabled = false;
	string wavetime;
	bool enabled[8] = {false};
	double* pwtime = NULL;
	char tmp[128];

	//Acquire the data (but don't parse it)
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		start = GetTime();
		//Get the wavedescs for all channels
		unsigned int firstEnabledChannel = UINT_MAX;
		bool any_enabled = true;

		if(!ReadWavedescs(m_wavedescs, enabled, firstEnabledChannel, any_enabled))
			return false;

		//Grab the WAVEDESC from the first enabled channel
		unsigned char* pdesc = NULL;
		for(unsigned int i = 0; i < m_analogChannelCount; i++)
		{
			if(enabled[i] || (!any_enabled && i == 0))
			{
				pdesc = (unsigned char*)(&m_wavedescs[i][0]);
				break;
			}
		}

		//See if any digital channels are enabled
		if(m_digitalChannelCount > 0)
		{
			m_cacheMutex.lock();
			for(size_t i = 0; i < m_digitalChannels.size(); i++)
			{
				if(m_channelsEnabled[m_digitalChannels[i]->GetIndex()])
				{
					denabled = true;
					break;
				}
			}
			m_cacheMutex.unlock();
		}

		//Pull sequence count out of the WAVEDESC if we have analog channels active
		if(pdesc)
		{
			uint32_t trigtime_len = *reinterpret_cast<uint32_t*>(pdesc + 48);
			if(trigtime_len > 0)
				num_sequences = trigtime_len / 16;
		}

		//No WAVEDESCs, look at digital channels
		else
		{
			//TODO: support sequence capture of digital channels if the instrument supports this
			//(need to look into it)
			if(denabled)
				num_sequences = 1;

			//no enabled channels. abort
			else
				return false;
		}

		//Ask for every enabled channel up front, so the scope can send us the next while we parse the first
		RequestWaveforms(enabled, num_sequences, denabled);

		if(pdesc)
		{
			// THIS SECTION IS UNTESTED
			//Figure out when the first trigger happened.
			//Read the timestamps if we're doing segmented capture
			ttime = ExtractTimestamp(pdesc, basetime);
			if(num_sequences > 1)
				wavetime = m_transport->ReadReply();
			pwtime = reinterpret_cast<double*>(&wavetime[16]);	  //skip 16-byte SCPI header

			//Read the data from each analog waveform
			for(unsigned int i = 0; i < m_analogChannelCount; i++)
			{
				//                          m_transport->SendCommand(":WAVEFORM:SOURCE "+to_string(i+1));
				//                          m_transport->SendCommand(":WAVEFORM:DATA?");
				if(enabled[i])
				{
					m_analogWaveformDataSize[i] = ReadWaveformBlock(WAVEFORM_SIZE, m_analogWaveformData[i]);
					// This is the 0x0a0a at the end
					m_transport->ReadRawData(2, (unsigned char*)tmp);
				}
			}
		}

		//Read the data from the digital waveforms, if enabled
		if(denabled)
		{
			if(!ReadWaveformBlock(WAVEFORM_SIZE, m_digitalWaveformDataBytes))
			{
				LogDebug("failed to download digital waveform\n");
				return false;
			}
		}
	}

	//At this point all data has been read so the scope is free to go do its thing while we crunch the results.
	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		//		lock_guard<recursive_mutex> lock(m_mutex);
		sendOnly(":TRIGGER:MODE SINGLE");
		m_triggerArmed = true;
	}

	//Process analog waveforms
	vector<vector<WaveformBase*>> waveforms;
	waveforms.resize(m_analogChannelCount);
	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			waveforms[i] = ProcessAnalogWaveform(&m_analogWaveformData[i][0],
				m_analogWaveformDataSize[i],
				&m_wavedescs[i][0],
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
		if(!enabled[i])
			continue;

		//Done, update the data
		for(size_t j = 0; j < num_sequences; j++)
			pending_waveforms[i].push_back(waveforms[i][j]);
	}
	//TODO: proper support for sequenced capture when digital channels are active
	// if(denabled)
	// {
	// 	//This is a weird XML-y format but I can't find any other way to get it :(
	// 	map<int, DigitalWaveform*> digwaves = ProcessDigitalWaveform(m_digitalWaveformData);

	// 	//Done, update the data
	// 	for(auto it : digwaves)
	// 		pending_waveforms[it.first].push_back(it.second);
	// }

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	for(size_t i = 0; i < num_sequences; i++)
	{
		SequenceSet s;
		for(size_t j = 0; j < m_channels.size(); j++)
		{
			if(pending_waveforms.find(j) != pending_waveforms.end())
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	double dt = GetTime() - start;
	LogTrace("Waveform download and processing took %.3f ms\n", dt * 1000);
	return true;
}

void SiglentSCPIOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	sendOnly(":TRIGGER:MODE STOP");
	sendOnly(":TRIGGER:MODE SINGLE");	 //always do single captures, just re-trigger
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void SiglentSCPIOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//LogDebug("Start single trigger\n");
	sendOnly(":TRIGGER:MODE STOP");
	sendOnly(":TRIGGER:MODE SINGLE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void SiglentSCPIOscilloscope::Stop()
{
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		sendOnly(":TRIGGER:MODE STOP");
	}

	m_triggerArmed = false;
	m_triggerOneShot = true;

	//Clear out any pending data (the user doesn't want it, and we don't want stale stuff hanging around)
	ClearPendingWaveforms();
}

double SiglentSCPIOscilloscope::GetChannelOffset(size_t i)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	string reply = converse(":CHANNEL%ld:OFFSET?", i + 1);
	double offset;
	sscanf(reply.c_str(), "%lf", &offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void SiglentSCPIOscilloscope::SetChannelOffset(size_t i, double offset)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return;

	{
		lock_guard<recursive_mutex> lock2(m_mutex);
		sendOnly(":CHANNEL%ld:OFFSET %e", i + 1, offset);
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

double SiglentSCPIOscilloscope::GetChannelVoltageRange(size_t i)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	string reply = converse(":CHANNEL%d:SCALE?", i + 1);
	double volts_per_div;
	sscanf(reply.c_str(), "%lf", &volts_per_div);

	double v = volts_per_div * 8;	 //plot is 8 divisions high
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = v;
	return v;
}

void SiglentSCPIOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	double vdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	sendOnly(":CHANNEL%ld:SCALE %.4f", i + 1, vdiv);
}

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret = {10 * 1000,
		20 * 1000,
		50 * 1000,
		100 * 1000,
		200 * 1000,
		500 * 1000,
		1 * 1000 * 1000,
		2 * 1000 * 1000,
		5 * 1000 * 1000,
		10 * 1000 * 1000,
		20 * 1000 * 1000,
		50 * 1000 * 1000,
		100 * 1000 * 1000,
		200 * 1000 * 1000,
		500 * 1000 * 1000,
		1 * 1000 * 1000 * 1000,
		2 * 1000 * 1000 * 1000};
	return ret;
}

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleRatesInterleaved()
{
	vector<uint64_t> ret = {};
	GetSampleRatesNonInterleaved();
	return ret;
}

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret = {};
	return ret;
}

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleDepthsInterleaved()
{
	vector<uint64_t> ret = {};
	return ret;
}

set<SiglentSCPIOscilloscope::InterleaveConflict> SiglentSCPIOscilloscope::GetInterleaveConflicts()
{
	set<InterleaveConflict> ret;

	//All scopes normally interleave channels 1/2 and 3/4.
	//If both channels in either pair is in use, that's a problem.
	ret.emplace(InterleaveConflict(m_channels[0], m_channels[1]));
	if(m_analogChannelCount > 2)
		ret.emplace(InterleaveConflict(m_channels[2], m_channels[3]));

	return ret;
}

uint64_t SiglentSCPIOscilloscope::GetSampleRate()
{
	double f;
	if(!m_sampleRateValid)
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		string reply = converse(":ACQUIRE:SRATE?");
		sscanf(reply.c_str(), "%lf", &f);
		m_sampleRate = static_cast<int64_t>(f);
		m_sampleRateValid = true;
	}

	return m_sampleRate;
}

uint64_t SiglentSCPIOscilloscope::GetSampleDepth()
{
	double f;
	if(!m_memoryDepthValid)
	{
		//:AQUIRE:MDEPTH can sometimes return incorrect values! It returns the *cap* on memory depth,
		// not the *actual* memory depth....we don't know that until we've collected samples

		// What you see below is the only observed method that seems to reliably get the *actual* memory depth.
		lock_guard<recursive_mutex> lock(m_mutex);
		string reply = converse(":ACQUIRE:MDEPTH?");
		f = Unit(Unit::UNIT_SAMPLEDEPTH).ParseString(reply);
		m_memoryDepth = static_cast<int64_t>(f);
		m_memoryDepthValid = true;
	}

	return m_memoryDepth;
}

void SiglentSCPIOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(depth)
	{
		case 10000:
			sendOnly("ACQUIRE:MDEPTH 10k");
			break;
		case 20000:
			sendOnly("ACQUIRE:MDEPTH 20k");
			break;
		case 100000:
			sendOnly("ACQUIRE:MDEPTH 100k");
			break;
		case 200000:
			sendOnly("ACQUIRE:MDEPTH 200k");
			break;
		case 1000000:
			sendOnly("ACQUIRE:MDEPTH 1M");
			break;
		case 2000000:
			sendOnly("ACQUIRE:MDEPTH 2M");
			break;
		case 10000000:
			sendOnly("ACQUIRE:MDEPTH 10M");
			break;

			// We don't yet support memory depths that need to be transferred in chunks
		case 20000000:
			//sendOnly("ACQUIRE:MDEPTH 20M");
			//	break;
		case 50000000:
			//	sendOnly("ACQUIRE:MDEPTH 50M");
			//	break;
		case 100000000:
			//	sendOnly("ACQUIRE:MDEPTH 100M");
			//	break;
		case 200000000:
			//	sendOnly("ACQUIRE:MDEPTH 200M");
			//	break;
		default:
			LogError("Invalid memory depth for channel: %lu\n", depth);
	}

	m_memoryDepthValid = false;
}

void SiglentSCPIOscilloscope::SetSampleRate(uint64_t rate)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_sampleRate = rate;
	m_sampleRateValid = false;

	m_memoryDepthValid = false;
	double sampletime = GetSampleDepth() / (double)rate;
	sendOnly(":TIMEBASE:SCALE %e", sampletime / 10);
	m_memoryDepthValid = false;
}

void SiglentSCPIOscilloscope::EnableTriggerOutput()
{
	LogWarning("EnableTriggerOutput not implemented\n");
}

void SiglentSCPIOscilloscope::SetUseExternalRefclk(bool /*external*/)
{
	LogWarning("SetUseExternalRefclk not implemented\n");
}

void SiglentSCPIOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Siglents standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	sendOnly(":TIMEBASE:DELAY %e", (offset - halfwidth) * SECONDS_PER_FS);

	//Don't update the cache because the scope is likely to round the offset we ask for.
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_triggerOffsetValid = false;
}

int64_t SiglentSCPIOscilloscope::GetTriggerOffset()
{
	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_triggerOffsetValid)
			return m_triggerOffset;
	}

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		reply = converse(":TIMEBASE:DELAY?");
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Result comes back in scientific notation
	double sec;
	sscanf(reply.c_str(), "%le", &sec);
	m_triggerOffset = static_cast<int64_t>(round(sec * FS_PER_SECOND));

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));
	m_triggerOffset += halfwidth;

	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

void SiglentSCPIOscilloscope::SetDeskewForChannel(size_t channel, int64_t skew)
{
	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	sendOnly(":CHANNEL%ld:SKEW %e", channel, skew * SECONDS_PER_FS);

	//Update cache
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew;
}

int64_t SiglentSCPIOscilloscope::GetDeskewForChannel(size_t channel)
{
	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return 0;

	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDeskew.find(channel) != m_channelDeskew.end())
			return m_channelDeskew[channel];
	}

	//Read the deskew
	lock_guard<recursive_mutex> lock(m_mutex);
	string reply = converse(":CHANNEL%ld:SKEW?", channel + 1);

	//Value comes back as floating point ps
	float skew;
	sscanf(reply.c_str(), "%f", &skew);
	int64_t skew_ps = round(skew * FS_PER_SECOND);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew_ps;

	return skew_ps;
}

bool SiglentSCPIOscilloscope::IsInterleaving()
{
	LogWarning("IsInterleaving is not implemented\n");
	return false;
}

bool SiglentSCPIOscilloscope::SetInterleaving(bool /* combine*/)
{
	LogWarning("SetInterleaving is not implemented\n");
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Analog bank configuration

bool SiglentSCPIOscilloscope::IsADCModeConfigurable()
{
	return false;
}

vector<string> SiglentSCPIOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> v;
	LogWarning("GetADCModeNames is not implemented\n");
	return v;
}

size_t SiglentSCPIOscilloscope::GetADCMode(size_t /*channel*/)
{
	return 0;
}

void SiglentSCPIOscilloscope::SetADCMode(size_t /*channel*/, size_t /*mode*/)
{
	return;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> SiglentSCPIOscilloscope::GetDigitalBanks()
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

Oscilloscope::DigitalBank SiglentSCPIOscilloscope::GetDigitalBank(size_t channel)
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

bool SiglentSCPIOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool SiglentSCPIOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float SiglentSCPIOscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	LogWarning("GetDigitalHysteresis is not implemented\n");
	return 0;
}

float SiglentSCPIOscilloscope::GetDigitalThreshold(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	channel -= m_analogChannelCount + 1;

	string r = converse(":DIGITAL:THRESHOLD%d?", (channel / 8) + 1).c_str();

	// Look through the threshold table to see if theres a string match, return it if so
	uint32_t i = 0;
	while((c_threshold_table[i].name) &&
		  (strncmp(c_threshold_table[i].name, r.c_str(), strlen(c_threshold_table[i].name))))
		i++;

	if(c_threshold_table[i].name)
		return c_threshold_table[i].val;

	// Didn't match a standard, check for custom
	if(!strncmp(r.c_str(), c_custom_thresh, strlen(c_custom_thresh)))
		return strtof(&(r.c_str()[strlen(c_custom_thresh)]), NULL);

	LogWarning("GetDigitalThreshold unrecognised value [%s]\n", r.c_str());
	return 0.0f;
}

void SiglentSCPIOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
	LogWarning("SetDigitalHysteresis is not implemented\n");
}

void SiglentSCPIOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	channel -= m_analogChannelCount + 1;

	// Search through standard thresholds to see if one matches
	uint32_t i = 0;
	while(((c_threshold_table[i].name) && (fabsf(level - c_threshold_table[i].val)) > c_thresh_thresh))
		i++;

	if(c_threshold_table[i].name)
		sendOnly(":DIGITAL:THRESHOLD%d %s", (channel / 8) + 1, (c_threshold_table[i].name));
	else
	{
		do
		{
			sendOnly(":DIGITAL:THRESHOLD%d CUSTOM,%1.2E", (channel / 8) + 1, level);

			// This is a kludge to get the custom threshold to stick.
			usleep(c_setting_delay);
		} while(fabsf((GetDigitalThreshold(channel + m_analogChannelCount + 1) - level)) > 0.1f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

void SiglentSCPIOscilloscope::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Figure out what kind of trigger is active.
	string reply = Trim(converse(":TRIGGER:TYPE?"));
	if(reply == "DROPOUT")
		PullDropoutTrigger();
	else if(reply == "EDGE")
		PullEdgeTrigger();
	else if(reply == "RUNT")
		PullRuntTrigger();
	else if(reply == "SLOPE")
		PullSlewRateTrigger();
	else if(reply == "UART")
		PullUartTrigger();
	else if(reply == "INTERVAL")
		PullPulseWidthTrigger();
	else if(reply == "WINDOW")
		PullWindowTrigger();

	// Note that PULSe, PATTern, QUALified, VIDeo, IIC, SPI, LIN, CAN, FLEXray, CANFd & IIS are not yet handled

	//Unrecognized trigger type
	else
	{
		LogWarning("Unknown trigger type \"%s\"\n", reply.c_str());
		m_trigger = NULL;
		return;
	}

	//Pull the source (same for all types of trigger)
	PullTriggerSource(m_trigger, reply);

	//TODO: holdoff
}

/**
	@brief Reads the source of a trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullTriggerSource(Trigger* trig, string triggerModeName)
{
	string reply = Trim(converse(":TRIGGER:%s:SOURCE?", triggerModeName.c_str()));
	auto chan = GetChannelByHwName(reply);
	trig->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source \"%s\"\n", reply.c_str());
}

/**
	@brief Reads settings for a dropout trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullDropoutTrigger()
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

	//Level
	dt->SetLevel(stof(converse(":TRIGGER:DROPOUT:LEVEL?")));

	//Dropout time
	Unit fs(Unit::UNIT_FS);
	dt->SetDropoutTime(fs.ParseString(converse(":TRIGGER_DROPOUT:TIME?")));

	//Edge type
	if(Trim(converse(":TRIGGER:DROPOUT:SLOPE?")) == "RISING")
		dt->SetType(DropoutTrigger::EDGE_RISING);
	else
		dt->SetType(DropoutTrigger::EDGE_FALLING);

	//Reset type
	dt->SetResetType(DropoutTrigger::RESET_NONE);
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullEdgeTrigger()
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

	//Level
	et->SetLevel(stof(converse(":TRIGGER:EDGE:LEVEL?")));

	//TODO: OptimizeForHF (changes hysteresis for fast signals)

	//Slope
	GetTriggerSlope(et, Trim(converse(":TRIGGER:EDGE:SLOPE?")));
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullPulseWidthTrigger()
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

	//Level
	pt->SetLevel(stof(converse(":TRIGGER:INTERVAL:LEVEL?'")));

	//Condition
	pt->SetCondition(GetCondition(converse(":TRIGGER:INTERVAL:LIMIT?")));

	//Min range
	Unit fs(Unit::UNIT_FS);
	pt->SetLowerBound(fs.ParseString(converse(":TRIGGER:INTERVAL:TLOWER?")));

	//Max range
	pt->SetUpperBound(fs.ParseString(converse(":TRIGGER:INTERVAL:TUPPER?")));

	//Slope
	GetTriggerSlope(pt, Trim(converse(":TRIGGER:INTERVAL:SLOPE?")));
}

/**
	@brief Reads settings for a runt-pulse trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullRuntTrigger()
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

	//Lower bound
	Unit v(Unit::UNIT_VOLTS);
	rt->SetLowerBound(v.ParseString(converse(":TRIGGER:RUNT:LLEVEL?")));

	//Upper bound
	rt->SetUpperBound(v.ParseString(converse(":TRIGGER:RUNT:HLEVEL?")));

	//Lower interval
	Unit fs(Unit::UNIT_FS);
	rt->SetLowerInterval(fs.ParseString(converse(":TRIGGER:RUNT:TLOWER?")));

	//Upper interval
	rt->SetUpperInterval(fs.ParseString(converse(":TRIGGER:RUNT:TUPPER?")));

	//Slope
	auto reply = Trim(converse(":TRIGGER:RUNT:POLARITY?"));
	if(reply == "POSitive")
		rt->SetSlope(RuntTrigger::EDGE_RISING);
	else if(reply == "NEGative")
		rt->SetSlope(RuntTrigger::EDGE_FALLING);

	//Condition
	//	rt->SetCondition(GetCondition(m_transport->ReadReply()));
}

/**
	@brief Reads settings for a slew rate trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullSlewRateTrigger()
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
	Unit v(Unit::UNIT_VOLTS);
	st->SetLowerBound(v.ParseString(converse(":TRIGGER:SLOPE:TLEVEL?")));

	//Upper bound
	st->SetUpperBound(v.ParseString(converse(":TRIGGER:SLOPE:HLEVEL?")));

	//Lower interval
	Unit fs(Unit::UNIT_FS);
	st->SetLowerInterval(fs.ParseString(converse(":TRIGGER:SLOPE:TLOWER?")));

	//Upper interval
	st->SetUpperInterval(fs.ParseString(converse(":TRIGGER:SLOPE:TUPPER?")));

	//Slope
	auto reply = Trim(converse("TRIGGER:SLOPE:SLOPE?"));
	if(reply == "POSitive")
		st->SetSlope(SlewRateTrigger::EDGE_RISING);
	else if(reply == "NEGative")
		st->SetSlope(SlewRateTrigger::EDGE_FALLING);

	//Condition
	//st->SetCondition(GetCondition(m_transport->ReadReply()));
}

/**
	@brief Reads settings for a UART trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullUartTrigger()
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

	//Bit rate
	ut->SetBitRate(stoi(converse(":TRIGGER:UART:BAUD?")));

	//Level
	ut->SetLevel(stof(converse(":TRIGGER:UART:LIMIT?")));

	//Parity
	auto reply = Trim(converse(":TRIGGER:UART:PARITY?"));
	if(reply == "NONE")
		ut->SetParityType(UartTrigger::PARITY_NONE);
	else if(reply == "EVEN")
		ut->SetParityType(UartTrigger::PARITY_EVEN);
	else if(reply == "ODD")
		ut->SetParityType(UartTrigger::PARITY_ODD);
	else if(reply == "MARK")
		ut->SetParityType(UartTrigger::PARITY_MARK);
	else if(reply == "SPACe")
		ut->SetParityType(UartTrigger::PARITY_SPACE);

	//Operator
	//bool ignore_p2 = true;

	// It seems this scope only copes with equivalence
	ut->SetCondition(Trigger::CONDITION_EQUAL);

	//Idle polarity
	reply = Trim(converse(":TRIGGER:UART:IDLE?"));
	if(reply == "HIGH")
		ut->SetPolarity(UartTrigger::IDLE_HIGH);
	else if(reply == "LOW")
		ut->SetPolarity(UartTrigger::IDLE_LOW);

	//Stop bits
	ut->SetStopBits(stof(Trim(converse(":TRIGGER:UART:STOP?"))));

	//Trigger type
	reply = Trim(converse(":TRIGGER:UART:CONDITION?"));
	if(reply == "STARt")
		ut->SetMatchType(UartTrigger::TYPE_START);
	else if(reply == "STOP")
		ut->SetMatchType(UartTrigger::TYPE_STOP);
	else if(reply == "ERRor")
		ut->SetMatchType(UartTrigger::TYPE_PARITY_ERR);
	else
		ut->SetMatchType(UartTrigger::TYPE_DATA);

	// Data to match (there is no pattern2 on sds)
	string p1 = Trim(converse(":TRIGGER:UART:DATA?"));
	ut->SetPatterns(p1, "", true);
}

/**
	@brief Reads settings for a window trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullWindowTrigger()
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

	//Lower bound
	Unit v(Unit::UNIT_VOLTS);
	wt->SetLowerBound(v.ParseString(converse(":TRIGGER:WINDOW:LLEVEL?")));

	//Upper bound
	wt->SetUpperBound(v.ParseString(converse(":TRIGGER:WINDOW:HLEVEL?")));
}

/**
	@brief Processes the slope for an edge or edge-derived trigger
 */
void SiglentSCPIOscilloscope::GetTriggerSlope(EdgeTrigger* trig, string reply)

{
	reply = Trim(reply);

	if(reply == "RISing")
		trig->SetType(EdgeTrigger::EDGE_RISING);
	else if(reply == "FALLing")
		trig->SetType(EdgeTrigger::EDGE_FALLING);
	else if(reply == "ALTernate")
		trig->SetType(EdgeTrigger::EDGE_ANY);
	else
		LogWarning("Unknown trigger slope %s\n", reply.c_str());
}

/**
	@brief Parses a trigger condition
 */
Trigger::Condition SiglentSCPIOscilloscope::GetCondition(string reply)
{
	reply = Trim(reply);

	if(reply == "LessThan")
		return Trigger::CONDITION_LESS;
	else if(reply == "GreaterThan")
		return Trigger::CONDITION_GREATER;
	else if(reply == "InRange")
		return Trigger::CONDITION_BETWEEN;
	else if(reply == "OutOfRange")
		return Trigger::CONDITION_NOT_BETWEEN;

	//unknown
	return Trigger::CONDITION_LESS;
}

void SiglentSCPIOscilloscope::PushTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	auto dt = dynamic_cast<DropoutTrigger*>(m_trigger);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto rt = dynamic_cast<RuntTrigger*>(m_trigger);
	auto st = dynamic_cast<SlewRateTrigger*>(m_trigger);
	auto ut = dynamic_cast<UartTrigger*>(m_trigger);
	auto wt = dynamic_cast<WindowTrigger*>(m_trigger);

	if(dt)
	{
		sendOnly(":TRIGGER:TYPE DROPOUT");
		sendOnly(":TRIGGER:DROPOUT:SOURCE C%d", m_trigger->GetInput(0).m_channel->GetIndex() + 1);
		PushDropoutTrigger(dt);
	}
	else if(pt)
	{
		sendOnly(":TRIGGER:TYPE INTERVAL");
		sendOnly(":TRIGGER:INTERVAL:SOURCE C%d", m_trigger->GetInput(0).m_channel->GetIndex() + 1);
		PushPulseWidthTrigger(pt);
	}
	else if(rt)
	{
		sendOnly(":TRIGGER:TYPE RUNT");
		sendOnly(":TRIGGER:RUNT:SOURCE C%d", m_trigger->GetInput(0).m_channel->GetIndex() + 1);
		PushRuntTrigger(rt);
	}
	else if(st)
	{
		sendOnly(":TRIGGER:TYPE SLOPE");
		sendOnly(":TRIGGER:SLOPE:SOURCE C%d", m_trigger->GetInput(0).m_channel->GetIndex() + 1);
		PushSlewRateTrigger(st);
	}
	else if(ut)
	{
		sendOnly(":TRIGGER:TYPE UART");
		// TODO: Validate these trigger allocations
		sendOnly(":TRIGGER:UART:RXSOURCE C%d", m_trigger->GetInput(0).m_channel->GetIndex() + 1);
		sendOnly(":TRIGGER:UART:TXSOURCE C%d", m_trigger->GetInput(1).m_channel->GetIndex() + 1);
		PushUartTrigger(ut);
	}
	else if(wt)
	{
		sendOnly(":TRIGGER:TYPE WINDOW");
		sendOnly(":TRIGGER:WINDOW:SOURCE C%d", m_trigger->GetInput(0).m_channel->GetIndex() + 1);
		PushWindowTrigger(wt);
	}

	// TODO: Add in PULSE, VIDEO, PATTERN, QUALITFIED, SPI, IIC, CAN, LIN, FLEXRAY and CANFD Triggers

	else if(et)	   //must be last
	{
		sendOnly(":TRIGGER:TYPE EDGE");
		sendOnly(":TRIGGER:EDGE:SOURCE C%d", m_trigger->GetInput(0).m_channel->GetIndex() + 1);
		PushEdgeTrigger(et, "EDGE");
	}

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for a dropout trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
{
	PushFloat(":TRIGGER:DROPOUT:LEVEL ", trig->GetLevel());
	PushFloat(":TRIGGER_DROPOUT:TIME ", trig->GetDropoutTime() * SECONDS_PER_FS);

	sendOnly(":TRIGGER:DROPOUT:SLOPE %s", (trig->GetType() == DropoutTrigger::EDGE_RISING) ? "RISING" : "FALLING");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushEdgeTrigger(EdgeTrigger* trig, const std::string trigType)
{
	//Slope
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			sendOnly(":TRIGGER:%s:SLOPE RISING", trigType.c_str());
			break;

		case EdgeTrigger::EDGE_FALLING:
			sendOnly(":TRIGGER:%s:SLOPE FALLING", trigType.c_str());
			break;

		case EdgeTrigger::EDGE_ANY:
			sendOnly(":TRIGGER:%s:SLOPE ALTERNATE", trigType.c_str());
			break;

		default:
			LogWarning("Invalid trigger type %d\n", trig->GetType());
			break;
	}
	//Level
	sendOnly(":TRIGGER:%s:LEVEL %e", trigType.c_str(), trig->GetLevel());
	usleep(c_setting_delay);
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	PushEdgeTrigger(trig, "INTERVAL");
	PushCondition(":TRIGGER:INTERVAL", trig->GetCondition());
	PushFloat(":TRIGGER:INTERVAL:TUPPER", trig->GetUpperBound() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:INTERVAL:TLOWER", trig->GetLowerBound() * SECONDS_PER_FS);
}

/**
	@brief Pushes settings for a runt trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushRuntTrigger(RuntTrigger* trig)
{
	PushCondition(":TRIGGER:RUNT", trig->GetCondition());
	PushFloat(":TRIGGER:RUNT:TUPPER", trig->GetUpperInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:RUNT:TLOWER", trig->GetLowerInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:RUNT:LLEVEL", trig->GetUpperBound());
	PushFloat(":TRIGGER:RUNT:HLEVEL", trig->GetLowerBound());

	sendOnly(":TRIGGER:RUNT:POLARITY %s", (trig->GetSlope() == RuntTrigger::EDGE_RISING) ? "RISING" : "FALLING");
}

/**
	@brief Pushes settings for a slew rate trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
{
	PushCondition(":TRIGGER:SLEW", trig->GetCondition());
	PushFloat(":TRIGGER:SLEW:TUPPER", trig->GetUpperInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:SLEW:TLOWER", trig->GetLowerInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:SLEW:HLEVEL", trig->GetUpperBound());
	PushFloat(":TRIGGER:SLEW:LLEVEL", trig->GetLowerBound());

	sendOnly(":TRIGGER:SLEW:SLOPE %s", (trig->GetSlope() == SlewRateTrigger::EDGE_RISING) ? "POSITIVE" : "NEGATIVE");
}

/**
	@brief Pushes settings for a UART trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushUartTrigger(UartTrigger* trig)
{
	//Special parameter for trigger level
	PushFloat(":TRIGGER:UART:LIMIT", trig->GetLevel());

	//AtPosition
	//Bit9State
	PushFloat(":TRIGGER:UART:BAUD", trig->GetBitRate());
	sendOnly(":TRIGGER:UART:BITORDER LSB");
	//DataBytesLenValue1
	//DataBytesLenValue2
	//DataCondition
	//FrameDelimiter
	//InterframeMinBits
	//NeedDualLevels
	//NeededSources
	sendOnly(":TRIGGER:UART:DLENGTH 8");

	switch(trig->GetParityType())
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
	}

	//Pattern length depends on the current format.
	//Note that the pattern length is in bytes, not bits, even though patterns are in binary.
	auto pattern1 = trig->GetPattern1();
	sendOnly(":TRIGGER:UART:DLENGTH \"%d\"", (int)pattern1.length() / 8);

	PushCondition(":TRIGGER:UART", trig->GetCondition());

	//Polarity
	sendOnly(":TRIGGER:UART:IDLE %s", (trig->GetPolarity() == UartTrigger::IDLE_HIGH) ? "HIGH" : "LOW");

	auto nstop = trig->GetStopBits();
	if(nstop == 1)
		sendOnly(":TRIGGER:UART:STOP 1");
	else if(nstop == 2)
		sendOnly(":TRIGGER:UART:STOP 2");
	else
		sendOnly(":TRIGGER:UART:STOP 1.5");

	//Match type
	switch(trig->GetMatchType())
	{
		case UartTrigger::TYPE_START:
			sendOnly(":TRIGGER:UART:CONDITION START");
			break;
		case UartTrigger::TYPE_STOP:
			sendOnly(":TRIGGER:UART:CONDITION STOP");
			break;
		case UartTrigger::TYPE_PARITY_ERR:
			sendOnly(":TRIGGER:UART:CONDITION ERROR");
			break;
		default:
		case UartTrigger::TYPE_DATA:
			sendOnly(":TRIGGER:UART:CONDITION DATA");
			break;
	}

	//UARTCondition
	//ViewingMode
}

/**
	@brief Pushes settings for a window trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushWindowTrigger(WindowTrigger* trig)
{
	PushFloat(":TRIGGER:WINDOW:LLEVEL", trig->GetLowerBound());
	PushFloat(":TRIGGER:WINDOW:HLEVEL", trig->GetUpperBound());
}

/**
	@brief Pushes settings for a trigger condition under a .Condition field
 */
void SiglentSCPIOscilloscope::PushCondition(const string& path, Trigger::Condition cond)
{
	switch(cond)
	{
		case Trigger::CONDITION_LESS:
			sendOnly("%s:LIMIT LESSTHAN", path);
			break;

		case Trigger::CONDITION_GREATER:
			sendOnly("%s:LIMIT GREATERTHAN", path);
			break;

		case Trigger::CONDITION_BETWEEN:
			sendOnly("%s:LIMIT INNER", path);
			break;

		case Trigger::CONDITION_NOT_BETWEEN:
			sendOnly("%s:LIMIT OUTER", path);
			break;

		//Other values are not legal here, it seems
		default:
			break;
	}
}

void SiglentSCPIOscilloscope::PushFloat(string path, float f)
{
	sendOnly("%s = %e", path.c_str(), f);
}

vector<string> SiglentSCPIOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(DropoutTrigger::GetTriggerName());
	ret.push_back(EdgeTrigger::GetTriggerName());
	ret.push_back(PulseWidthTrigger::GetTriggerName());
	ret.push_back(RuntTrigger::GetTriggerName());
	ret.push_back(SlewRateTrigger::GetTriggerName());
	if(m_hasUartTrigger)
		ret.push_back(UartTrigger::GetTriggerName());
	ret.push_back(WindowTrigger::GetTriggerName());

	// TODO: Add in PULSE, VIDEO, PATTERN, QUALITFIED, SPI, IIC, CAN, LIN, FLEXRAY and CANFD Triggers
	return ret;
}
