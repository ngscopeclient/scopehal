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
 * Rohde & Schwarz RTB2000/RTB2 scope driver.
 */

//TODO: unimplemented feature, potential optimization point, etc.
//FIXME: known minor problem, temporary workaround, or something that needs to be reworked later

#include "scopehal.h"
#include "RSRTB2kOscilloscope.h"

#include "RSRTB2kEdgeTrigger.h"
#include "RSRTB2kLineTrigger.h"
#include "RSRTB2kRiseTimeTrigger.h"
#include "RSRTB2kRuntTrigger.h"
#include "RSRTB2kTimeoutTrigger.h"
#include "RSRTB2kVideoTrigger.h"
#include "RSRTB2kWidthTrigger.h"

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

RSRTB2kOscilloscope::RSRTB2kOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_analogChannelCount(0)
	, m_digitalChannelCount(0)
	, m_analogAndDigitalChannelCount(0)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_hasI2cTrigger(false)
	, m_hasSpiTrigger(false)
	, m_hasUartTrigger(false)
	, m_hasCanTrigger(false)
	, m_hasLinTrigger(false)
	, m_maxBandwidth(70)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleRateValid(false)
	, m_sampleRate(1)
	, m_memoryDepthValid(false)
	, m_memoryDepth(1)
	, m_triggerOffsetValid(false)
	, m_triggerOffset(0)
	, m_triggerReference(0)
	, m_highDefinition(true)
{
	//LogTrace("\n");
	// standard initialization
	FlushConfigCache();
	IdentifyHardware();
	DetectOptions();
	AddAnalogChannels();
	AddDigitalChannels();
	AddExternalTriggerChannel();
	AddLineTriggerChannel();
	AddAwgChannel();
	m_analogAndDigitalChannelCount = m_analogChannelCount + m_digitalChannelCount;

	SetupForAcquisition();
}

string RSRTB2kOscilloscope::converse(const char* fmt, ...)
{
	string ret;
	char opString[128];
	va_list va;
	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	ret = m_transport->SendCommandQueuedWithReply(opString, false);
	//~ LogDebug("RTB2k: converse() >%s< - >%s<\n", opString, ret.c_str());
	return ret;
}

void RSRTB2kOscilloscope::sendOnly(const char* fmt, ...)
{
	char opString[128];
	va_list va;

	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	//~ LogDebug("RTB2k: sendOnly() >%s<\n", opString);
	m_transport->SendCommandQueued(opString);
}

bool RSRTB2kOscilloscope::sendWithAck(const char* fmt, ...)
{
	string ret;
	char opString[128];
	va_list va;
	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

    std::string result(opString);
    result += ";*OPC?";

	ret = m_transport->SendCommandQueuedWithReply(result.c_str(), false);
	//~ LogDebug("RTB2k: sendWithAck() -> %s >%s<\n", result.c_str(), ret.c_str());
	return (ret == "1");
}

void RSRTB2kOscilloscope::flush()
{
	//LogTrace("\n");
	m_transport->ReadReply();
}

void RSRTB2kOscilloscope::protocolError(bool flush, const char* fmt, va_list ap)
{
    char opString[128];
    vsnprintf(opString, sizeof(opString), fmt, ap);
    LogError("RTB2k: Protocol error%s: %s.\n", flush ? ", flushing read stream" : "", opString);
    if(flush) m_transport->ReadReply();
}

void RSRTB2kOscilloscope::protocolError(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    protocolError(false, fmt, ap);
    va_end(ap);
}

void RSRTB2kOscilloscope::protocolErrorWithFlush(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    protocolError(true, fmt, ap);
    va_end(ap);
}

void RSRTB2kOscilloscope::AddAnalogChannels()
{	// 2 or 4 Channels on RTB2k scopes
	//LogTrace("\n");
	for(unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN") + to_string(i+1);

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

		chan->SetDisplayName(GetChannelDisplayName(i));
		m_channels.push_back(chan);
	}
}

void RSRTB2kOscilloscope::AddDigitalChannels()
{
	//LogTrace("\n");
	m_digitalChannelBase = m_channels.size();

	char chn[32];
	for(unsigned int i = 0; i < m_digitalChannelCount; i++)
	{
		snprintf(chn, sizeof(chn), "DIG%u", i);
		auto chan = new OscilloscopeChannel(
			this,
			chn,
			"#149cec", //~ GetDefaultChannelColor(m_channels.size()),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_COUNTS),
			Stream::STREAM_TYPE_DIGITAL,
			i + m_digitalChannelBase);

		chan->SetDisplayName(GetChannelDisplayName(i + m_digitalChannelBase));

		m_channels.push_back(chan);
		m_digitalChannels.push_back(chan);

	}
}

void RSRTB2kOscilloscope::AddExternalTriggerChannel()
{
	//LogTrace("\n");
	m_extTrigChannel =
		new OscilloscopeChannel(
			this,
			"EXT",
			"#ffffff",
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_TRIGGER,
			m_channels.size());
	m_channels.push_back(m_extTrigChannel);
}

void RSRTB2kOscilloscope::AddLineTriggerChannel()
{
	//LogTrace("\n");
	m_lineTrigChannel =
		new OscilloscopeChannel(
			this,
			"LINE",
			"#ffffff",
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_TRIGGER,
			m_channels.size());
	m_channels.push_back(m_lineTrigChannel);
}

void RSRTB2kOscilloscope::AddAwgChannel()
{
	//LogTrace("\n");
	//Add the function generator output
	if(m_hasFunctionGen)
	{
		m_awgChannel =
			new FunctionGeneratorChannel(
				this,
				"FGEN",
				"#ffff00",
				m_channels.size());
		m_channels.push_back(m_awgChannel);
		m_awgChannel->SetDisplayName("FGEN");
	}
	else
		m_awgChannel = nullptr;
}

RSRTB2kOscilloscope::~RSRTB2kOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device information

string RSRTB2kOscilloscope::GetDriverNameInternal()
{
	//LogTrace("\n");
	return "rs.rtb2k";
}

void RSRTB2kOscilloscope::FlushConfigCache()
{
	//LogTrace("\n");
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if(m_trigger)
		delete m_trigger;
	m_trigger = NULL;

	m_channelVoltageRanges.clear();
	m_channelOffsets.clear();
	m_channelsEnabled.clear();
	//~ m_channelDeskew.clear();
	m_channelDigitalHysteresis.clear();
	m_channelDigitalThresholds.clear();
	//~ m_probeIsActive.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;
	//~ m_meterModeValid = false;
	m_awgEnabled.clear();
	m_awgDutyCycle.clear();
	m_awgRange.clear();
	m_awgOffset.clear();
	m_awgFrequency.clear();
	m_awgRiseTime.clear();
	m_awgFallTime.clear();
	m_awgShape.clear();
	m_awgImpedance.clear();

	//Clear cached display name of all channels
	for(auto c : m_channels)
	{
		if(GetInstrumentTypesForChannel(c->GetIndex()) & Instrument::INST_OSCILLOSCOPE)
			c->ClearCachedDisplayName();
	}
}

void RSRTB2kOscilloscope::IdentifyHardware()
{
	//LogTrace("\n");
	//analog channel count
	if(m_model.compare("RTB2002") == 0 || m_model.compare("RTB22") == 0)
	{
		m_analogChannelCount = 2;
	}
	else if(m_model.compare("RTB2004") == 0 || m_model.compare("RTB24") == 0)
	{
		m_analogChannelCount = 4;
	}
	else
	{
		LogWarning("Model \"%s\" is unknown, available analog channel count may not be properly detected\n",
			m_model.c_str());
	}
}

void RSRTB2kOscilloscope::DetectOptions()
{
	//LogTrace("\n");
	// B1: mixed signal option
	// B6: waveform generator and 4-bit pattern generator
	// B221, B241: 100 MHz bandwidth
	// B222, B242: 200 MHz bandwidth
	// B223, B243: 300 MHz bandwidth
	// K1: SPI/I2C triggering and decoding
	// K2: UART/RS-232/RS-422/RS-485 triggering and decoding
	// K3: CAN/LIN triggering and decoding
	// K15: History and segmented memory
	// K36: Frequency response analysis (Bode plot)
	// example: *OPT? -> "K1,K2,K3,K15,B1,B6,B242,B243,K36"

	string options = converse("*OPT?");
	if (options.find("B1") != string::npos)
	{
		string probe = converse(":LOG1:PROB?");
		if (probe == "1")
		{
			m_hasLA = true;
			m_digitalChannelCount = 8;
		}
		probe = converse(":LOG2:PROB?");
		if (probe == "1")
		{
			m_hasLA = true;
			m_digitalChannelCount = 16;
		}
	}
	if (options.find("B6") != string::npos)
		m_hasFunctionGen = true;
	//the bandwidth option may be available multiple times, use the largest bandwidth
	if (options.find("B221") != string::npos || options.find("B241") != string::npos)
		m_maxBandwidth = 100;
	if (options.find("B222") != string::npos || options.find("B242") != string::npos)
		m_maxBandwidth = 200;
	if (options.find("B223") != string::npos || options.find("B243") != string::npos)
		m_maxBandwidth = 300;
	//do not confuse K1 with K15
	if (options.find("K1") != string::npos)
	{
		if (options.find("K15") == string::npos || options.find("K1") != options.find("K15"))
		{
			m_hasI2cTrigger = true;
			m_hasSpiTrigger = true;
		}
	}
	if (options.find("K2") != string::npos)
		m_hasUartTrigger = true;
	if (options.find("K3") != string::npos)
	{
		m_hasCanTrigger = true;
		m_hasLinTrigger = true;
	}
}

OscilloscopeChannel* RSRTB2kOscilloscope::GetExternalTrigger()
{
	//LogTrace("\n");
	return m_extTrigChannel;
}

unsigned int RSRTB2kOscilloscope::GetInstrumentTypes() const
{
	//LogTrace("\n");
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasFunctionGen)
		type |= INST_FUNCTION;
	return type;
}

uint32_t RSRTB2kOscilloscope::GetInstrumentTypesForChannel(size_t i) const
{
	//LogTrace("\n");
	if(m_awgChannel && (m_awgChannel->GetIndex() == i) )
		return Instrument::INST_FUNCTION;

	//If we get here, it's an oscilloscope channel
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool RSRTB2kOscilloscope::IsChannelEnabled(size_t i)
{
	//LogTrace("\n");
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex() || i == m_lineTrigChannel->GetIndex())
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
		string reply = converse(":CHAN%zu:STAT?", i + 1);
		{
			lock_guard<recursive_mutex> lock2(m_cacheMutex);
			m_channelsEnabled[i] = (reply == "1");
		}
	}
	else if(i < m_analogAndDigitalChannelCount)
	{
		//Digital => first check digital module is ON
		string probe;
		bool isOn = false;
		if ((i >= m_digitalChannelBase) && (i < (m_digitalChannelBase + 8)))
			probe = converse(":LOG1:STAT?");
		else if ((i >= (m_digitalChannelBase + 8)) && (i < (m_digitalChannelBase + 16)))
			probe = converse(":LOG2:STAT?");

		if(probe == "1")
		{	//See if the channel is on (digital channel numbers are 0 based)
			size_t nchan = i - m_analogChannelCount;
			string channel = converse(":DIG%zu:DISP?", nchan);
			isOn = (channel == "1");
		}

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelsEnabled[i] = isOn;
	}
	else if(i == LOGICPOD1 || i == LOGICPOD2)
	{
		//Digital Logicpod => check digitale module is ON
		bool pod1 = (converse(":LOG1:STAT?") == "1" ? true : false);
		bool pod2 = (converse(":LOG2:STAT?") == "1" ? true : false);

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelsEnabled[LOGICPOD1] = pod1;
		m_channelsEnabled[LOGICPOD2] = pod2;
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	return m_channelsEnabled[i];
}

void RSRTB2kOscilloscope::EnableChannel(size_t i)
{
	//LogTrace("\n");
	bool wasInterleaving = IsInterleaving();
	bool TriggerArmed = IsTriggerArmed();

	//No need to lock the main mutex since sendOnly now pushes to the queue

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
	{
		// During operation, read errors may occur without stopping.
		if (TriggerArmed) Stop();
		sendWithAck(":CHAN%zu:STAT ON", i + 1);
		if (TriggerArmed) Start();
	}
	else if(i < m_analogAndDigitalChannelCount)
	{
		//Digital channel (digital channel numbers are 0 based)
		// During operation, activating a logic pod causes a crash.
		if (TriggerArmed) Stop();
		sendWithAck(":DIG%d:DISP ON", i - m_analogChannelCount);
		if (TriggerArmed) Start();
	}
	else if(i == m_extTrigChannel->GetIndex())
	{
		//Trigger can't be enabled
	}
	else if(i == m_lineTrigChannel->GetIndex())
	{
		//Trigger can't be enabled
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;

	//Sample rate and memory depth can change if interleaving state changed
	if(IsInterleaving() != wasInterleaving)
	{
		m_memoryDepthValid = false;
		m_sampleRateValid = false;
		m_triggerOffsetValid = false;
	}
}

bool RSRTB2kOscilloscope::CanEnableChannel(size_t i)
{
	//LogTrace("\n");
	// Can enable all channels except trigger
	return !(i == m_extTrigChannel->GetIndex() || i == m_lineTrigChannel->GetIndex());
}

void RSRTB2kOscilloscope::DisableChannel(size_t i)
{
	//LogTrace("\n");
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
		sendWithAck(":DIG%zu:DISP OFF", i - m_analogChannelCount);
	}
	else if(i == m_extTrigChannel->GetIndex())
	{
		//Trigger can't be disabled
	}
	else if(i == m_lineTrigChannel->GetIndex())
	{
		//Trigger can't be disabled
	}

	//Sample rate and memory depth can change if interleaving state changed
	if(IsInterleaving() != wasInterleaving)
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_memoryDepthValid = false;
		m_sampleRateValid = false;
		m_triggerOffsetValid = false;
	}
}

vector<OscilloscopeChannel::CouplingType> RSRTB2kOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	//LogTrace("\n");
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);
	return ret;
}

OscilloscopeChannel::CouplingType RSRTB2kOscilloscope::GetChannelCoupling(size_t i)
{
	//LogTrace("\n");
	if(i >= m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	string replyType = converse(":CHAN%zu:COUP?", i + 1);

	if(replyType == "ACL")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if(replyType == "DCL")
		return OscilloscopeChannel::COUPLE_DC_1M;
	else if(replyType == "GND")
		return OscilloscopeChannel::COUPLE_GND;

	//invalid
	protocolError("RTB2k: GetChannelCoupling got invalid coupling [%s]", replyType.c_str());
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void RSRTB2kOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	//LogTrace("\n");
	if(i >= m_analogChannelCount)
		return;

	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			sendOnly(":CHAN%zu:COUP ACL", i + 1);
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			sendOnly(":CHAN%zu:COUP DCL", i + 1);
			break;

		//treat unrecognized as ground
		case OscilloscopeChannel::COUPLE_GND:
		default:
			sendOnly(":CHAN%zu:COUP GND", i + 1);
			break;
	}
}

double RSRTB2kOscilloscope::GetChannelAttenuation(size_t i)
{
	//LogTrace("\n");
	if(i >= m_analogChannelCount)
		return 1;

	if(i == m_extTrigChannel->GetIndex())
		return 1;

	if(i == m_lineTrigChannel->GetIndex())
		return 1;

	string reply;

	reply = converse(":PROB%zu:SET:ATT:MAN?", i + 1);

	double f;
	if(sscanf(reply.c_str(), "%lf", &f) != 1)
	{
		protocolError("RTB2k: invalid channel attenuation value '%s'",reply.c_str());
	}
	return f;
}

void RSRTB2kOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	//LogTrace("\n");
	if(i >= m_analogChannelCount)
		return;

	if(atten <= 0)
		return;

	sendOnly(":PROB%zu:SET:ATT:MAN %f", i + 1, atten);
}

vector<unsigned int> RSRTB2kOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	//LogTrace("\n");
	vector<unsigned int> ret;

	ret.push_back(0);
	ret.push_back(20);

	return ret;
}

unsigned int RSRTB2kOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	//LogTrace("\n");
	if(i >= m_analogChannelCount)
		return 0;

	string reply;

	reply = converse(":CHAN%zu:BAND?", i + 1);
	if(reply == "B20")
		return 20;
	else if(reply == "FULL")
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
			return m_maxBandwidth;
	}

	protocolError("RTB2k: GetChannelBandwidthLimit got invalid bwlimit %s", reply.c_str());
	return 0;
}

void RSRTB2kOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	//LogTrace("\n");
	if(limit_mhz == 20)
		sendOnly(":CHAN%zu:BAND B20", i + 1);
	else
		sendOnly(":CHAN%zu:BAND FULL", i + 1);
}

bool RSRTB2kOscilloscope::CanInvert(size_t i)
{
	//LogTrace("\n");
	//All analog channels, and only analog channels, can be inverted
	return (i < m_analogChannelCount);
}

void RSRTB2kOscilloscope::Invert(size_t i, bool invert)
{
	//LogTrace("\n");
	if(i >= m_analogChannelCount)
		return;

	sendOnly(":CHAN%zu:POL %s", i + 1, invert ? "INV" : "NORM");
}

bool RSRTB2kOscilloscope::IsInverted(size_t i)
{
	//LogTrace("\n");
	if(i >= m_analogChannelCount)
		return false;

	string reply;

	reply = Trim(converse(":CHAN%zu:POL?", i + 1));
	return (reply == "INV");
}

void RSRTB2kOscilloscope::SetChannelDisplayName(size_t i, string name)
{
	//LogTrace("\n");
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return;

	//External trigger cannot be renamed in hardware.
	if(chan == m_extTrigChannel)
		return;

	//Line trigger cannot be renamed in hardware.
	if(chan == m_lineTrigChannel)
		return;

	//Update in hardware
	if(i < m_analogChannelCount)
	{
		if(name.length() > 0)
		{
			sendOnly(":CHAN%zu:LAB \"%s\"", i + 1, name.c_str());
			sendOnly(":CHAN%zu:LAB:STAT ON", i + 1);
		}
		else
		{
			m_channels[i]->SetDisplayName(m_channels[i]->GetHwname());
			sendOnly(":CHAN%zu:LAB:STAT OFF", i + 1);
		}

	}
	else
	{
		if(name.length() > 0)
		{
			sendOnly(":DIG%zu:LAB \"%s\"", i - m_analogChannelCount, name.c_str());
			sendOnly(":DIG%zu:LAB:STAT ON", i - m_analogChannelCount);
		}
		else
		{
			m_channels[i]->SetDisplayName(m_channels[i]->GetHwname());
			sendOnly(":DIG%zu:LAB:STAT OFF", i - m_analogChannelCount);
		}
	}
}

string RSRTB2kOscilloscope::GetChannelDisplayName(size_t i)
{
	//LogTrace("\n");
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return "";

	//External trigger cannot be renamed in hardware.
	if(chan == m_extTrigChannel)
		return m_extTrigChannel->GetHwname();

	//Line trigger cannot be renamed in hardware.
	if(chan == m_lineTrigChannel)
		return m_lineTrigChannel->GetHwname();

	string reply;
	string name = "";

	if(i < m_analogChannelCount)
	{
		reply = converse(":CHAN%zu:LAB:STAT?", i + 1);
		if (reply == "1")
		{
			name = converse(":CHAN%zu:LAB?", i + 1);
			// Remove "'s around the name
			if(name.length() > 2)
				name = name.substr(1, name.length() - 2);
		}
	}
	else
	{
		reply = converse(":DIG%zu:LAB:STAT?", i - m_analogChannelCount);
		if (reply == "1")
		{
			name = converse(":DIG%zu:LAB?", i - m_analogChannelCount);
			// Remove "'s around the name
			if(name.length() > 2)
				name = name.substr(1, name.length() - 2);
		}
	}

	//Default to using hwname if no alias defined
	if(name == "" || name == "\"\"")
		name = chan->GetHwname();

	return name;
}

bool RSRTB2kOscilloscope::GetActiveChannels(bool* pod1, bool* pod2, bool* chan1, bool* chan2, bool* chan3, bool* chan4)
{
	//LogTrace("\n");
	bool memoryFull = false;
	bool stop = false;

	//TODO: protocol decoder activ: every channel 10 Mpts
	//~ if (BUS PROTOCOL ACTIVE):
		//~ memoryFull = false;

	// 1 logic probe: 20 Mpts
	if (IsChannelEnabled(LOGICPOD1))
	{
		*pod1 = true;
		memoryFull = true;
	}
	if (IsChannelEnabled(LOGICPOD2))
	{
		// 2 logic probes activ: every channel 10 Mpts
		*pod2 = true;
		if (memoryFull)
		{
			memoryFull = false;
			stop = true;
		}
		else
			memoryFull = true;
	}

	// 1 analog channel and 1 logic probe: 20 Mpts
	// 2 analog channels from different group (1+3/4 or 2+3/4) and 1 logic probe: 20 Mpts
	if (IsChannelEnabled(0))
	{
		*chan1 = true;
		if (!stop)
			memoryFull = true;
	}
	if (IsChannelEnabled(1))
	{
		// 2 analog channels from same group: 10 Mpts
		*chan2 = true;
		if (!stop)
		{
			if (memoryFull && IsChannelEnabled(0))
			{
				memoryFull = false;
				stop = true;
			}
			else
				memoryFull = true;
		}
	}
	if (IsChannelEnabled(2))
	{
		*chan3 = true;
		if (!stop)
			memoryFull = true;
	}
	if (IsChannelEnabled(3))
	{
		// 2 analog channels from same group: 10 Mpts
		*chan4 = true;
		if (!stop)
		{
			if (memoryFull && IsChannelEnabled(2))
				memoryFull = false;
			else
				memoryFull = true;
		}
	}

	return memoryFull;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

float RSRTB2kOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	//LogTrace("\n");
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
		protocolError("RTB2k: invalid channel vlotage range value '%s'",reply.c_str());
	}

	float v = volts_per_div * 10;	//plot is 10 divisions high
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = v;
	return v;
}

void RSRTB2kOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	//LogTrace("\n");
	// Only for analog channels
	if(i >= m_analogChannelCount)
		return;

	float vdiv = range / 10;	//plot is 10 divisions high

	sendWithAck(":CHAN%zu:SCALE %.4f", i + 1, vdiv);

	//Don't update the cache because the scope is likely to round the value
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelVoltageRanges.erase(i);
}

float RSRTB2kOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	//LogTrace("\n");
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
		protocolError("RTB2k: invalid channel offset value '%s'",reply.c_str());
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void RSRTB2kOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	//LogTrace("\n");
	//not meaningful for trigger or digital channels
	if(i >= m_analogChannelCount)
		return;

	sendWithAck(":CHAN%zu:OFFSET %1.2E", i + 1, offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

/**
	@brief Processes the channel hystersis for an trigger
 */
void RSRTB2kOscilloscope::GetTriggerHysteresis(Trigger* trig, string reply)
{
	//LogTrace("\n");
	auto st = dynamic_cast<RSRTB2kRiseTimeTrigger*>(trig);
	auto rt = dynamic_cast<RSRTB2kRuntTrigger*>(trig);
	auto tt = dynamic_cast<RSRTB2kTimeoutTrigger*>(trig);
	auto wt = dynamic_cast<RSRTB2kWidthTrigger*>(trig);
	reply = Trim(reply);


	if(reply == "SMAL")
	{
		if(st) st->SetHysteresisType(RSRTB2kRiseTimeTrigger::HYSTERESIS_SMALL);
		if(rt) rt->SetHysteresisType(RSRTB2kRuntTrigger::HYSTERESIS_SMALL);
		if(tt) tt->SetHysteresisType(RSRTB2kTimeoutTrigger::HYSTERESIS_SMALL);
		if(wt) wt->SetHysteresisType(RSRTB2kWidthTrigger::HYSTERESIS_SMALL);
	}
	else if(reply == "MED")
	{
		if(st) st->SetHysteresisType(RSRTB2kRiseTimeTrigger::HYSTERESIS_MEDIUM);
		if(rt) rt->SetHysteresisType(RSRTB2kRuntTrigger::HYSTERESIS_MEDIUM);
		if(tt) tt->SetHysteresisType(RSRTB2kTimeoutTrigger::HYSTERESIS_MEDIUM);
		if(wt) wt->SetHysteresisType(RSRTB2kWidthTrigger::HYSTERESIS_MEDIUM);
	}
	else if(reply == "LARG")
	{
		if(st) st->SetHysteresisType(RSRTB2kRiseTimeTrigger::HYSTERESIS_LARGE);
		if(rt) rt->SetHysteresisType(RSRTB2kRuntTrigger::HYSTERESIS_LARGE);
		if(tt) tt->SetHysteresisType(RSRTB2kTimeoutTrigger::HYSTERESIS_LARGE);
		if(wt) wt->SetHysteresisType(RSRTB2kWidthTrigger::HYSTERESIS_LARGE);
	}
	else
		protocolError("RTB2k: Unknown trigger hysteresis %s\n", reply.c_str());
}

/**
	@brief Processes the slope for an edge or edge-derived trigger
 */
void RSRTB2kOscilloscope::GetTriggerSlope(Trigger* trig, string reply)
{
	//LogTrace("\n");
	auto et = dynamic_cast<RSRTB2kEdgeTrigger*>(trig);
	auto st = dynamic_cast<RSRTB2kRiseTimeTrigger*>(trig);
	auto rt = dynamic_cast<RSRTB2kRuntTrigger*>(trig);
	auto tt = dynamic_cast<RSRTB2kTimeoutTrigger*>(trig);
	auto vt = dynamic_cast<RSRTB2kVideoTrigger*>(trig);
	auto wt = dynamic_cast<RSRTB2kWidthTrigger*>(trig);
	reply = Trim(reply);

	if(reply == "POS")
	{
		if(et) et->SetType(RSRTB2kEdgeTrigger::EDGE_RISING);
		if(st) st->SetType(RSRTB2kRiseTimeTrigger::EDGE_RISING);
		if(rt) rt->SetType(RSRTB2kRuntTrigger::EDGE_RISING);
		if(tt) tt->SetType(RSRTB2kTimeoutTrigger::EDGE_RISING);
		if(vt) vt->SetType(RSRTB2kVideoTrigger::EDGE_RISING);
		if(wt) wt->SetType(RSRTB2kWidthTrigger::EDGE_RISING);
	}
	else if(reply == "NEG")
	{
		if(et) et->SetType(RSRTB2kEdgeTrigger::EDGE_FALLING);
		if(st) st->SetType(RSRTB2kRiseTimeTrigger::EDGE_FALLING);
		if(rt) rt->SetType(RSRTB2kRuntTrigger::EDGE_FALLING);
		if(tt) tt->SetType(RSRTB2kTimeoutTrigger::EDGE_FALLING);
		if(vt) vt->SetType(RSRTB2kVideoTrigger::EDGE_FALLING);
		if(wt) wt->SetType(RSRTB2kWidthTrigger::EDGE_FALLING);
	}
	else if(reply == "EITH")
	{
		if(et) et->SetType(RSRTB2kEdgeTrigger::EDGE_ANY);
		if(st) st->SetType(RSRTB2kRiseTimeTrigger::EDGE_ANY);
		if(rt) rt->SetType(RSRTB2kRuntTrigger::EDGE_ANY);
		//~ if(tt) tt->SetType(RSRTB2kTimeoutTrigger::EDGE_ANY);
		//~ if(vt) vt->SetType(RSRTB2kVideoTrigger::EDGE_ANY);
		//~ if(wt) wt->SetType(RSRTB2kWidthTrigger::EDGE_ANY);
	}
	else
		protocolError("RTB2k: Unknown trigger slope %s\n", reply.c_str());
}

/**
	@brief Processes the coupling for an edge or edge-derived trigger
 */
void RSRTB2kOscilloscope::GetTriggerCoupling(Trigger* trig, string reply)
{
	//LogTrace("\n");
	auto et = dynamic_cast<RSRTB2kEdgeTrigger*>(trig);
	reply = Trim(reply);


	if(reply == "AC")
	{
		if(et) et->SetCouplingType(RSRTB2kEdgeTrigger::COUPLING_AC);
	}
	else if(reply == "DC")
	{
		if(et) et->SetCouplingType(RSRTB2kEdgeTrigger::COUPLING_DC);
	}
	else if(reply == "LFR")
	{
		if(et) et->SetCouplingType(RSRTB2kEdgeTrigger::COUPLING_LFREJECT);
	}
	else
		protocolError("RTB2k: Unknown trigger coupling %s\n", reply.c_str());
}

bool RSRTB2kOscilloscope::IsTriggerArmed()
{
	//LogTrace("\n");
	return m_triggerArmed;
}

void RSRTB2kOscilloscope::ForceTrigger()
{
	//LogTrace("\n");
	// Don't allow more than one force at a time
	if(m_triggerForced)
		return;

	m_triggerForced = true;

	PrepareAcquisition();
	//~ sendOnly(":SINGLE");
	if(!m_triggerArmed)
	{
		//~ sendOnly(":SINGLE");
		sendWithAck(":SINGLE");
	}

	m_triggerArmed = true;
	this_thread::sleep_for(c_trigger_delay);
}

Oscilloscope::TriggerMode RSRTB2kOscilloscope::PollTrigger()
{
	//LogTrace("\n");
	//Read the Internal State Change Register
	string sinr = "";

	if(m_triggerForced)
	{
		// The force trigger completed, return the sample set
		m_triggerForced = false;
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	sinr = converse("ACQ:STAT?");

	//No waveform, but ready for one?
	if(sinr == "RUN")
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//Stopped, no data available
	//~ if(sinr == "COMP" || sinr == "STOP" || sinr == "BRE") // Complete, Stopping, Break
	if(sinr == "COMP" || sinr == "BRE") // Complete, Break
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

/**
	@brief Optimized function for checking channel enable status en masse with less round trips to the scope
 */
void RSRTB2kOscilloscope::BulkCheckChannelEnableState()
{
	//LogTrace("\n");
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
		string probe1 = converse(":LOG1:STAT?");
		string probe2 = converse(":LOG2:STAT?");
		digitalModuleOn = (probe1 == "1" || probe2 == "1");
	}
	for(auto i : uncached)
	{
		bool enabled;
		if((i < m_analogChannelCount))
		{	// Analog
			enabled = (converse(":CHAN%zu:STAT?", i + 1) == "1");
		}
		else
		{	// Digital
			enabled = digitalModuleOn && (converse(":DIG%zu:DISP?", (i - m_analogChannelCount)) == "1");
		}
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = enabled;
	}
}

/**
	@brief Parses a trigger condition
 */
Trigger::Condition RSRTB2kOscilloscope::GetCondition(string reply)
{
	//LogTrace("\n");
	reply = Trim(reply);

	if(reply == "SHOR")
		return Trigger::CONDITION_LESS;
	else if(reply == "LONG")
		return Trigger::CONDITION_GREATER;
	else if(reply == "WITH")
		return Trigger::CONDITION_EQUAL;
	else if(reply == "OUTS")
		return Trigger::CONDITION_NOT_EQUAL;

	//unknown
	protocolError("RTB2k: GetCondition(): Unknown trigger condition [%s]\n", reply.c_str());
	return Trigger::CONDITION_LESS;
}

/**
	@brief Pushes settings for a trigger condition under a .Condition field
 */
void RSRTB2kOscilloscope::PushCondition(const string& path, Trigger::Condition cond)
{
	//LogTrace("\n");
	switch(cond)
	{
		case Trigger::CONDITION_LESS:
			sendOnly("%s SHOR", path.c_str());
			break;

		case Trigger::CONDITION_GREATER:
			sendOnly("%s LONG", path.c_str());
			break;

		case Trigger::CONDITION_EQUAL:
			sendOnly("%s WITH", path.c_str());
			break;

		case Trigger::CONDITION_NOT_EQUAL:
			sendOnly("%s OUTS", path.c_str());
			break;

		//Other values are not legal here, it seems
		default:
			break;
	}
}

void RSRTB2kOscilloscope::PushFloat(string path, float f)
{
	sendOnly("%s %1.5E", path.c_str(), f);
}

string RSRTB2kOscilloscope::PullTriggerSourceNumber(bool noDigital)
{
	//LogTrace("\n");
	// Trigger source: CH1 | CH2 | CH3 | CH4 | EXTernanalog | LINE | SBUS1 | SBUS2 | D0..D15
	string reply = converse(":TRIG:A:SOUR?");

	// Get channel number
	string channel;

	if (reply[0] == 'C' || reply[0] == 'D' || reply[0] == 'S')
	{
		int i = reply.size() - 1;
		while (i >= 0 && std::isdigit(reply[i])) {
			i--;
		}

		if (!(noDigital && reply[0] != 'C'))
			channel = reply.substr(i + 1);
    }
    else if (reply[0] == 'E')
    {
		channel = "5";
	}

	return channel;
}

vector<string> RSRTB2kOscilloscope::GetTriggerTypes()
{
	//LogTrace("\n");
	vector<string> ret;
	ret.push_back(RSRTB2kEdgeTrigger::GetTriggerName());
	ret.push_back(RSRTB2kLineTrigger::GetTriggerName());
	ret.push_back(RSRTB2kRiseTimeTrigger::GetTriggerName());
	ret.push_back(RSRTB2kRuntTrigger::GetTriggerName());
	ret.push_back(RSRTB2kTimeoutTrigger::GetTriggerName());
	ret.push_back(RSRTB2kVideoTrigger::GetTriggerName());
	ret.push_back(RSRTB2kWidthTrigger::GetTriggerName());
	return ret;
}

void RSRTB2kOscilloscope::PullTrigger()
{
	//LogTrace("\n");
	std::string reply;

	bool isUart = false;
	//Figure out what kind of trigger is active.
	reply = converse(":TRIG:A:TYPE?");
	if(reply == "EDGE")
		PullEdgeTrigger();
	else if(reply == "LINE")
		PullLineTrigger();
	else if(reply == "RIS")
		PullRiseTimeTrigger();
	else if(reply == "RUNT")
		PullRuntTrigger();
	else if(reply == "TIM")
		PullTimeoutTrigger();
	else if(reply == "TV")
		PullVideoTrigger();
	else if(reply == "WIDT")
		PullWidthTrigger();
	else
	{
		LogWarning("Unsupported trigger type \"%s\", defaulting to Edge.\n", reply.c_str());
		reply = "EDGE";
		// Default to Edge
		PullEdgeTrigger();
	}

	//Pull the source (same for all types of trigger)
	PullTriggerSource(m_trigger, reply, isUart);
}

/**
	@brief Reads the source of a trigger from the instrument
 */
void RSRTB2kOscilloscope::PullTriggerSource(Trigger* trig, string /* triggerModeName */, bool isUart)
{
	//LogTrace("\n");
	string reply;
	if(!isUart)
	{
		reply = converse(":TRIG:A:SOUR?");
	}
	else
	{
		reply = converse(":TRIG:A:SOUR?");
	}

	// Trigger source: CH1 | CH2 | CH3 | CH4 | EXTernanalog | LINE | SBUS1 | SBUS2 | D0..D15

	// Get channel number
	bool isAnalog;
	string number;
	string channel;

	if (reply[0] == 'C' || reply[0] == 'D')
	{
		int i = reply.size() - 1;
		while (i >= 0 && std::isdigit(reply[i])) {
			i--;
		}
		number = reply.substr(i + 1);
		isAnalog = (reply[0] == 'C');
		channel = (isAnalog ? "CHAN" :  "DIG") + number;
    }
    else
    {
		channel = reply;
	}

	auto chan = GetOscilloscopeChannelByHwName(channel);
	trig->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		protocolError("RTB2k: PullTriggerSource(): Unknown trigger source \"%s\"", reply.c_str());
}

void RSRTB2kOscilloscope::PushTrigger()
{
	//LogTrace("\n");
	auto et = dynamic_cast<RSRTB2kEdgeTrigger*>(m_trigger);
	auto lt = dynamic_cast<RSRTB2kLineTrigger*>(m_trigger);
	auto st = dynamic_cast<RSRTB2kRiseTimeTrigger*>(m_trigger);
	auto rt = dynamic_cast<RSRTB2kRuntTrigger*>(m_trigger);
	auto tt = dynamic_cast<RSRTB2kTimeoutTrigger*>(m_trigger);
	auto vt = dynamic_cast<RSRTB2kVideoTrigger*>(m_trigger);
	auto wt = dynamic_cast<RSRTB2kWidthTrigger*>(m_trigger);

	if(st)
	{
		sendOnly(":TRIG:A:TYPE RIS");
		sendOnly(":TRIG:A:SOUR %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushRiseTimeTrigger(st);
	}
	else if(lt)
	{
		sendOnly(":TRIG:A:TYPE LINE");
		sendOnly(":TRIG:A:SOUR LINE");
		PushLineTrigger(lt);
	}
	else if(rt)
	{
		sendOnly(":TRIG:A:TYPE RUNT");
		sendOnly(":TRIG:A:SOUR %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushRuntTrigger(rt);
	}
	else if(tt)
	{
		sendOnly(":TRIG:A:TYPE TIM");
		sendOnly(":TRIG:A:SOUR %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushTimeoutTrigger(tt);
	}
	else if(vt)
	{
		sendOnly(":TRIG:A:TYPE TV");
		sendOnly(":TRIG:A:SOUR %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushVideoTrigger(vt);
	}
	else if(wt)
	{
		sendOnly(":TRIG:A:TYPE WIDT");
		sendOnly(":TRIG:A:SOUR %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushWidthTrigger(wt);
	}
	else if(et)	   //must be last
	{
		sendOnly(":TRIG:A:TYPE EDGE");
		sendOnly(":TRIG:A:SOUR %s", GetChannelName(m_trigger->GetInput(0).m_channel->GetIndex()).c_str());
		PushEdgeTrigger(et, "EDGE");
	}
	else
		LogWarning("RTB2k: PushTrigger on an unimplemented trigger type.\n");
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void RSRTB2kOscilloscope::PullEdgeTrigger()
{
	//LogTrace("\n");
	double f;

	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RSRTB2kEdgeTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RSRTB2kEdgeTrigger(this);
	RSRTB2kEdgeTrigger* et = dynamic_cast<RSRTB2kEdgeTrigger*>(m_trigger);

	// Check for digital source
	// Level only for analog source
	et->SetLevel(stof(converse(":TRIG:A:LEV%s?", PullTriggerSourceNumber(true).c_str())));

	//Slope
	GetTriggerSlope(et, converse(":TRIG:A:EDGE:SLOP?"));

	//Coupling
	GetTriggerCoupling(et, converse(":TRIG:A:EDGE:COUP?"));

	//HF and noise reject
	et->SetHfRejectState(converse(":TRIG:A:EDGE:FILT:HFR?") == "1" ? true : false);
	et->SetNoiseRejectState(converse(":TRIG:A:EDGE:FILT:NREJ?") == "1" ? true : false);

	//Hold off time
	et->SetHoldoffTimeState(Trim(converse(":TRIG:A:HOLD:MODE?")) == "TIME" ? true : false);
	sscanf(converse(":TRIG:A:HOLD:TIME?").c_str(), "%lf", &f);
	et->SetHoldoffTime(f * FS_PER_SECOND);
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void RSRTB2kOscilloscope::PushEdgeTrigger(RSRTB2kEdgeTrigger* trig, const std::string /* trigType */)
{
	//LogTrace("\n");
	switch(trig->GetType())
	{
		case RSRTB2kEdgeTrigger::EDGE_RISING:
			sendOnly(":TRIG:A:EDGE:SLOP POS");
			break;

		case RSRTB2kEdgeTrigger::EDGE_FALLING:
			sendOnly(":TRIG:A:EDGE:SLOP NEG");
			break;

		case RSRTB2kEdgeTrigger::EDGE_ANY:
			sendOnly(":TRIG:A:EDGE:SLOP EITH");
			break;

		default:
			LogWarning("Invalid trigger type %d\n", trig->GetType());
			break;
	}

	switch(trig->GetCouplingType())
	{
		case RSRTB2kEdgeTrigger::COUPLING_AC:
			sendOnly(":TRIG:A:EDGE:COUP AC");
			break;

		case RSRTB2kEdgeTrigger::COUPLING_DC:
			sendOnly(":TRIG:A:EDGE:COUP DC");
			break;
		case RSRTB2kEdgeTrigger::COUPLING_LFREJECT:
			sendOnly(":TRIG:A:EDGE:COUP LFR");
			break;

		default:
			LogWarning("Invalid trigger coupling type %d\n", trig->GetCouplingType());
			break;
	}

	//HF and noise reject
	sendOnly(":TRIG:A:EDGE:FILT:HFR %zu", trig->GetHfRejectState());
	sendOnly(":TRIG:A:EDGE:FILT:NREJ %zu", trig->GetNoiseRejectState());

	//Hold off time - follow the sequence!
	PushFloat(":TRIG:A:HOLD:TIME", trig->GetHoldoffTime() * SECONDS_PER_FS);
	sendOnly(":TRIG:A:HOLD:MODE %s", trig->GetHoldoffTimeState() == true ? "TIME" : "OFF");

	// Level only for analog source
	PushFloat(":TRIG:A:LEV" + PullTriggerSourceNumber(true), trig->GetLevel());
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void RSRTB2kOscilloscope::PullWidthTrigger()
{
	//LogTrace("\n");
	double f;

	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RSRTB2kWidthTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RSRTB2kWidthTrigger(this);
	auto wt = dynamic_cast<RSRTB2kWidthTrigger*>(m_trigger);
	Unit fs(Unit::UNIT_FS);

	// Level only for analog and external source
	wt->SetLevel(stof(converse(":TRIG:A:LEV%s?", PullTriggerSourceNumber(true).c_str())));

	//Condition
	wt->SetCondition(GetCondition(converse(":TRIG:A:WIDT:RANG?")));

	// Lower/upper not available on RTB's pulse, only Time t and Variation is available
	double timeWidth;
	double timeDelta;
	sscanf(converse(":TRIG:A:WIDT:WIDT?").c_str(), "%lf", &timeWidth);
	sscanf(converse(":TRIG:A:WIDT:DELT?").c_str(), "%lf", &timeDelta);
	if (wt->GetCondition() == Trigger::CONDITION_LESS || wt->GetCondition() == Trigger::CONDITION_GREATER)
	{
		wt->SetWidthTime(timeWidth * FS_PER_SECOND);
		//~ wt->SetWidthVariation(timeDelta);
	}
	else
	{
		wt->SetWidthTime(timeWidth * FS_PER_SECOND);
		wt->SetWidthVariation(timeDelta * FS_PER_SECOND);
	}

	//Slope
	GetTriggerSlope(wt, converse(":TRIG:A:WIDT:POL?"));

	//Hysteresis only for analog source
	GetTriggerHysteresis(wt, converse(":CHAN%s:THR:HYST?", PullTriggerSourceNumber(true).c_str()));

	//Hold off time
	wt->SetHoldoffTimeState(Trim(converse(":TRIG:A:HOLD:MODE?")) == "TIME" ? true : false);
	sscanf(converse(":TRIG:A:HOLD:TIME?").c_str(), "%lf", &f);
	wt->SetHoldoffTime(f * FS_PER_SECOND);
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument
 */
void RSRTB2kOscilloscope::PushWidthTrigger(RSRTB2kWidthTrigger* trig)
{
	//LogTrace("\n");
	// Level only for analog source
	PushFloat(":TRIG:A:LEV" + PullTriggerSourceNumber(true), trig->GetLevel());
	PushCondition(":TRIG:A:WIDT:RANG", trig->GetCondition());
	// Lower/upper not available on RTB's pulse, only Time t and Variation is available
	if (trig->GetCondition() == Trigger::CONDITION_LESS || trig->GetCondition() == Trigger::CONDITION_GREATER)
	{
		PushFloat(":TRIG:A:WIDT:WIDT", trig->GetWidthTime() * SECONDS_PER_FS);
	}
	else
	{
		double widthTime = trig->GetWidthTime() * SECONDS_PER_FS;
		double widthVariation = trig->GetWidthVariation() * SECONDS_PER_FS;
		PushFloat(":TRIG:A:WIDT:WIDT", widthTime);
		PushFloat(":TRIG:A:WIDT:DELT", widthVariation);
	}
	sendOnly(":TRIG:A:WIDT:POL %s", trig->GetType() != RSRTB2kWidthTrigger::EDGE_FALLING ? "POS" : "NEG");

	//Hysteresis only for analog source
	string channel = PullTriggerSourceNumber(true);
	string hysteresis = "SMAL";
	if(trig->GetHysteresisType() == RSRTB2kWidthTrigger::HYSTERESIS_MEDIUM)
		hysteresis = "MED";
	else if(trig->GetHysteresisType() == RSRTB2kWidthTrigger::HYSTERESIS_LARGE)
		hysteresis = "LARG";
	sendOnly(":CHAN%s:THR:HYST %s", channel.c_str(), hysteresis.c_str());

	//Hold off time - follow the sequence!
	PushFloat(":TRIG:A:HOLD:TIME", trig->GetHoldoffTime() * SECONDS_PER_FS);
	sendOnly(":TRIG:A:HOLD:MODE %s", trig->GetHoldoffTimeState() == true ? "TIME" : "OFF");
}

/**
	@brief Reads settings for a runt-pulse trigger from the instrument
 */
void RSRTB2kOscilloscope::PullRuntTrigger()
{
	//LogTrace("\n");
	double f;

	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RSRTB2kRuntTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RSRTB2kRuntTrigger(this);
	RSRTB2kRuntTrigger* rt = dynamic_cast<RSRTB2kRuntTrigger*>(m_trigger);

	// Level only for analog and external source
	rt->SetLowerLevel(stof(converse(":TRIG:A:LEV%s:RUNT:LOW?", PullTriggerSourceNumber(true).c_str())));
	rt->SetUpperLevel(stof(converse(":TRIG:A:LEV%s:RUNT:UPP?", PullTriggerSourceNumber(true).c_str())));

	//Slope
	GetTriggerSlope(rt, converse(":TRIG:A:RUNT:POL?"));

	//Hysteresis only for analog source
	GetTriggerHysteresis(rt, converse(":CHAN%s:THR:HYST?", PullTriggerSourceNumber(true).c_str()));

	//Hold off time
	rt->SetHoldoffTimeState(Trim(converse(":TRIG:A:HOLD:MODE?")) == "TIME" ? true : false);
	sscanf(converse(":TRIG:A:HOLD:TIME?").c_str(), "%lf", &f);
	rt->SetHoldoffTime(f * FS_PER_SECOND);
}

/**
	@brief Pushes settings for a runt trigger to the instrument
 */
void RSRTB2kOscilloscope::PushRuntTrigger(RSRTB2kRuntTrigger* trig)
{
	//LogTrace("\n");
	string channel = PullTriggerSourceNumber(true);
	PushFloat(":TRIG:A:LEV" + channel + ":RUNT:LOW", trig->GetLowerLevel());
	PushFloat(":TRIG:A:LEV" + channel + ":RUNT:UPP", trig->GetUpperLevel());

	if (trig->GetType() == RSRTB2kRuntTrigger::EDGE_RISING)
		sendOnly(":TRIG:A:RUNT:POL POS");
	else if (trig->GetType() == RSRTB2kRuntTrigger::EDGE_FALLING)
		sendOnly(":TRIG:A:RUNT:POL NEG");
	else if (trig->GetType() == RSRTB2kRuntTrigger::EDGE_ANY)
		sendOnly(":TRIG:A:RUNT:POL EITH");

	//Hysteresis only for analog source
	string hysteresis = "SMAL";
	if(trig->GetHysteresisType() == RSRTB2kRuntTrigger::HYSTERESIS_MEDIUM)
		hysteresis = "MED";
	else if(trig->GetHysteresisType() == RSRTB2kRuntTrigger::HYSTERESIS_LARGE)
		hysteresis = "LARG";
	sendOnly(":CHAN%s:THR:HYST %s", channel.c_str(), hysteresis.c_str());

	//Hold off time - follow the sequence!
	PushFloat(":TRIG:A:HOLD:TIME", trig->GetHoldoffTime() * SECONDS_PER_FS);
	sendOnly(":TRIG:A:HOLD:MODE %s", trig->GetHoldoffTimeState() == true ? "TIME" : "OFF");
}

/**
	@brief Reads settings for a rise time trigger from the instrument
 */
void RSRTB2kOscilloscope::PullRiseTimeTrigger()
{
	//LogTrace("\n");
	double f;

	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RSRTB2kRiseTimeTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RSRTB2kRiseTimeTrigger(this);
	RSRTB2kRiseTimeTrigger* st = dynamic_cast<RSRTB2kRiseTimeTrigger*>(m_trigger);

	//Slope
	GetTriggerSlope(st, converse(":TRIG:A:RIS:SLOP?"));

	//Condition
	st->SetCondition(GetCondition(converse(":TRIG:A:RIS:RANG?")));

	//Time and Variation
	sscanf(converse(":TRIG:A:RIS:TIME?").c_str(), "%lf", &f);
	st->SetRiseTime(f * FS_PER_SECOND);
	sscanf(converse(":TRIG:A:RIS:DELT?").c_str(), "%lf", &f);
	st->SetRiseTimeVariation(f * FS_PER_SECOND);

	// Level only for analog and external source
	st->SetLowerLevel(stof(converse(":TRIG:A:LEV%s:RIS:LOW?", PullTriggerSourceNumber(true).c_str())));
	st->SetUpperLevel(stof(converse(":TRIG:A:LEV%s:RIS:UPP?", PullTriggerSourceNumber(true).c_str())));

	//Hysteresis only for analog source
	GetTriggerHysteresis(st, converse(":CHAN%s:THR:HYST?", PullTriggerSourceNumber(true).c_str()));

	//Hold off time
	st->SetHoldoffTimeState(Trim(converse(":TRIG:A:HOLD:MODE?")) == "TIME" ? true : false);
	sscanf(converse(":TRIG:A:HOLD:TIME?").c_str(), "%lf", &f);
	st->SetHoldoffTime(f * FS_PER_SECOND);
}

/**
	@brief Pushes settings for a slew rate trigger to the instrument
 */
void RSRTB2kOscilloscope::PushRiseTimeTrigger(RSRTB2kRiseTimeTrigger* trig)
{
	//LogTrace("\n");
	PushFloat(":TRIG:A:RIS:TIME", trig->GetRiseTime() * SECONDS_PER_FS);
	PushFloat(":TRIG:A:RIS:DELT", trig->GetRiseTimeVariation() * SECONDS_PER_FS);

	string channel = PullTriggerSourceNumber(true);
	PushFloat(":TRIG:A:LEV" + channel + ":RIS:LOW", trig->GetLowerLevel());
	PushFloat(":TRIG:A:LEV" + channel + ":RIS:UPP", trig->GetUpperLevel());

	if (trig->GetType() == RSRTB2kRiseTimeTrigger::EDGE_RISING)
		sendOnly(":TRIG:A:RIS:SLOP POS");
	else if (trig->GetType() == RSRTB2kRiseTimeTrigger::EDGE_FALLING)
		sendOnly(":TRIG:A:RIS:SLOP NEG");
	else if (trig->GetType() == RSRTB2kRiseTimeTrigger::EDGE_ANY)
		sendOnly(":TRIG:A:RIS:SLOP EITH");

	PushCondition(":TRIG:A:RIS:RANG", trig->GetCondition());

	//Hysteresis only for analog source
	string hysteresis = "SMAL";
	if(trig->GetHysteresisType() == RSRTB2kRiseTimeTrigger::HYSTERESIS_MEDIUM)
		hysteresis = "MED";
	else if(trig->GetHysteresisType() == RSRTB2kRiseTimeTrigger::HYSTERESIS_LARGE)
		hysteresis = "LARG";
	sendOnly(":CHAN%s:THR:HYST %s", channel.c_str(), hysteresis.c_str());

	//Hold off time - follow the sequence!
	PushFloat(":TRIG:A:HOLD:TIME", trig->GetHoldoffTime() * SECONDS_PER_FS);
	sendOnly(":TRIG:A:HOLD:MODE %s", trig->GetHoldoffTimeState() == true ? "TIME" : "OFF");
}

/**
	@brief Reads settings for a timeout trigger from the instrument
 */
void RSRTB2kOscilloscope::PullTimeoutTrigger()
{
	//LogTrace("\n");
	double f;

	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RSRTB2kTimeoutTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RSRTB2kTimeoutTrigger(this);
	RSRTB2kTimeoutTrigger* tt = dynamic_cast<RSRTB2kTimeoutTrigger*>(m_trigger);

	// Level only for analog source
	tt->SetLevel(stof(converse(":TRIG:A:LEV%s?", PullTriggerSourceNumber(true).c_str())));

	//time
	sscanf(converse(":TRIG:A:TIM:TIME?").c_str(), "%lf", &f);
	tt->SetTimeoutTime(f * FS_PER_SECOND);

	//Range type
	tt->SetType(Trim(converse(":TRIG:A:TIM:RANG?")) == "HIGH" ? RSRTB2kTimeoutTrigger::EDGE_RISING : RSRTB2kTimeoutTrigger::EDGE_FALLING);

	//Hysteresis only for analog source
	GetTriggerHysteresis(tt, converse(":CHAN%s:THR:HYST?", PullTriggerSourceNumber(true).c_str()));

	//Hold off time
	tt->SetHoldoffTimeState(Trim(converse(":TRIG:A:HOLD:MODE?")) == "TIME" ? true : false);
	sscanf(converse(":TRIG:A:HOLD:TIME?").c_str(), "%lf", &f);
	tt->SetHoldoffTime(f * FS_PER_SECOND);
}

/**
	@brief Pushes settings for a timeout trigger to the instrument
 */
void RSRTB2kOscilloscope::PushTimeoutTrigger(RSRTB2kTimeoutTrigger* trig)
{
	//LogTrace("\n");
	// Level only for analog source
	PushFloat(":TRIG:A:LEV" + PullTriggerSourceNumber(true), trig->GetLevel());

	//Timeout time
	PushFloat(":TRIG:A:TIM:TIME", trig->GetTimeoutTime() * SECONDS_PER_FS);

	//Range type
	sendOnly(":TRIG:A:TIM:RANG %s", (trig->GetType() == RSRTB2kTimeoutTrigger::EDGE_RISING) ? "HIGH" : "LOW");

	//Hysteresis only for analog source
	string channel = PullTriggerSourceNumber(true);
	string hysteresis = "SMAL";
	if(trig->GetHysteresisType() == RSRTB2kTimeoutTrigger::HYSTERESIS_MEDIUM)
		hysteresis = "MED";
	else if(trig->GetHysteresisType() == RSRTB2kTimeoutTrigger::HYSTERESIS_LARGE)
		hysteresis = "LARG";
	sendOnly(":CHAN%s:THR:HYST %s", channel.c_str(), hysteresis.c_str());

	//Hold off time - follow the sequence!
	PushFloat(":TRIG:A:HOLD:TIME", trig->GetHoldoffTime() * SECONDS_PER_FS);
	sendOnly(":TRIG:A:HOLD:MODE %s", trig->GetHoldoffTimeState() == true ? "TIME" : "OFF");
}

/**
	@brief Reads settings for a video trigger from the instrument
 */
void RSRTB2kOscilloscope::PullVideoTrigger()
{
	//LogTrace("\n");
	double f;

	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RSRTB2kVideoTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RSRTB2kVideoTrigger(this);
	RSRTB2kVideoTrigger* vt = dynamic_cast<RSRTB2kVideoTrigger*>(m_trigger);

	//Polarity
	GetTriggerSlope(vt, converse(":TRIG:A:TV:POL?"));

	//Standard type
	string reply = Trim(converse(":TRIG:A:TV:STAN?"));
	if(reply == "PAL")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_PAL);
	else if(reply == "NTSC")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_NTSC);
	else if(reply == "SEC")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_SEC);
	else if(reply == "PALM")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_PALM);
	else if(reply == "I576")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_I576);
	else if(reply == "P720")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_P720);
	else if(reply == "P1080")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_P1080);
	else if(reply == "I1080")
		vt->SetStandardType(RSRTB2kVideoTrigger::STANDARD_I1080);
	else
		LogWarning("RTB2k: Unsupported video standard type \"%s\"\n", reply.c_str());

	//Mode type
	reply = Trim(converse(":TRIG:A:TV:FIEL?"));
	if(reply == "ALL")
		vt->SetModeType(RSRTB2kVideoTrigger::MODE_ALL);
	else if(reply == "ODD")
		vt->SetModeType(RSRTB2kVideoTrigger::MODE_ODD);
	else if(reply == "EVEN")
		vt->SetModeType(RSRTB2kVideoTrigger::MODE_EVEN);
	else if(reply == "ALIN")
		vt->SetModeType(RSRTB2kVideoTrigger::MODE_ALIN);
	else if(reply == "LINE")
		vt->SetModeType(RSRTB2kVideoTrigger::MODE_LINE);
	else
		LogWarning("RTB2k: Unsupported video mode type \"%s\"\n", reply.c_str());

	//Line number
	vt->SetLineNumber(stoi(converse(":TRIG:A:TV:LINE?")));

	//Hold off time
	vt->SetHoldoffTimeState(Trim(converse(":TRIG:A:HOLD:MODE?")) == "TIME" ? true : false);
	sscanf(converse(":TRIG:A:HOLD:TIME?").c_str(), "%lf", &f);
	vt->SetHoldoffTime(f * FS_PER_SECOND);
}

/**
	@brief Pushes settings for a video trigger to the instrument
 */
void RSRTB2kOscilloscope::PushVideoTrigger(RSRTB2kVideoTrigger* trig)
{
	//LogTrace("\n");
	//Polarity type
	string param;
	if(trig->GetType() == RSRTB2kVideoTrigger::EDGE_RISING)
		param = "POS";
	else
		param = "NEG";
	sendOnly(":TRIG:A:TV:POL %s", param.c_str());

	//Standard type
	if(trig->GetStandardType() == RSRTB2kVideoTrigger::STANDARD_PAL)
		param = "PAL";
	else if(trig->GetStandardType() == RSRTB2kVideoTrigger::STANDARD_NTSC)
		param = "NTSC";
	else if(trig->GetStandardType() == RSRTB2kVideoTrigger::STANDARD_SEC)
		param = "SEC";
	else if(trig->GetStandardType() == RSRTB2kVideoTrigger::STANDARD_PALM)
		param = "PALM";
	else if(trig->GetStandardType() == RSRTB2kVideoTrigger::STANDARD_I576)
		param = "I576";
	else if(trig->GetStandardType() == RSRTB2kVideoTrigger::STANDARD_P720)
		param = "P720";
	else if(trig->GetStandardType() == RSRTB2kVideoTrigger::STANDARD_P1080)
		param = "P1080";
	else
		param = "I1080";
	sendOnly(":TRIG:A:TV:STAN %s", param.c_str());

	//Mode type
	if(trig->GetModeType() == RSRTB2kVideoTrigger::MODE_ALL)
		param = "ALL";
	else if(trig->GetModeType() == RSRTB2kVideoTrigger::MODE_ODD)
		param = "ODD";
	else if(trig->GetModeType() == RSRTB2kVideoTrigger::MODE_EVEN)
		param = "EVEN";
	else if(trig->GetModeType() == RSRTB2kVideoTrigger::MODE_ALIN)
		param = "ALIN";
	else
		param = "LINE";
	sendOnly(":TRIG:A:TV:FIEL %s", param.c_str());

	//Line number
	sendOnly(":TRIG:A:TV:LINE %zu", trig->GetLineNumber());

	//Hold off time - follow the sequence!
	PushFloat(":TRIG:A:HOLD:TIME", trig->GetHoldoffTime() * SECONDS_PER_FS);
	sendOnly(":TRIG:A:HOLD:MODE %s", trig->GetHoldoffTimeState() == true ? "TIME" : "OFF");
}

/**
	@brief Reads settings for a line trigger from the instrument
 */
void RSRTB2kOscilloscope::PullLineTrigger()
{
	//LogTrace("\n");
	double f;

	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<RSRTB2kLineTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RSRTB2kLineTrigger(this);
	RSRTB2kLineTrigger* lt = dynamic_cast<RSRTB2kLineTrigger*>(m_trigger);

	//Hold off time
	lt->SetHoldoffTimeState(Trim(converse(":TRIG:A:HOLD:MODE?")) == "TIME" ? true : false);
	sscanf(converse(":TRIG:A:HOLD:TIME?").c_str(), "%lf", &f);
	lt->SetHoldoffTime(f * FS_PER_SECOND);
}

/**
	@brief Pushes settings for a line trigger to the instrument
 */
void RSRTB2kOscilloscope::PushLineTrigger(RSRTB2kLineTrigger* trig)
{
	//LogTrace("\n");
	//Hold off time - follow the sequence!
	PushFloat(":TRIG:A:HOLD:TIME", trig->GetHoldoffTime() * SECONDS_PER_FS);
	sendOnly(":TRIG:A:HOLD:MODE %s", trig->GetHoldoffTimeState() == true ? "TIME" : "OFF");
}

/**
	@brief Forces 16-bit transfer mode on/off when for HD models
 */
void RSRTB2kOscilloscope::ForceHDMode(bool mode)
{
	//LogTrace("\n");
	m_highDefinition = mode;
}

/**
	@brief Converts 16-bit ADC samples to floating point
 */
void RSRTB2kOscilloscope::Convert16BitSamples(float* pout, const uint16_t* pin, float gain, float offset, size_t count)
{
	//LogTrace("\n");
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
void RSRTB2kOscilloscope::Convert16BitSamplesGeneric(float* pout, const uint16_t* pin, float gain, float offset, size_t count)
{
	//LogTrace("\n");
	for(size_t j=0; j<count; j++)
		pout[j] = offset + pin[j] * gain;
}

/**
	@brief Converts 8-bit ADC samples to floating point
 */
void RSRTB2kOscilloscope::Convert8BitSamples(float* pout, const uint8_t* pin, float gain, float offset, size_t count)
{
	//LogTrace("\n");
	//Divide large waveforms (>1M points) into blocks and multithread them
	//TODO: tune split
	if(count > 1000000)
	{
		//Round blocks to multiples of 32 samples for clean vectorization
		size_t numblocks = omp_get_max_threads();
		size_t lastblock = numblocks - 1;
		size_t blocksize = count / numblocks;
		blocksize = blocksize - (blocksize % 32);

		#pragma omp parallel for
		for(size_t i=0; i<numblocks; i++)
		{
			//Last block gets any extra that didn't divide evenly
			size_t nsamp = blocksize;
			if(i == lastblock)
				nsamp = count - i*blocksize;

			size_t off = i*blocksize;
			{
				Convert8BitSamplesGeneric(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
			}
		}
	}

	//Small waveforms get done single threaded to avoid overhead
	else
	{
			Convert8BitSamplesGeneric(pout, pin, gain, offset, count);
	}
}

/**
	@brief Generic backend for Convert8BitSamples()
 */
void RSRTB2kOscilloscope::Convert8BitSamplesGeneric(float* pout, const uint8_t* pin, float gain, float offset, size_t count)
{
	//LogTrace("\n");
	for(unsigned int k=0; k<count; k++)
		pout[k] = offset + pin[k] * gain;
}

//TODO
vector<WaveformBase*> RSRTB2kOscilloscope::ProcessAnalogWaveform(
	const std::vector<uint8_t>& data,
	size_t /* dataLen */,
	time_t ttime,
	uint32_t sampleCount,
	uint32_t bytesPerSample,
	float verticalStep,
	float verticalStart,
	float interval,
	int ch)
{
	//LogTrace("\n");
	vector<WaveformBase*> ret;

	//Set up the capture we're going to store our data into
	//~ auto cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(ch)->GetHwname());
	auto cap = AllocateAnalogWaveform(m_nickname + "." + m_channels[ch]->GetHwname());
	cap->m_timescale = round(interval);

	//~ cap->m_triggerPhase = h_off_frac;
	cap->m_startTimestamp = ttime;

	cap->Resize(sampleCount);
	cap->PrepareForCpuAccess();

	//Convert raw ADC samples to volts
	if (bytesPerSample == 2)
	{
		const uint16_t* wdata = reinterpret_cast<const uint16_t*>(data.data());
		Convert16BitSamples(
			cap->m_samples.GetCpuPointer(),
			wdata,
			verticalStep,
			verticalStart,
			sampleCount);

		cap->MarkSamplesModifiedFromCpu();
		ret.push_back(cap);
	}
	else if (bytesPerSample == 1)
	{
		const uint8_t* bdata = reinterpret_cast<const uint8_t*>(data.data());
		Convert8BitSamples(
			cap->m_samples.GetCpuPointer(),
			bdata,
			verticalStep,
			verticalStart,
			sampleCount);

		cap->MarkSamplesModifiedFromCpu();
		ret.push_back(cap);
	}
	else
		LogError("RTB2k: ProcessAnalogWaveform(): There is no conversion available for this number of bytes per sample: %i\n", bytesPerSample);

	return ret;
}

size_t RSRTB2kOscilloscope::ReadWaveformBlock(std::vector<uint8_t>* data, RSRTB2kOscilloscope::Metadata* metadata, std::function<void(float)> progress)
{
	//LogTrace("\n");
	//determine the length of the data from the SCPI block
	//the first character must be a ‘#’
	char tmp[128] = {0};

	m_transport->ReadRawData(2, (unsigned char*)tmp);
	if (tmp[0] != '#')
	{	// This error always occurs when a channel is activated during operation.

		// This is a protocol error, flush pending rx data
		//~ protocolErrorWithFlush("RTB2k: ReadWaveformBlock: the first character was not a ‘#’");
		protocolError("RTB2k: ReadWaveformBlock: the first character was not a ‘#’");
		// Stop aqcuisition after this protocol error
		//~ Stop();
		return 0;
	}
	size_t num_digits = atoi(tmp + 1);
	m_transport->ReadRawData(num_digits, (unsigned char*)tmp + 2);
	uint32_t len = atoi(tmp + 2);

	size_t readDataBytes = 0;
	data->resize(len);
	uint8_t* resultData = data->data();
	while(readDataBytes < len)	{
		size_t newBytes = m_transport->ReadRawData(len-readDataBytes, resultData + readDataBytes, progress);
		if(newBytes == 0) break;
		readDataBytes += newBytes;
	}

	// Read in the attached data: POIN, YINC, YOR, XINC
	int c = 0;
	size_t readBytes = 0;
	while(true)
	{
		readBytes = m_transport->ReadRawData(1, (unsigned char*)tmp + c);
		if (readBytes == 0 || tmp[c] == '\n')
		{
			readBytes = c;
			break;
		}
		c += 1;
	}
	if (readBytes == 0)
	{	// This is a protocol error, flush pending rx data
		protocolErrorWithFlush("RTB2k: ReadWaveformBlock: there are no attached data available");
		// Stop aqcuisition after this protocol error
		Stop();
		return 0;
	}
	if (sscanf(tmp, ";%u;%f;%f;%f",
		&metadata->sampleCount,
		&metadata->verticalStep,
		&metadata->verticalStart,
		&metadata->interval) != 4)
	{
		protocolError("RTB2k: Error parsing metadata: %s", tmp);
		// Stop aqcuisition after this protocol error
		Stop();
		return 0;
	}
	if (metadata->sampleCount > 0)
		metadata->bytesPerSample = len / metadata->sampleCount;
	metadata->interval *= FS_PER_SECOND;

	return readDataBytes;
}

bool RSRTB2kOscilloscope::AcquireData()
{
	//LogTrace("\n");
	// Transfer buffers
	std::vector<uint8_t> analogWaveformData[MAX_ANALOG];
	int analogWaveformDataSize[MAX_ANALOG] {0};
	RSRTB2kOscilloscope::Metadata analogMetadata[MAX_ANALOG];
	std::vector<uint8_t> digitalWaveformDataBytes[MAX_DIGITAL_POD];
	int digitalWaveformDataSize[MAX_DIGITAL_POD] {0};
	RSRTB2kOscilloscope::Metadata digitalMetadata[MAX_DIGITAL_POD];

	//State for this acquisition
	time_t ttime = 0;
	map<int, vector<WaveformBase*>> pending_waveforms;
	//double start = 0;
	vector<vector<WaveformBase*>> waveforms;
	vector<vector<SparseDigitalWaveform*>> digitalWaveforms;
	bool analogEnabled[MAX_ANALOG] = {false};
	bool digitalEnabled[MAX_DIGITAL] = {false};
	bool anyDigitalEnabled = false;
	RSRTB2kOscilloscope::Logicpod digitalPod[MAX_DIGITAL_POD] = {{false,0},{false,0}};
	size_t digitalSampleCount = 0;

	//Acquire the data (but don't parse it)

	// Detect active channels
	BulkCheckChannelEnableState();
	for(unsigned int i = 0; i <  m_analogChannelCount; i++)
	{	// Check all analog channels
		analogEnabled[i] = IsChannelEnabled(i);
	}
	for(unsigned int i = 0; i <  m_digitalChannelCount; i++)
	{	// Check digital channels
		digitalEnabled[i] = IsChannelEnabled(i+m_analogChannelCount);
		anyDigitalEnabled |= digitalEnabled[i];
		if (digitalEnabled[i])
		{
			if (i < 8)
			{
				digitalPod[0].enabled = true;
				digitalPod[0].progressChannel = i;
			}
			else
			{
				digitalPod[1].enabled = true;
				digitalPod[1].progressChannel = i;
			}
		}
	}

	// Notify about download operation start
	ChannelsDownloadStarted();

	// Get time from instrument (no high res timer on R&S scopes)
	ttime = time(NULL);
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	{	// Lock transport from now during all acquisition phase
		lock_guard<recursive_mutex> lock(m_transport->GetMutex());
		//start = GetTime();

		//Read the data from each analog waveform
		for(unsigned int i = 0; i < m_analogChannelCount; i++)
		{
			if(analogEnabled[i])
			{
				//~ string channel = m_channels[i]->GetHwname();
				string format = ":FORM:DATA UINT,";
				if (m_highDefinition)
					format += "16;";
				else
					format += "8;";
				m_transport->SendCommand(format +
					":" + m_channels[i]->GetHwname() + ":DATA:POIN MAX;" +
					":" + m_channels[i]->GetHwname() + ":DATA?;" +
					":" + m_channels[i]->GetHwname() + ":DATA:POIN?;YINC?;YOR?;XINC?");
				size_t readBytes = ReadWaveformBlock(&analogWaveformData[i], &analogMetadata[i], [i, this] (float progress) { ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, progress); });
				analogWaveformDataSize[i] = readBytes;
				ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
			}
		}

		if(anyDigitalEnabled)
		{
			//Read the data from each logic probe
			for(unsigned int i = 0; i < MAX_DIGITAL_POD; i++)
			{
				if(digitalPod[i].enabled)
				{
					unsigned int channel = digitalPod[i].progressChannel + m_digitalChannelBase;
					m_transport->SendCommand(":FORM:DATA UINT,8;:LOG" +
						to_string(i + 1) + ":DATA:POIN MAX;:LOG" +
						to_string(i + 1) + ":DATA?;:LOG" +
						to_string(i + 1) + ":DATA:POIN?;YINC?;YOR?;XINC?");
					size_t readBytes = ReadWaveformBlock(&digitalWaveformDataBytes[i], &digitalMetadata[i], [channel, this] (float progress) { ChannelsDownloadStatusUpdate(channel, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, progress); });
					digitalWaveformDataSize[i] = readBytes;
					digitalSampleCount = digitalMetadata[i].sampleCount;
					ChannelsDownloadStatusUpdate(channel, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
				}
			}
		}

		//At this point all data has been read so the scope is free to go do its thing while we crunch the results.
		//Re-arm the trigger if not in one-shot mode
		if(!m_triggerOneShot)
		{
			//~ sendOnly(":SINGLE");
			sendWithAck(":SINGLE"); // Without acknowledgment, reading errors often occur
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
	for (unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if (analogEnabled[i] && analogWaveformDataSize[i] > 0)
		{
			if ((uint32_t)analogWaveformDataSize[i] != analogMetadata[i].sampleCount * analogMetadata[i].bytesPerSample)
				protocolError("RTB2k: Invalid sample count from metadata: DataSize %i, sampleCount %i, bytesPerSample %i.\n",	analogWaveformDataSize[i], analogMetadata[i].sampleCount, analogMetadata[i].bytesPerSample);
			else
			{
				waveforms[i] = ProcessAnalogWaveform(
					analogWaveformData[i],
					analogWaveformDataSize[i],
					ttime,
					analogMetadata[i].sampleCount,
					analogMetadata[i].bytesPerSample,
					analogMetadata[i].verticalStep,
					analogMetadata[i].verticalStart,
					analogMetadata[i].interval,
					i);
			}
		}
	}

	//Save analog waveform data
	for (unsigned int i = 0; i < m_analogChannelCount; i++)
	{
		if (!analogEnabled[i] || analogWaveformDataSize[i] == 0)
			continue;

		//Done, update the data
		pending_waveforms[i].push_back(waveforms[i][0]);
	}

	//Process digital waveforms
	if(anyDigitalEnabled)
	{
		//Create buffers for output waveforms
		SequenceSet s;
		digitalWaveforms.resize(m_digitalChannelCount);
		for(size_t i=0; i < m_digitalChannelCount; i++)
		{
			auto nchan = m_digitalChannelBase + i;
			digitalWaveforms[i].push_back(AllocateDigitalWaveform(m_nickname + "." + m_channels[nchan]->GetHwname()));
			s[GetOscilloscopeChannel(nchan)] = digitalWaveforms[i][0];
		}

		//Now that we have the waveform data, unpack it into individual channels
		#pragma omp parallel for
		for (size_t i = 0; i < MAX_DIGITAL_POD; i++)
		{
			if (digitalPod[i].enabled)
			{
				for(size_t j=0; j < 8; j++)
				{
					if (digitalEnabled[j + (i * 8)])
					{
						//Bitmask for this digital channel
						//~ int16_t mask = (1 << j);
						uint8_t mask = (1 << j);

						//Create the waveform
						auto cap = digitalWaveforms[j + (i * 8)][0];
						cap->m_timescale = round(digitalMetadata[i].interval);
						cap->m_startTimestamp = ttime;
						cap->m_startFemtoseconds = fs;

						//Preallocate memory assuming no deduplication possible
						cap->Resize(digitalSampleCount);
						cap->PrepareForCpuAccess();

						//First sample never gets deduplicated
						bool last = (digitalWaveformDataBytes[i][0] & mask) ? true : false;
						size_t k = 0;
						cap->m_offsets[0] = 0;
						cap->m_durations[0] = 1;
						cap->m_samples[0] = last;

						//Read and de-duplicate the other samples
						//TODO: can we vectorize this somehow?
						for(size_t m=1; m < digitalSampleCount; m++)
						{
							bool sample = (digitalWaveformDataBytes[i][m] & mask) ? true : false;

							//Deduplicate consecutive samples with same value
							//FIXME: temporary workaround for rendering bugs
							//if(last == sample)
							if( (last == sample) && ((m+3) < digitalSampleCount) )
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
					}
				}
			}
		}

		//Save digital waveform data
		for(unsigned int i = 0; i < m_digitalChannelCount; i++)
		{
			if(!digitalEnabled[i] || digitalWaveformDataSize[i / 8] == 0)
				continue;

			//Done, update the data
			pending_waveforms[i + m_digitalChannelBase].push_back(digitalWaveforms[i][0]);
		}
	}

	// Tell the download monitor that waveform download has finished
	ChannelsDownloadFinished();

	for(int i=0; i<MAX_ANALOG; i++)
	{	// Free memory
		analogWaveformData[i] = {};
	}
	for(int i=0; i<MAX_DIGITAL_POD; i++)
	{	// Free memory
		digitalWaveformDataBytes[i] = {};
	}


	{	//Now that we have all of the pending waveforms, save them in sets across all channels
		lock_guard<mutex> lock(m_pendingWaveformsMutex);
		SequenceSet s;
		for(size_t i = 0; i < m_analogAndDigitalChannelCount; i++)
		{
			if(pending_waveforms.find(i) != pending_waveforms.end())
				s[GetOscilloscopeChannel(i)] = pending_waveforms[i][0];
		}
		m_pendingWaveforms.push_back(s);
	}

	//double dt = GetTime() - start;
	//LogDebug("Waveform download and processing took %.3f ms\n", dt * 1000);
	return true;
}

void RSRTB2kOscilloscope::PrepareAcquisition()
{	// Make sure everything is up to date
	//LogTrace("\n");
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;
	m_channelOffsets.clear();
}

void RSRTB2kOscilloscope::SetupForAcquisition()
{
	//LogTrace("\n");
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	//Configure transport format to raw UInteger 8-bit or 16-bit, little endian
	sendOnly("FORM:DATA UINT,%s", m_highDefinition ? "16" : "8");
	sendOnly("FORM:BORD LSBFirst");

	//Single trigger only works correctly in normal mode
	sendOnly(":TRIG:A:MODE NORM");
}

void RSRTB2kOscilloscope::Start()
{
	//LogTrace("\n");
	PrepareAcquisition();
	//~ sendOnly(":STOP;:SINGLE");	 //always do single captures, just re-trigger
	sendOnly(":ACQ:STAT BRE;:SINGLE");	 //always do single captures, just re-trigger

	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RSRTB2kOscilloscope::StartSingleTrigger()
{
	//LogTrace("\n");
	PrepareAcquisition();
	//~ sendOnly(":STOP;:SINGLE");
	sendOnly(":ACQ:STAT BRE;:SINGLE");

	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RSRTB2kOscilloscope::Stop()
{
	//LogTrace("\n");
	if(!m_triggerArmed)
		return;

	m_transport->SendCommandImmediate(":STOP");

	m_triggerArmed = false;
	m_triggerOneShot = true;

	//Clear out any pending data (the user doesn't want it, and we don't want stale stuff hanging around)
	ClearPendingWaveforms();
}

//TODO
void RSRTB2kOscilloscope::EnableTriggerOutput()
{
	//LogTrace("\n");
	//generator and trigger share the same output!
	sendOnly(":TRIG:OUT:MODE TRIG");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timebase

vector<uint64_t> RSRTB2kOscilloscope::GetSampleDepthsInterleaved()
{	// Get the legal memory depths for this scope in combined-channels mode.
	//LogTrace("\n");
	return GetSampleDepthsNonInterleaved();
}

//TODO
vector<uint64_t> RSRTB2kOscilloscope::GetSampleDepthsNonInterleaved()
{	// Get the legal memory depths for this scope in all-channels mode.
	//LogTrace("\n");

	// Sample rate depend on the number of active analog channels, digital probes and decoder

	vector<uint64_t> ret;
	const uint64_t k = 1000;
	const uint64_t m = k*k;
	bool memoryFull = false;
	bool pod1 = false;
	bool pod2 = false;
	bool chan1 = false;
	bool chan2 = false;
	bool chan3 = false;
	bool chan4 = false;

	// Memory depth can either be "Fixed" or "Auto" according to the scope's configuration
	// Let's check mode by getting memory depth value
	GetSampleDepth();

	memoryFull = GetActiveChannels(&pod1, &pod2, &chan1, &chan2, &chan3, &chan4);

	if (m_memoryDepthAuto)
	{
		if (pod1 && pod2)
		{
			if (chan1 || chan2 || chan3 || chan4)
			{	// [A] 2 logic pods and at least 1 channel
				ret = {0, 1258, 1264, 3064, 7564, 15064, 30064, 75064, 150064, 300064, 750064, 1500064,
					3000064, 7500058, 7500060, 7500061, 7500064, 9375058, 9375059, 10*m};
			}
			else
			{	// [F] 2 logic pods
				ret = {0, 1258, 1306, 3106, 7606, 15106, 30106, 75106, 150106, 300106, 750106, 1500106,
					3000106, 750060, 750070, 750082, 750106, 9375059, 9375064, 10*m};
			}
		}
		else if (pod1 || pod2)
		{
			if ( (chan1 && chan3) || (chan1 && chan4) || (chan2 && chan3) || (chan2 && chan4) )
			{	// [C] 1 logic pod and 1 channel per group
				ret = {0, 1258, 1272, 3072, 7572, 15072, 30072, 75072, 150072, 300072, 750072, 1500072,
					3000072, 7500072, 15000066, 15000069, 15000072, 18750066, 18750067, 18750068,
					19736908, 20*m};
			}
			else if ( (chan1 && chan2) || (chan3 && chan4) )
			{	// [D] 1 logic pod and 2 channel in same group
				ret = {0, 1258, 1264, 3064, 7564, 15064, 30064, 75064, 150064, 300064, 750064, 1500064,
					3000064, 7500058, 7500060, 7500061, 7500064, 9375058, 9375059, 10*m};
			}
			else if (chan1 || chan2 || chan3 || chan4)
			{	// [C] 1 logic pod and 1 channel
				ret = {0, 1258, 1272, 3072, 7572, 15072, 30072, 75072, 150072, 300072, 750072, 1500072,
					3000072, 7500072, 15000066, 15000069, 15000072, 18750066, 18750067, 18750068,
					19736908, 20*m};
			}
			else
			{	// [E] 1 logic pod
				ret = {0, 1258, 1314, 3114, 7614, 15114, 30114, 75114, 150114, 300114, 750114, 1500114,
					3000114, 7500114, 15000068, 15000090, 15000114, 18750067, 18750072, 18750078,
					19736908, 20*m};
			}
		}
		else if ( (chan1 && chan2) || (chan3 && chan4) )
		{	// [A] 2 channel in same group or more channels
			ret = {0, 1258, 1264, 3064, 7564, 15064, 30064, 75064, 150064, 300064, 750064, 1500064,
				3000064, 7500058, 7500060, 7500061, 7500064, 9375058, 9375059, 10*m};
		}
		else
		{	// [B] 1 channel or 1 channel per group
			ret = {0, 1258, 1270, 2470, 6070, 15070, 30070, 60070, 1500070, 3000070, 6000070,
				15000066, 15000069, 15000070, 15000072, 18750066, 18750067, 18750068, 19736908, 20*m};
		}
	}
	else if (memoryFull)
		ret = {0, 10*k, 20*k, 50*k, 100*k, 200*k, 500*k, 1*m, 2*m, 5*m, 10*m, 20*m};
	else
		ret = {0, 10*k, 20*k, 50*k, 100*k, 200*k, 500*k, 1*m, 2*m, 5*m, 10*m};

	return ret;
}

vector<uint64_t> RSRTB2kOscilloscope::GetSampleRatesInterleaved()
{	// Get the legal sampling rates (in Hz) for this scope in combined-channels mode.
	//LogTrace("\n");
	return GetSampleRatesNonInterleaved();
}

//TODO
vector<uint64_t> RSRTB2kOscilloscope::GetSampleRatesNonInterleaved()
{	// Get the legal sampling rates (in Hz) for this scope in all-channels mode.
	//LogTrace("\n");

	// Sample depths depend on the number of active analog channels, digital probes and decoder:
	// max 20 Mpts per channel
	// 2 logic probes activ: every channel 10 Mpts
	// protocol decoder activ: every channel 10 Mpts
	// 1 analog channel and 1 logic probe: 20 Mpts
	// 2 analog channels from different group (1+3/4 or 2+3/4) and 1 logic probe: 20 Mpts
	// 2 analog channels from same group (1+2 or 3+4): 10 Mpts
	// 3-4 analog channels: 10 Mpts

	vector<uint64_t> ret;
	const uint64_t k = 1000;
	const uint64_t m = k*k;
	bool pod1 = false;
	bool pod2 = false;
	bool chan1 = false;
	bool chan2 = false;
	bool chan3 = false;
	bool chan4 = false;

	// Memory depth can either be "Fixed" or "Auto" according to the scope's configuration
	// Let's check mode by getting memory depth value
	GetSampleDepth();

	GetActiveChannels(&pod1, &pod2, &chan1, &chan2, &chan3, &chan4);

	if (m_memoryDepthAuto)
	{
		if (pod1 && pod2)
		{
			if ( (chan1 && chan3) || (chan1 && chan4) || (chan2 && chan3) || (chan2 && chan4) )
			{	// [B] 2 logic pods and 1 channel per group
				ret = {0, 1666, 4166, 8333, 16667, 41667, 83333, 166670, 416670, 833330, 1666700, 4166700,
					8333300, 15625*k, 41667*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
			else if ( (chan1 && chan2) || (chan3 && chan4) )
			{	// [E] 2 logic pods and 2 channel in same group
				ret = {0, 1667, 8333, 16667, 83333, 166670, 833330, 1666700, 4166700, 8333300, 15625*k,
					41667*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
			else if (chan1 || chan2 || chan3 || chan4)
			{	// [B] 2 logic pods and at least 1 channel
				ret = {0, 1666, 4166, 8333, 16667, 41667, 83333, 166670, 416670, 833330, 1666700, 4166700,
					8333300, 15625*k, 41667*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
			else
			{	// [B] 2 logic pods
				ret = {0, 1666, 4166, 8333, 16667, 41667, 83333, 166670, 416670, 833330, 1666700, 4166700,
					8333300, 15625*k, 41667*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
		}
		else if (pod1 || pod2)
		{
			if ( (chan1 && chan3) || (chan1 && chan4) || (chan2 && chan3) || (chan2 && chan4) )
			{	// [C] 1 logic pod and 1 channel per group
				ret = {0, 3333, 8333, 16667, 33333, 83333, 166670, 333330, 833330, 1666700, 3289500,
					8333300, 15625*k, 31250*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
			else if ( (chan1 && chan2) || (chan3 && chan4) )
			{	// [D] 1 logic pod and 2 channel in same group
				ret = {0, 1666, 4166, 8333, 41667, 83333, 166670, 416670, 833330, 1666700, 4166700, 8333300,
					15625*k, 41667*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
			else if (chan1 || chan2 || chan3 || chan4)
			{	// [C] 1 logic pod and 1 channel
				ret = {0, 3333, 8333, 16667, 33333, 83333, 166670, 333330, 833330, 1666700, 3289500,
					8333300, 15625*k, 31250*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
			else
			{	// [C] 1 logic pod
				ret = {0, 3333, 8333, 16667, 33333, 83333, 166670, 333330, 833330, 1666700, 3289500,
					8333300, 15625*k, 31250*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
			}
		}
		else if ( (chan1 && chan2) || (chan3 && chan4) )
		{	// [B] 2 channel in same group or more channels
				ret = {0, 1666, 4166, 8333, 16667, 41667, 83333, 166670, 416670, 833330, 1666700, 4166700,
					8333300, 15625*k, 41667*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m};
		}
		else
		{	// [A] 1 channel or 1 channel per group
				ret = {0, 3333, 8333, 16667, 33333, 83333, 166670, 333330, 833330, 1666700, 3289500,
					8333300, 15625*k, 31250*k, 62500*k, 156250*k, 312500*k, 625*m, 1250*m, 2500*m};
		}
	}
	else
		// All occurring values
		ret = {0, 1, 3, 4, 8, 16, 20, 33, 41, 83, 166, 208, 333, 416, 833, 1666, 2083, 3333, 4166, 8333,
			16667, 20833, 33333, 41667, 83333, 166670, 208330, 333330, 416670, 833330, 1666700, 2083300,
			3289500, 4166700, 8333300, 15625*k, 20833*k, 41667*k, 62500*k,
			156250*k, 312500*k, 625*m, 1250*m, 2500*m};

	return ret;
}

set<RSRTB2kOscilloscope::InterleaveConflict> RSRTB2kOscilloscope::GetInterleaveConflicts()
{
	//LogTrace("\n");
	set<InterleaveConflict> ret;

	ret.emplace(InterleaveConflict(GetOscilloscopeChannel(0), GetOscilloscopeChannel(1)));
	ret.emplace(InterleaveConflict(GetOscilloscopeChannel(2), GetOscilloscopeChannel(3)));

	return ret;
}

uint64_t RSRTB2kOscilloscope::GetSampleRate()
{
	//LogTrace("\n");
    {
        lock_guard<recursive_mutex> lock(m_cacheMutex);
        if(m_sampleRateValid)
            return m_sampleRate;
    }
	double f;
	string reply;
	reply = converse(":ACQ:POIN:ARAT?");

    lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(sscanf(reply.c_str(), "%lf", &f) == 1)
	{
		m_sampleRate = static_cast<int64_t>(f);
		m_sampleRateValid = true;
	}
	else
	{
		LogError("RTB2k: invalid sample rate value '%s'", reply.c_str());
	}
	return m_sampleRate;
}

uint64_t RSRTB2kOscilloscope::GetSampleDepth()
{
	//LogTrace("\n");
    {
        lock_guard<recursive_mutex> lock(m_cacheMutex);
        if(m_memoryDepthValid)
            return m_memoryDepth;
    }
    string depthAuto = converse(":ACQ:POIN:AUT?");
	string reply = converse(":ACQ:POIN?");
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	double f = Unit(Unit::UNIT_SAMPLEDEPTH).ParseString(reply);
	m_memoryDepth = static_cast<int64_t>(f);
	m_memoryDepthAuto = (depthAuto == "1");
	m_memoryDepthValid = true;
	return m_memoryDepth;
}

void RSRTB2kOscilloscope::SetSampleDepth(uint64_t depth)
{
	//LogTrace("\n");
	{	//Need to lock the transport mutex when setting depth to prevent changing depth during an acquisition
		lock_guard<recursive_mutex> lock(m_transport->GetMutex());
		sendWithAck("ACQ:POIN %" PRIu64 "", depth);
	}
	//Don't update the cache because the scope is likely to round the value
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_memoryDepthValid = false;
	m_sampleRateValid = false;
	m_triggerOffsetValid = false;
}

//TODO
void RSRTB2kOscilloscope::SetSampleRate(uint64_t rate)
{
	//LogTrace("\n");
	{ 	//Need to lock the transport mutex when setting rate to prevent changing rate during an acquisition
		lock_guard<recursive_mutex> lock(m_transport->GetMutex());

		double sampletime = GetSampleDepth() / (double)rate;
		double scale = sampletime / 12;
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "%1.0E", scale);
		sendWithAck(":TIM:SCAL %s", tmp);

		//The sample rate is not always updated correctly in normal mode.
		//That is an error in the firmware.
		sendWithAck(":TRIG:A:MODE AUTO");
		sendWithAck(":TRIG:A:MODE NORM");
	}
	//Don't update the cache because the scope is likely to round the value
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;

	// With a large time base, the change takes a while before the setting is updated.
	if (rate < 200000) // 200 kSa/s
		this_thread::sleep_for(std::chrono::milliseconds(1000));
}

bool RSRTB2kOscilloscope::IsInterleaving()
{	// Checks if the scope is currently combining channels.
	//LogTrace("\n");
	if(IsChannelEnabled(0) && IsChannelEnabled(1))
	{	// Non-Interleaving if Channel 1 and 2 are active
		return false;
	}
	if(IsChannelEnabled(2) && IsChannelEnabled(3))
	{	// Non-Interleaving if Channel 3 and 4 are active
		return false;
	}
	if(IsChannelEnabled(LOGICPOD1) || IsChannelEnabled(LOGICPOD2))
	{	// Non-Interleaving if Logicpod active
		return false;
	}
	//TODO
	//~ if(BUS PROTOCOL ACTIVE)
	//~ {	// Non-Interleaving if bus protocol active
		//~ return false;
	//~ }

	return true;
}

bool RSRTB2kOscilloscope::SetInterleaving(bool /* combine*/)
{	// Configures the scope to combine channels.
	//LogTrace("\n");
	//Setting interleaving is not supported, it's always hardware managed
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timebase Triggering

void RSRTB2kOscilloscope::SetTriggerOffset(int64_t offset)
{
	//LogTrace("\n");
	//R&S's standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	{
		sendWithAck(":TIM:POS %1.2E", (halfwidth + m_triggerReference - offset) * SECONDS_PER_FS);

		//Don't update the cache because the scope is likely to round the offset we ask for.
		//If we query the instrument later, the cache will be updated then.
		m_triggerOffsetValid = false;
	}
}

int64_t RSRTB2kOscilloscope::GetTriggerOffset()
{
	//LogTrace("\n");
	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_triggerOffsetValid)
			return m_triggerOffset;
	}

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	//Result comes back in scientific notation
	double sec = stod(converse(":TIM:POS?"));
	double perc = stod(converse(":TIM:REF?"));
	double ref = 0;
	double scale = 0;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if (perc == 50.0)
	{
		ref = sec;
		m_triggerReference = 0;
	}
	else
	{
		scale = stod(converse(":TIM:SCAL?")) * 5;
		ref = sec - (perc < 50.0 ? -scale : scale);
		m_triggerReference = static_cast<int64_t>(round((perc < 50.0 ? -scale : scale) * FS_PER_SECOND));
	}

	m_triggerOffset = static_cast<int64_t>(round(ref * FS_PER_SECOND));
	m_triggerOffset = -(m_triggerOffset - halfwidth);
	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Analog bank configuration

std::string RSRTB2kOscilloscope::GetChannelName(size_t channel)
{
	//LogTrace("\n");
	//Returns the name that can be used as a parameter.
	if(channel < m_digitalChannelBase)
	 	return string("CH") + to_string(channel + 1);
	else if (channel == m_extTrigChannel->GetIndex())
		return string("EXT");
	else if (channel == m_lineTrigChannel->GetIndex())
		return string("LINE");
	else
	 	return string("D") + to_string(channel - m_digitalChannelBase);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

std::string RSRTB2kOscilloscope::GetDigitalChannelBankName(size_t channel)
{
 	//LogTrace("\n");
	return ((channel - m_digitalChannelBase) < 8) ? "1" : "2";
}

vector<Oscilloscope::DigitalBank> RSRTB2kOscilloscope::GetDigitalBanks()
{
	//LogTrace("\n");
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

Oscilloscope::DigitalBank RSRTB2kOscilloscope::GetDigitalBank(size_t channel)
{
	//LogTrace("\n");
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

bool RSRTB2kOscilloscope::IsDigitalHysteresisConfigurable()
{
	return true;
}

bool RSRTB2kOscilloscope::IsDigitalThresholdConfigurable()
{
	//LogTrace("\n");
	return true;
}

float RSRTB2kOscilloscope::GetDigitalHysteresis(size_t channel)
{
	//LogTrace("\n");
	if( (channel < m_digitalChannelBase) || (m_digitalChannelCount == 0) )
		return 0;

	string bank = GetDigitalChannelBankName(channel);
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDigitalHysteresis.find(bank) != m_channelDigitalHysteresis.end())
			return m_channelDigitalHysteresis[bank];
	}

	float result = 0;

	string reply;
	reply = converse(":LOG%s:HYST?", bank.c_str());
	if (reply == "SMAL")
		result = 1;
	else if (reply == "MED")
		result = 2;
	else if (reply == "LARG")
		result = 3;
	else
		protocolError("RTB2k: invalid digital hysteresis '%s'", reply.c_str());

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelDigitalHysteresis[bank] = result;
	return result;
}

float RSRTB2kOscilloscope::GetDigitalThreshold(size_t channel)
{
	//LogTrace("\n");
	//threshold level value between -2 V and +8 V in steps of 10 mV
	if( (channel < m_digitalChannelBase) || (m_digitalChannelCount == 0) )
		return 0;

	string bank = GetDigitalChannelBankName(channel);
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDigitalThresholds.find(bank) != m_channelDigitalThresholds.end())
			return m_channelDigitalThresholds[bank];
	}

	float result;

	string reply = converse(":LOG%s:THR:UDL?", bank.c_str());
	if(sscanf(reply.c_str(), "%f", &result)!=1)
	{
		protocolError("RTB2k: invalid digital threshold offset value '%s'",reply.c_str());
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelDigitalThresholds[bank] = result;
	return result;
}

void RSRTB2kOscilloscope::SetDigitalHysteresis(size_t channel, float level)
{
	//LogTrace("\n");
	string bank = GetDigitalChannelBankName(channel);
	string hyst = "SMAL";

	if (level <= 1)
		hyst = "SMAL";
	else if (level >= 2 && level < 3)
		hyst = "MED";
	else if (level >= 3)
		hyst = "LARG";

	sendWithAck(":LOG%s:HYST %s", bank.c_str(), hyst.c_str());

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDigitalHysteresis[bank] = level;
}

void RSRTB2kOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	//LogTrace("\n");
	string bank = GetDigitalChannelBankName(channel);

	sendWithAck(":LOG%s:THR:UDL %1.2E", bank.c_str(), level);

	//Don't update the cache because the scope is likely to round the offset we ask for.
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDigitalThresholds.erase(bank);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator mode

vector<FunctionGenerator::WaveShape> RSRTB2kOscilloscope::GetAvailableWaveformShapes(int /*chan*/)
{
	//LogTrace("\n");
	vector<WaveShape> ret;

	// DC | SINusoid | SQUare | PULSe | TRIangle | RAMP | SINC | ARBitrary | EXPonential

	ret.push_back(SHAPE_DC);
	ret.push_back(SHAPE_SINE);
	ret.push_back(SHAPE_SQUARE);
	ret.push_back(SHAPE_PULSE);
	ret.push_back(SHAPE_TRIANGLE);
	//~ ret.push_back(SHAPE_RAMP);
	ret.push_back(SHAPE_SAWTOOTH_UP);
	ret.push_back(SHAPE_SAWTOOTH_DOWN);
	ret.push_back(SHAPE_SINC);
	ret.push_back(SHAPE_ARB);
	ret.push_back(SHAPE_EXPONENTIAL_RISE);

	return ret;
}

bool RSRTB2kOscilloscope::GetFunctionChannelActive(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgEnabled.find(chan) != m_awgEnabled.end())
			return m_awgEnabled[chan];
	}

	string reply = converse(":WGEN:OUTP?");

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_awgEnabled[chan] = (reply == "1" ? true : false);

		return m_awgEnabled[chan];
	}
}

void RSRTB2kOscilloscope::SetFunctionChannelActive(int chan, bool on)
{
	//LogTrace("\n");
	sendWithAck(":WGEN:OUTP %s",(on ? "ON" : "OFF"));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgEnabled[chan] = on;
}

float RSRTB2kOscilloscope::GetFunctionChannelDutyCycle(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgDutyCycle.find(chan) != m_awgDutyCycle.end())
			return m_awgDutyCycle[chan];
	}

	string duty = converse(":WGEN:FUNC:PULS:DCYC?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	float dutyf;
	if(sscanf(duty.c_str(), "%f", &dutyf)!=1)
	{
		protocolError("invalid channel ducy cycle value '%s'",duty.c_str());
	}
	m_awgDutyCycle[chan] = (dutyf/100);
	return m_awgDutyCycle[chan];
}

void RSRTB2kOscilloscope::SetFunctionChannelDutyCycle(int chan, float duty)
{
	//LogTrace("\n");
	sendWithAck(":WGEN:FUNC:PULS:DCYC %.4f", round(duty * 100));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgDutyCycle.erase(chan);
}

float RSRTB2kOscilloscope::GetFunctionChannelAmplitude(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgRange.find(chan) != m_awgRange.end())
			return m_awgRange[chan];
	}

	string amp = converse(":WGEN:VOLT?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	float ampf;
	if(sscanf(amp.c_str(), "%f", &ampf)!=1)
	{
		protocolError("RTB2k: invalid channel amplitude value '%s'",amp.c_str());
	}
	m_awgRange[chan] = ampf;

	return m_awgRange[chan];
}

void RSRTB2kOscilloscope::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	//LogTrace("\n");
	sendWithAck(":WGEN:VOLT %.4f",amplitude);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgRange.erase(chan);
}

float RSRTB2kOscilloscope::GetFunctionChannelOffset(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgOffset.find(chan) != m_awgOffset.end())
			return m_awgOffset[chan];
	}

	string offset = converse(":WGEN:VOLT:OFFS?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	float offsetf;
	if(sscanf(offset.c_str(), "%f", &offsetf)!=1)
	{
		protocolError("RTB2k: invalid channel attenuation value '%s'",offset.c_str());
	}
	m_awgOffset[chan] = offsetf;
	return m_awgOffset[chan];
}

void RSRTB2kOscilloscope::SetFunctionChannelOffset(int chan, float offset)
{
	//LogTrace("\n");
	sendWithAck(":WGEN:VOLT:OFFS %.4f",offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgOffset.erase(chan);
}

float RSRTB2kOscilloscope::GetFunctionChannelFrequency(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgFrequency.find(chan) != m_awgFrequency.end())
			return m_awgFrequency[chan];
	}

	string freq = converse(":WGEN:FREQ ?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	float freqf;
	if(sscanf(freq.c_str(), "%f", &freqf)!=1)
	{
		protocolError("RTB2k: invalid channel frequency value '%s'",freq.c_str());
	}
	m_awgFrequency[chan] = freqf;
	return m_awgFrequency[chan];
}

void RSRTB2kOscilloscope::SetFunctionChannelFrequency(int chan, float hz)
{
	//LogTrace("\n");
	sendWithAck(":WGEN:FREQ %.4f",hz);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgFrequency.erase(chan);
}

FunctionGenerator::WaveShape RSRTB2kOscilloscope::GetFunctionChannelShape(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgShape.find(chan) != m_awgShape.end())
			return m_awgShape[chan];
	}

	//Query the basic wave parameters
	// DC | SINusoid | SQUare | PULSe | TRIangle | RAMP | SINC | ARBitrary | EXPonential
	string shape = converse(":WGEN:FUNC?");

	//Crack the replies
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(shape == "DC")
			m_awgShape[chan] = FunctionGenerator::SHAPE_DC;
		else if(shape == "SIN")
			m_awgShape[chan] = FunctionGenerator::SHAPE_SINE;
		else if(shape == "SQU")
			m_awgShape[chan] = FunctionGenerator::SHAPE_SQUARE;
		else if(shape == "PULS")
			m_awgShape[chan] = FunctionGenerator::SHAPE_PULSE;
		else if(shape == "TRI")
			m_awgShape[chan] = FunctionGenerator::SHAPE_TRIANGLE;
		else if(shape == "RAMP")
		{
			//~ LogWarning("RTB2k: wave type RAMP unimplemented\n");
			//~ m_awgShape[chan] = FunctionGenerator::SHAPE_RAMP;
			if (converse(":WGEN:FUNC:RAMP:POL?") == "POS")
				m_awgShape[chan] = FunctionGenerator::SHAPE_SAWTOOTH_UP;
			else
				m_awgShape[chan] = FunctionGenerator::SHAPE_SAWTOOTH_DOWN;
		}
		else if(shape == "SINC")
			m_awgShape[chan] = FunctionGenerator::SHAPE_SINC;
		else if(shape == "ARB")
		{
			//~ LogWarning("RTB2k: wave type ARB unimplemented\n");
			m_awgShape[chan] = FunctionGenerator::SHAPE_ARB;
		}
		else if(shape == "EXP")
			m_awgShape[chan] = FunctionGenerator::SHAPE_EXPONENTIAL_RISE;
		else
			LogWarning("RTB2k: wave type %s unimplemented\n", shape.c_str());

		return m_awgShape[chan];
	}
}

void RSRTB2kOscilloscope::SetFunctionChannelShape(int chan, FunctionGenerator::WaveShape shape)
{
	//LogTrace("\n");
	string basicType;
	string basicProp;

	switch(shape)
	{
		case SHAPE_DC:
			basicType = "DC";
			break;

		case SHAPE_SINE:
			basicType = "SIN";
			break;

		case SHAPE_SQUARE:
			basicType = "SQU";
			break;

		case SHAPE_PULSE:
			basicType = "PULS";
			break;

		case SHAPE_TRIANGLE:
			basicType = "TRI";
			break;

		//TODO: "ramp"
		//~ case SHAPE_RAMP:
		case SHAPE_SAWTOOTH_UP:
			basicType = "RAMP";
			basicProp = "POS";
			break;

		case SHAPE_SAWTOOTH_DOWN:
			basicType = "RAMP";
			basicProp = "NEG";
			break;

		case SHAPE_SINC:
			basicType = "SINC";
			break;

		//TODO: "arb"
		case SHAPE_ARB:
			basicType = "ARB";
			break;

		case SHAPE_EXPONENTIAL_RISE:
			basicType = "EXP";
			break;

		//unsupported, ignore
		default:
			return;
	}

	//Select type
	sendWithAck(":WGEN:FUNC %s", basicType.c_str());
	if (basicType == "RAMP")
	{
		sendWithAck(":WGEN:FUNC:RAMP:POL %s", basicProp.c_str());
	}
	//~ if(basicType == "ARB")
	//~ {	// TODO when  available in Magnova firmware
		//~ //sendWithAck(":FGEN:WAV:SHAP %s", (arbmap[arbType].substr(1)).c_str());
	//~ }

	//Update cache
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	// Duty cycle  is reset when changing shape
	m_awgDutyCycle.erase(chan);
	m_awgShape[chan] = shape;
}

FunctionGenerator::OutputImpedance RSRTB2kOscilloscope::GetFunctionChannelOutputImpedance(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgImpedance.find(chan) != m_awgImpedance.end())
			return m_awgImpedance[chan];
	}

	string load = converse(":WGEN:OUTP:LOAD ?");

	FunctionGenerator::OutputImpedance imp = (load == "R50") ? IMPEDANCE_50_OHM : IMPEDANCE_HIGH_Z;

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgImpedance[chan] = imp;
	return m_awgImpedance[chan];
}

void RSRTB2kOscilloscope::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	//LogTrace("\n");
	string imp;
	if(z == IMPEDANCE_50_OHM)
		imp = "R50";
	else
		imp = "HIGH";

	sendWithAck(":WGEN:OUTP:LOAD %s",imp.c_str());

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgImpedance.erase(chan);
}

bool RSRTB2kOscilloscope::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	//LogTrace("\n");
	return true;
}

float RSRTB2kOscilloscope::GetFunctionChannelRiseTime(int chan)
{
	//LogTrace("\n");
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgRiseTime.find(chan) != m_awgRiseTime.end())
			return m_awgRiseTime[chan];
	}

	string time = converse(":WGEN:FUNC:PULS:ETIM?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	float timef;
	if(sscanf(time.c_str(), "%f", &timef)!=1)
	{
		protocolError("invalid channel rise time value '%s'",time.c_str());
	}
	m_awgRiseTime[chan] = timef * FS_PER_SECOND;
	return m_awgRiseTime[chan];
}

void RSRTB2kOscilloscope::SetFunctionChannelRiseTime(int chan, float fs)
{
	//LogTrace("\n");
	sendWithAck(":WGEN:FUNC:PULS:ETIM %.10f",fs * SECONDS_PER_FS);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgRiseTime.erase(chan);
}

float RSRTB2kOscilloscope::GetFunctionChannelFallTime(int chan)
{
	//LogTrace("\n");
	return GetFunctionChannelRiseTime(chan);
}

void RSRTB2kOscilloscope::SetFunctionChannelFallTime(int chan, float fs)
{
	//LogTrace("\n");
	SetFunctionChannelRiseTime(chan, fs);
}
