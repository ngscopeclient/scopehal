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
 * Generic Siglent scope driver. Currently supports SDS2000X+ and SDS1104X-E.
 * This file was originally derived from the LeCroy driver but has been modified extensively.
 * Note that this port replaces the previous Siglent driver, which was non-functional. That is available in the git
 * archive if needed.
 *
 * SDS2000/5000/6000 port (c) 2021 Dave Marples.
 * Tested on SDS2000X+. If someone wants to loan an SDS5000/6000A/6000Pro for testing those
 * can be integrated and the changes required should be limited.
 *
 * Starting SDS1x04X-E port (c) 2021 Dannes and Stefan Mandl
 * Using Programming Guide  PG01-E02D an Firmware 6.1.37R8
 * Tested on SDS1104X-E. Should also support SDS10000CML+/CNL+/Dl+/E+/F+,  SDS2000/2000x, SDS1000x/1000x+,
 * SDS1000X-E/X-C but these are untested. Feedback and/or loan instruments appreciated.
 *
 *
 * Current State
 * =============
 *
 * SDS2000XP
 *
 * - Basic functionality for analog channels works.
 * - Feature detection via LCISL? or *OPT? not yet implemented.
 *     - With a request, *OPT? command got added in firmware 1.5.2R1.
 *     - LCISL? command present in firmware 1.3.5 through 1.5.2 (although undocumented).
 * - Digital channels are not implemented (code in here is leftover from LeCroy)
 * - Triggers are untested.
 * - Sampling lengths up to 10MSamples are supported. 50M and 100M need to be batched and will be
 *   horribly slow.
 *
 * SDS1104X-E
 *
 * Using Programming Guide  PG01-E02D and Firmware 6.2.37R8
 *   receive  data from scope on c1 c2 c3 c4
 *   set EDGE Trigger on channel
 *   using 4 Channels ( 70 kS  25 MS/s)        got 4,23 WFM/s
 *   using 4 Channels ( 700 kpts  100 MSa/s)   got 1,62 WFM/s
 *   using 1 Channels ( 1.75 Mpts  250 MSa/s)  got 2,38 WFM/s
 *   using 4 Channels ( 3.5 Mpts  500 MSa/s)   got 0,39 WFM/s
 * 
 *  TODO Click "Reload configuration from scope"   sometimes we loosing WAVE rendering ( threading issue ?)
 *  TODO sometimes socket timeout (Warning: Socket read failed errno=11 errno=4)
 *
 * Note that this port replaces the previous Siglent driver, which was non-functional. That is available in the git
 * archive if needed.
 */

#include "scopehal.h"
#include "SiglentSCPIOscilloscope.h"
#include "base64.h"

#include "DropoutTrigger.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"

#include <locale>
#include <stdarg.h>
#include <omp.h>
#include <thread>
#include <chrono>
#include <cinttypes>

using namespace std;

static const struct
{
	const char* name;
	float val;
} c_sds2000xp_threshold_table[] = {{"TTL", 1.5F}, {"CMOS", 1.65F}, {"LVCMOS33", 1.65F}, {"LVCMOS25", 1.25F}, {NULL, 0}};

static const std::chrono::milliseconds c_trigger_delay(1000);	 // Delay required when forcing trigger
static const char* c_custom_thresh = "CUSTOM,";					 // Prepend string for custom digital threshold
static const float c_thresh_thresh = 0.01f;						 // Zero equivalence threshold for fp comparisons

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentSCPIOscilloscope::SiglentSCPIOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
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
	, m_highDefinition(false)
{
	//Enable command rate limiting
	//TODO: only for some firmware versions or instrument SKUs?
	transport->EnableRateLimiting(chrono::milliseconds(50));

	//standard initialization
	FlushConfigCache();
	IdentifyHardware();
	DetectAnalogChannels();
	SharedCtorInit();
	DetectOptions();

	//Figure out if scope is in low or high bit depth mode so we can download waveforms with the correct format
	GetADCMode(0);
}

string SiglentSCPIOscilloscope::converse(const char* fmt, ...)
{
	string ret;
	char opString[128];
	va_list va;
	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	ret = m_transport->SendCommandQueuedWithReply(opString, false);
	return ret;
}

void SiglentSCPIOscilloscope::sendOnly(const char* fmt, ...)
{
	char opString[128];
	va_list va;

	va_start(va, fmt);
	vsnprintf(opString, sizeof(opString), fmt, va);
	va_end(va);

	m_transport->SendCommandQueued(opString);
}

void SiglentSCPIOscilloscope::SharedCtorInit()
{
	m_digitalChannelCount = 0;

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
		//TODO: this is stupid, it shares the same name as our scope input!
		//Is this going to break anything??
		m_awgChannel = new FunctionGeneratorChannel(this, "C1", "#808080", m_channels.size());
		m_channels.push_back(m_awgChannel);
		m_awgChannel->SetDisplayName("AWG");
	}
	else
		m_awgChannel = nullptr;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			// Omit header and units in numbers for responses to queries.
			sendOnly("CHDR OFF");
			// change momory size to 14K. less data on the network
			SetSampleDepth(14000);
			// ToDo fix render we get this imformation sometimes late
			// then we miss channel in GUI. Workaround ....
			GetChannelVoltageRange(0, 0);
			GetChannelVoltageRange(1, 0);
			GetChannelVoltageRange(2, 0);
			GetChannelVoltageRange(3, 0);
			GetChannelOffset(0, 0);
			GetChannelOffset(1, 0);
			GetChannelOffset(2, 0);
			GetChannelOffset(3, 0);

			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:

			//This is the default behavior, but it's safer to explicitly specify it
			//TODO: save bandwidth and simplify parsing by doing OFF
			sendOnly("CHDR SHORT");

			//Desired format for waveform data
			//Only use increased bit depth if the scope actually puts content there!
			sendOnly(":WAVEFORM:WIDTH %s", m_highDefinition ? "WORD" : "BYTE");
			
			break;
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly("CHDR SHORT");
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	//Controlled memory depth, adjust sample rate based on this
	if(m_modelid == MODEL_SIGLENT_SDS6000A)
		sendOnly("ACQ:MMAN FMDepth");

	//Clear the state-change register to we get rid of any history we don't care about
	PollTrigger();

	//Enable deduplication for vertical axis commands once we know what we're dealing with
	switch(m_modelid)
	{
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			m_transport->DeduplicateCommand("OFST");
			m_transport->DeduplicateCommand("VOLT_DIV");
			break;

		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			m_transport->DeduplicateCommand("OFFSET");
			m_transport->DeduplicateCommand("SCALE");
			break;

		default:
			break;
	}
}

void SiglentSCPIOscilloscope::ParseFirmwareVersion()
{
	//Check if version requires size workaround (1.3.9R6 and older)
	m_ubootMajorVersion = 0;
	m_ubootMinorVersion = 0;
	m_fwMajorVersion = 0;
	m_fwMinorVersion = 0;
	m_fwPatchVersion = 0;
	m_fwPatchRevision = 0;

	//Version format for 1.5.2R3 and older
	sscanf(m_fwVersion.c_str(), (m_fwVersion.find('R') != std::string::npos) ? "%d.%d.%d.%d.%dR%d" : "%d.%d.%d.%d.%d.%d",
		&m_ubootMajorVersion,
		&m_ubootMinorVersion,
		&m_fwMajorVersion,
		&m_fwMinorVersion,
		&m_fwPatchVersion,
		&m_fwPatchRevision);
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
	m_requireSizeWorkaround = false;

	if(m_vendor.compare("Siglent Technologies") == 0)
	{
		// TODO(dannas): Tighten this check
		// The Programming Guide says that we support SDS1000CFL, SDS1000A,
		// SDS10000CML+/CNL+/Dl+/E+/F+,  SDS2000/2000x, SDS1000x/1000x+,
		// SDS1000X-E/X-C. But I only have a SDS1004X-E so we should only check for that
		if(m_model.compare(0, 4, "SDS1") == 0)
		{
			m_modelid = MODEL_SIGLENT_SDS1000;
			m_maxBandwidth = 100;
			if(m_model.compare(4, 1, "2") == 0)
				m_maxBandwidth = 200;
			if(m_fwVersion != "8.2.6.1.37R9")
				LogWarning("Siglent firmware \"%s\" is not tested\n", m_fwVersion.c_str());
			return;
		}
		else if(m_model.compare(0, 4, "SDS2") == 0 && m_model.back() == 'E')
		{
			m_modelid = MODEL_SIGLENT_SDS2000XE;

			m_maxBandwidth = 100;
			if(m_model.compare(4, 1, "2") == 0)
				m_maxBandwidth = 200;
			else if(m_model.compare(4, 1, "3") == 0)
				m_maxBandwidth = 350;
			if(m_model.compare(4, 1, "5") == 0)
				m_maxBandwidth = 500;
			return;
		}
		else if(m_model.compare(0, 4, "SDS2") == 0 && m_model.back() == 's')
		{
			m_modelid = MODEL_SIGLENT_SDS2000XP;

			m_maxBandwidth = 100;
			if(m_model.compare(4, 1, "2") == 0)
				m_maxBandwidth = 200;
			else if(m_model.compare(4, 1, "3") == 0)
				m_maxBandwidth = 350;
			if(m_model.compare(4, 1, "5") == 0)
				m_maxBandwidth = 500;

			//Firmware 1.6.2R5 (and newer) has 7 digits in version string whereas
			//older firmware has 6 digits.
			if(m_fwVersion.size() == 11)
			{
				ParseFirmwareVersion();
				//Firmware 1.3.9R6 and older require size workaround.
				if(m_fwMajorVersion < 1)
					m_requireSizeWorkaround = true;
				else if((m_fwMajorVersion == 1) && (m_fwMinorVersion < 3))
					m_requireSizeWorkaround = true;
				else if((m_fwMajorVersion == 1) && (m_fwMinorVersion == 3) && (m_fwPatchVersion < 9))
					m_requireSizeWorkaround = true;
				else if((m_fwMajorVersion == 1) && (m_fwMinorVersion == 3) && (m_fwPatchVersion == 9) && (m_fwPatchRevision <= 6))
					m_requireSizeWorkaround = true;
			}

			if(m_requireSizeWorkaround)
				LogTrace("Current firmware (%s) requires size workaround\n", m_fwVersion.c_str());

			//TODO: check for whether we actually have the license
			m_hasFunctionGen = true;
		}
		else if( (m_model.compare(0, 4, "SDS2") == 0) && (m_model.find("HD") != string::npos) )
		{
			m_maxBandwidth = 100;
			if(m_model.compare(4, 1, "2") == 0)
				m_maxBandwidth = 200;
			else if(m_model.compare(4, 1, "3") == 0)
				m_maxBandwidth = 350;
			else if(m_model.compare(4, 1, "5") == 0) // No 500 MHz HD model but one can have BW update option
				m_maxBandwidth = 500;

			//TODO: check for whether we actually have the license
			//(no SCPI command for this yet)
			m_hasFunctionGen = true;

			//2000X+ HD is native 12 bit resolution but supports 8 bit data transfer with higher refresh rate
			// This can be overriden by driver 16bits setting
			m_highDefinition = true;

			m_modelid = MODEL_SIGLENT_SDS2000X_HD;

			ParseFirmwareVersion();
			if(m_fwMajorVersion>=1 && m_fwMinorVersion >= 2)
			{	// Only pre-production firmware version (e.g. 1.1.7) uses SCPI standard size reporting
				LogTrace("Current firmware (%s) requires size workaround\n", m_fwVersion.c_str());
				m_requireSizeWorkaround = true;
			}
		}
		else if(m_model.compare(0, 4, "SDS5") == 0)
		{
			m_modelid = MODEL_SIGLENT_SDS5000X;

			m_maxBandwidth = 350;
			if(m_model.compare(5, 1, "5") == 0)
				m_maxBandwidth = 500;
			if(m_model.compare(5, 1, "0") == 0)
				m_maxBandwidth = 1000;
		}
		else if(m_model.compare(0, 4, "SDS6") == 0)
		{
			m_modelid = MODEL_SIGLENT_SDS6000A;

			m_maxBandwidth = 500;
			if(m_model.compare(4, 1, "1") == 0)
				m_maxBandwidth = 1000;
			if(m_model.compare(4, 2, "2") == 0)
				m_maxBandwidth = 2000;
		}

		else if(m_model.compare(0, 4, "SDS8") == 0)
		{
			m_maxBandwidth = 70;
			if(m_model.compare(4, 1, "1") == 0)
				m_maxBandwidth = 100;
			else if(m_model.compare(4, 1, "2") == 0)
				m_maxBandwidth = 200;

			// Native 12 bit resolution but supports 8 bit data transfer with higher refresh rate
			// This can be overriden by driver 16bits setting
			m_highDefinition = true;

			m_modelid = MODEL_SIGLENT_SDS800X_HD;
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

void SiglentSCPIOscilloscope::DetectOptions()
{
	//AddDigitalChannels(16);

	//TODO: support feature checking for SDS2000XP
	//SDS2000XP supports optional feature checking via LCISL? <OPT> on all firmware
	//Valid OPT choices: AWG, MSO, FLX, CFD, I2S, 1553, PWA, MANC, SENT
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
void SiglentSCPIOscilloscope::DetectAnalogChannels()
{
	int nchans = 1;

	// Either character 6 or 7 of the model name is the number of channels,
	// depending on number of  digits in model name - SDSnnn vs SDSnnnn.
	// Currently only SDS800X_HD is the outlier..
	unsigned int chanoffset;

	if(m_model.compare(0, 4, "SDS8") == 0) {
		chanoffset = 5;
	} else {
		chanoffset = 6;
	}

	if(m_model.length() > chanoffset)
	{
		switch(m_model[chanoffset])
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
		string chname = string("C") + to_string(i+1);

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
	m_probeIsActive.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;
	m_meterModeValid = false;
	m_awgEnabled.clear();
	m_awgDutyCycle.clear();
	m_awgRange.clear();
	m_awgOffset.clear();
	m_awgFrequency.clear();
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
unsigned int SiglentSCPIOscilloscope::GetMeasurementTypes()
{
	unsigned int type = 0;
	return type;
}

/**
	@brief See what features we have
 */
unsigned int SiglentSCPIOscilloscope::GetInstrumentTypes() const
{
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasFunctionGen)
		type |= INST_FUNCTION;
	return type;
}

uint32_t SiglentSCPIOscilloscope::GetInstrumentTypesForChannel(size_t i) const
{
	if(m_awgChannel && (m_awgChannel->GetIndex() == i) )
		return Instrument::INST_FUNCTION;

	//If we get here, it's an oscilloscope channel
	return Instrument::INST_OSCILLOSCOPE;
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

	//Analog
	if(i < m_analogChannelCount)
	{
		//See if the channel is enabled, hide it if not
		string reply;

		switch(m_modelid)
		{
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS1000:
			case MODEL_SIGLENT_SDS2000XE:
				reply = converse("C%d:TRACE?", i + 1);

				{
					lock_guard<recursive_mutex> lock2(m_cacheMutex);
					m_channelsEnabled[i] = (reply.find("OFF") != 0);	//may have a trailing newline, ignore that
				}
				break;
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS800X_HD:
			case MODEL_SIGLENT_SDS2000XP:
			case MODEL_SIGLENT_SDS2000X_HD:
			case MODEL_SIGLENT_SDS5000X:
			case MODEL_SIGLENT_SDS6000A:
				reply = converse(":CHANNEL%d:SWITCH?", i + 1);
				{
					lock_guard<recursive_mutex> lock2(m_cacheMutex);
					m_channelsEnabled[i] = (reply.find("OFF") != 0);	//may have a trailing newline, ignore that
				}
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
	}
	else
	{
		//Digital

		//See if the channel is on
		size_t nchan = i - (m_analogChannelCount + 1);
		string str = converse(":DIGITAL:D%d?", nchan);

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelsEnabled[i] = (str == "OFF") ? false : true;
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	return m_channelsEnabled[i];
}

void SiglentSCPIOscilloscope::EnableChannel(size_t i)
{
	bool wasInterleaving = IsInterleaving();

	//No need to lock the main mutex since sendOnly now pushes to the queue

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
	{
		switch(m_modelid)
		{
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS1000:
			case MODEL_SIGLENT_SDS2000XE:
				sendOnly(":C%d:TRACE ON", i + 1);
				break;
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS800X_HD:
			case MODEL_SIGLENT_SDS2000XP:
			case MODEL_SIGLENT_SDS2000X_HD:
			case MODEL_SIGLENT_SDS5000X:
			case MODEL_SIGLENT_SDS6000A:
				sendOnly(":CHANNEL%d:SWITCH ON", i + 1);
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
	}
	else if(i == m_extTrigChannel->GetIndex())
	{
		//Trigger can't be enabled
	}
	else
	{
		//Digital channel
		sendOnly(":DIGITAL:D%d ON", i - (m_analogChannelCount + 1));
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelsEnabled[i] = true;

	//Sample rate and memory depth can change if interleaving state changed
	if(IsInterleaving() != wasInterleaving)
	{
		m_memoryDepthValid = false;
		m_sampleRateValid = false;
	}
}

bool SiglentSCPIOscilloscope::CanEnableChannel(size_t i)
{
	// Can enable all channels except trigger
	return !(i == m_extTrigChannel->GetIndex());
}

void SiglentSCPIOscilloscope::DisableChannel(size_t i)
{
	bool wasInterleaving = IsInterleaving();

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
	}

	if(i < m_analogChannelCount)
	{
		switch(m_modelid)
		{
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS1000:
			case MODEL_SIGLENT_SDS2000XE:
				sendOnly("C%d:TRACE OFF", i + 1);
				break;
			// -------------------------------------------------
			case MODEL_SIGLENT_SDS800X_HD:
			case MODEL_SIGLENT_SDS2000XP:
			case MODEL_SIGLENT_SDS2000X_HD:
			case MODEL_SIGLENT_SDS5000X:
			case MODEL_SIGLENT_SDS6000A:
				//If this is an analog channel, just toggle it
				if(i < m_analogChannelCount)
					sendOnly(":CHANNEL%d:SWITCH OFF", i + 1);
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
	}
	else if(i == m_extTrigChannel->GetIndex())
	{
		//Trigger can't be enabled
	}
	else
	{
		//Digital channel

		//Disable this channel
		sendOnly(":DIGITAL:D%d OFF", i - (m_analogChannelCount + 1));

		//If we have NO digital channels enabled, disable the appropriate digital bus

		//bool anyDigitalEnabled = false;
		//        for (uint32_t c=m_analogChannelCount+1+((chNum/8)*8); c<(m_analogChannelCount+1+((chNum/8)*8)+c_digiChannelsPerBus); c++)
		//          anyDigitalEnabled |= m_channelsEnabled[c];

		//        if(!anyDigitalEnabled)
		//sendOnly(":DIGITAL:BUS%d:DISP OFF",chNum/8);
	}

	//Sample rate and memory depth can change if interleaving state changed
	if(IsInterleaving() != wasInterleaving)
	{
		m_memoryDepthValid = false;
		m_sampleRateValid = false;
	}
}

vector<OscilloscopeChannel::CouplingType> SiglentSCPIOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_GND);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
			ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
			ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
			ret.push_back(OscilloscopeChannel::COUPLE_GND);
			break;

		//SDS6000A does not support 50 ohm AC coupling
		case MODEL_SIGLENT_SDS6000A:
			ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
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

OscilloscopeChannel::CouplingType SiglentSCPIOscilloscope::GetChannelCoupling(size_t i)
{
	if(i >= m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	string replyType;
	string replyImp;

	m_probeIsActive[i] = false;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			replyType = Trim(converse("C%d:COUPLING?", i + 1));
			if(replyType == "A50")
				return OscilloscopeChannel::COUPLE_AC_50;
			else if(replyType == "D50")
				return OscilloscopeChannel::COUPLE_DC_50;
			else if(replyType == "A1M")
				return OscilloscopeChannel::COUPLE_AC_1M;
			else if(replyType == "D1M")
				return OscilloscopeChannel::COUPLE_DC_1M;
			else if(replyType == "GND")
				return OscilloscopeChannel::COUPLE_GND;
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			replyType = Trim(converse(":CHANNEL%d:COUPLING?", i + 1).substr(0, 2));
			replyImp = Trim(converse(":CHANNEL%d:IMPEDANCE?", i + 1).substr(0, 3));

			if(replyType == "AC")
				return (replyImp.find("FIF") == 0) ? OscilloscopeChannel::COUPLE_AC_50 : OscilloscopeChannel::COUPLE_AC_1M;
			else if(replyType == "DC")
				return (replyImp.find("FIF") == 0) ? OscilloscopeChannel::COUPLE_DC_50 : OscilloscopeChannel::COUPLE_DC_1M;
			else if(replyType == "GN")
				return OscilloscopeChannel::COUPLE_GND;
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

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

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_AC_50:
					sendOnly("C%d:COUPLING %s", i + 1, "A50");
					break;

				case OscilloscopeChannel::COUPLE_DC_50:
					sendOnly("C%d:COUPLING %s", i + 1, "D50");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					sendOnly("C%d:COUPLING %s", i + 1, "A1M");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					sendOnly("C%d:COUPLING %s", i + 1, "D1M");
					break;

				//treat unrecognized as ground
				case OscilloscopeChannel::COUPLE_GND:
				default:
					sendOnly("C%d:COUPLING %s", i + 1, "GND");
					break;
			}
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:  // todo: 50 ohm not supported, any implications?
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
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
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

double SiglentSCPIOscilloscope::GetChannelAttenuation(size_t i)
{
	if(i > m_analogChannelCount)
		return 1;

	//TODO: support ext/10
	if(i == m_extTrigChannel->GetIndex())
		return 1;

	string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			reply = converse("C%d:ATTENUATION?", i + 1);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			reply = converse(":CHANNEL%d:PROBE?", i + 1);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

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

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			// Values larger than 1x should be sent as integers, and values smaller
			// should be sent as floating point numbers with one decimal.
			if(atten >= 1)
			{
				sendOnly("C%d:ATTENUATION %d", i + 1, (int)atten);
			}
			else
			{
				sendOnly("C%d:ATTENUATION %.1lf", i + 1, atten);
			}
			break;

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":CHANNEL%d:PROBE VALUE,%lf", i + 1, atten);
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

vector<unsigned int> SiglentSCPIOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			//"no limit"
			ret.push_back(0);

			//Supported by all models
			ret.push_back(20);
			break;

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			//"no limit"
			ret.push_back(0);

			//Supported by all models
			ret.push_back(20);

			if(m_maxBandwidth > 200)
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

unsigned int SiglentSCPIOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i > m_analogChannelCount)
		return 0;

	string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			reply = converse("C%d:BANDWIDTH_LIMIT?", i + 1);
			if(reply == "OFF")
				return 0;
			else if(reply == "ON")
				return 20;
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			reply = converse(":CHANNEL%d:BWLIMIT?", i + 1);
			if(reply == "FULL")
				return 0;
			else if(reply == "20M")
				return 20;
			else if(reply == "200M")
				return 200;
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	LogWarning("SiglentSCPIOscilloscope::GetChannelCoupling got invalid bwlimit %s\n", reply.c_str());
	return 0;
}

void SiglentSCPIOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			switch(limit_mhz)
			{
				case 0:
					sendOnly("BANDWIDTH_LIMIT C%d,OFF", i + 1);
					break;

				case 20:
					sendOnly("BANDWIDTH_LIMIT C%d,ON", i + 1);
					break;

				default:
					LogWarning("SiglentSCPIOscilloscope::invalid bwlimit set request (%dMhz)\n", limit_mhz);
			}
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
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
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
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

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			sendOnly("C%d:INVERTSET %s", i + 1, invert ? "ON" : "OFF");
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":CHANNEL%d:INVERT %s", i + 1, invert ? "ON" : "OFF");
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

bool SiglentSCPIOscilloscope::IsInverted(size_t i)
{
	if(i >= m_analogChannelCount)
		return false;

	string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			reply = Trim(converse("C%d:INVERTSET?", i + 1));
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			reply = Trim(converse(":CHANNEL%d:INVERT?", i + 1));
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			reply = "";
			break;
			// --------------------------------------------------
	}
	return (reply == "ON");
}

void SiglentSCPIOscilloscope::SetChannelDisplayName(size_t i, string name)
{
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return;

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return;

	//Update in hardware
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			if(i < m_analogChannelCount)
			{
				sendOnly(":CHANNEL%zu:LABEL:TEXT \"%s\"", i + 1, name.c_str());
				sendOnly(":CHANNEL%zu:LABEL ON", i + 1);
			}
			else
			{
				sendOnly(":DIGITAL:LABEL%zu \"%s\"", i - (m_analogChannelCount + 1), name.c_str());
			}
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

string SiglentSCPIOscilloscope::GetChannelDisplayName(size_t i)
{
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return "";

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return m_extTrigChannel->GetHwname();

	//Analog and digital channels use completely different namespaces, as usual.
	//Because clean, orthogonal APIs are apparently for losers?
	string name = "";

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			break;

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
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
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	//Default to using hwname if no alias defined
	if(name == "")
		name = chan->GetHwname();

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
	string sinr = "";

	if(m_triggerForced)
	{
		// The force trigger completed, return the sample set
		m_triggerForced = false;
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			sinr = converse("SAMPLE_STATUS?");
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sinr = converse(":TRIGGER:STATUS?");
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

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

int SiglentSCPIOscilloscope::ReadWaveformBlock(uint32_t maxsize, char* data, bool hdSizeWorkaround)
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
		{
			LogError("ReadWaveformBlock: threw away 20 bytes of data and never saw a '#'\n");
			return 0;
		}
	}

	//Read length of the length field
	m_transport->ReadRawData(1, &tmp);
	int lengthOfLength = tmp - '0';

	//Read the actual length field
	char textlen[10] = {0};
	m_transport->ReadRawData(lengthOfLength, (unsigned char*)textlen);
	uint32_t getLength = atoi(textlen);

	uint32_t len = getLength;
	if(hdSizeWorkaround)
		len *= 2;
	len = min(len, maxsize);

	// Now get the data
	m_transport->ReadRawData(len, (unsigned char*)data);

	if(hdSizeWorkaround)
		return getLength*2;
	return getLength;
}

/**
	@brief Optimized function for checking channel enable status en masse with less round trips to the scope
 */
void SiglentSCPIOscilloscope::BulkCheckChannelEnableState()
{
	vector<int> uncached;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//Check enable state in the cache.
		for(unsigned int i = 0; i < m_analogChannelCount; i++)
		{
			if(m_channelsEnabled.find(i) == m_channelsEnabled.end())
				uncached.push_back(i);
		}
	}

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

			m_transport->SendCommand(":WAVEFORM:SOURCE C" + to_string(i + 1) + ";:WAVEFORM:PREAMBLE?");
			if(WAVEDESC_SIZE != ReadWaveformBlock(WAVEDESC_SIZE, wavedescs[i]))
				LogError("ReadWaveformBlock for wavedesc %u failed\n", i);

			// I have no idea why this is needed, but it certainly is
			m_transport->ReadReply();
		}
	}

	return true;
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
	int ch)
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

	//double h_off_frac = fmodf(h_off, interval);	   //fractional sample position, in fs

	double h_off_frac = 0;	  //((interval*datalen)/2)+h_off;

	if(h_off_frac < 0)
		h_off_frac = h_off;	   //interval + h_off_frac;	   //double h_unit = *reinterpret_cast<double*>(pdesc + 244);

	//Raw waveform data
	size_t num_samples;
	if(m_highDefinition)
		num_samples = datalen / 2;
	else
		num_samples = datalen;
	size_t num_per_segment = num_samples / num_sequences;
	// int16_t* wdata = (int16_t*)&data[0];
	// int8_t* bdata = (int8_t*)&data[0];
	const int16_t* wdata = reinterpret_cast<const int16_t*>(data);
	const int8_t* bdata = reinterpret_cast<const int8_t*>(data);

	float codes_per_div;

	//Codes per div varies with vertical scale on SDS6000A!
	//500 uV/div: 63.75 codes per div
	//1 mV - 10 mV/div: 127.5 codes per div
	//Larger scales: 170 codes per div
	if(m_modelid == MODEL_SIGLENT_SDS6000A)
	{
		float volts_per_div = GetChannelVoltageRange(ch, 0) / 8;

		if(volts_per_div < 0.001)
			codes_per_div = 63.75;
		else if(volts_per_div < 0.011)
			codes_per_div = 127.5;
		else
			codes_per_div = 170;

		//Codes per div from datasheet assume 12 bit ADC resolution
		//Rescale to 8 bit for US-market SDS6000A scopes
		//TODO: remove this for Asia-market 10/12 bit models
		codes_per_div /= 16;
	}

	//SDS2000X+ and SDS5000X have 30 codes per div.
	else
		codes_per_div = 30;

	v_gain = v_gain * v_probefactor / codes_per_div;

	//in word mode, we have 256x as many codes
	if(m_highDefinition)
		v_gain /= 256;

	// Vertical offset is also scaled by the probefactor
	v_off = v_off * v_probefactor;

	// Update channel voltages and offsets based on what is in this wavedesc
	// m_channelVoltageRanges[ch] = v_gain * v_probefactor * 30 * 8;
	// m_channelOffsets[ch] = v_off;
	// m_triggerOffset = ((interval * datalen) / 2) + h_off;
	// m_triggerOffsetValid = true;

	LogTrace("\nV_Gain=%f, V_Off=%f, interval=%f, h_off=%f, h_off_frac=%f, datalen=%zu\n",
		v_gain,
		v_off,
		interval,
		h_off,
		h_off_frac,
		datalen);

	for(size_t j = 0; j < num_sequences; j++)
	{
		//Set up the capture we're going to store our data into
		auto cap = new UniformAnalogWaveform;
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
		if(m_highDefinition)
		{
			Convert16BitSamples(
				cap->m_samples.GetCpuPointer(),
				wdata + j * num_per_segment,
				v_gain,
				v_off,
				num_per_segment);
		}
		else
		{
			Convert8BitSamples(
				cap->m_samples.GetCpuPointer(),
				bdata + j * num_per_segment,
				v_gain,
				v_off,
				num_per_segment);
		}

		cap->MarkSamplesModifiedFromCpu();
		ret.push_back(cap);
	}

	return ret;
}

map<int, SparseDigitalWaveform*> SiglentSCPIOscilloscope::ProcessDigitalWaveform(string& /*data*/)
{
	map<int, SparseDigitalWaveform*> ret;

	// Digital channels not yet implemented
	return ret;

	/*

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
			LogDebug("%s: %zu samples deduplicated to %zu (%.1f %%)\n",
				m_digitalChannels[i]->GetDisplayName().c_str(),
				num_samples,
				k,
				(k * 100.0f) / num_samples);

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
	*/
}

bool SiglentSCPIOscilloscope::AcquireData()
{
	//State for this acquisition (may be more than one waveform)
	uint32_t num_sequences = 1;
	map<int, vector<WaveformBase*>> pending_waveforms;
	double start = GetTime();
	time_t ttime = 0;
	double basetime = 0;
	double h_off_frac = 0;
	vector<vector<WaveformBase*>> waveforms;
	unsigned char* pdesc = NULL;
	bool denabled = false;
	string wavetime;
	bool enabled[8] = {false};
	double* pwtime = NULL;
	char tmp[128];

	//Acquire the data (but don't parse it)

	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	start = GetTime();
	//Get the wavedescs for all channels
	unsigned int firstEnabledChannel = UINT_MAX;
	bool any_enabled = true;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			m_sampleRateValid = false;
			GetSampleRate();

			// get enabled channels
			for(unsigned int i = 0; i < m_analogChannelCount; i++)
			{
				enabled[i] = IsChannelEnabled(i);
				any_enabled |= enabled[i];
			}
			start = GetTime();
			for(unsigned int i = 0; i < m_analogChannelCount; i++)
			{
				if(enabled[i])
				{
					m_transport->SendCommand("C" + to_string(i + 1) + ":WAVEFORM? DAT2");
					// length of data is current memory depth
					m_analogWaveformDataSize[i] = ReadWaveformBlock(WAVEFORM_SIZE, m_analogWaveformData[i]);
					// This is the 0x0a0a at the end
					m_transport->ReadRawData(2, (unsigned char*)tmp);
				}
			}
			//At this point all data has been read so the scope is free to go do
			//its thing while we crunch the results.  Re-arm the trigger if not
			//in one-shot mode
			if(!m_triggerOneShot)
			{
				sendOnly("TRIG_MODE SINGLE");
				m_triggerArmed = true;
			}

			//Process analog waveforms
			waveforms.resize(m_analogChannelCount);
			for(unsigned int i = 0; i < m_analogChannelCount; i++)
			{
				std::vector<WaveformBase*> ret;
				if(m_channelsEnabled[i])
				{
					auto cap = new UniformAnalogWaveform;
					cap->m_timescale = FS_PER_SECOND / m_sampleRate;
					// no high res timer on scope ?
					cap->m_triggerPhase = h_off_frac;
					cap->m_startTimestamp = time(NULL);
					// Fixme
					cap->m_startFemtoseconds = (start - floor(start)) * FS_PER_SECOND;

					cap->Resize(m_analogWaveformDataSize[i]);
					cap->PrepareForCpuAccess();

					Convert8BitSamples(
						cap->m_samples.GetCpuPointer(),
						(int8_t*)m_analogWaveformData[i],
						m_channelVoltageRanges[i] / (8 * 25),
						m_channelOffsets[i],
						m_analogWaveformDataSize[i]);
					cap->MarkSamplesModifiedFromCpu();
					ret.push_back(cap);
				}
#if (defined(__GNUC__) && !defined(__clang__))
//WORKAROUND likely GCC bug in newer versions
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Warray-bounds"
#endif
				waveforms[i] = ret;
#if (defined(__GNUC__) && !defined(__clang__))
#pragma GCC diagnostic pop
#endif
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
			break;

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			if(!ReadWavedescs(m_wavedescs, enabled, firstEnabledChannel, any_enabled))
				return false;

			//Grab the WAVEDESC from the first enabled channel
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

			if(pdesc)
			{
				// THIS SECTION IS UNTESTED
				//Figure out when the first trigger happened.
				//Read the timestamps if we're doing segmented capture
				ttime = ExtractTimestamp(pdesc, basetime);
				if(num_sequences > 1)
					wavetime = m_transport->ReadReply();
				pwtime = reinterpret_cast<double*>(&wavetime[16]);	  //skip 16-byte SCPI header

				//QUIRK: On SDS2000X+ with firmware 1.3.9R6 and older, the SCPI length header reports the
				//sample count rather than size in bytes! Firmware 1.3.9R10 and newer reports size in bytes.
				//2000X+ HD running firmware 1.1.7.0 seems to report size in bytes.
				bool hdWorkaround = m_requireSizeWorkaround && m_highDefinition;

				//Read the data from each analog waveform
				for(unsigned int i = 0; i < m_analogChannelCount; i++)
				{
					if(enabled[i])
					{
						m_transport->SendCommand(":WAVEFORM:SOURCE C" + to_string(i + 1) + ";:WAVEFORM:DATA?");
						m_analogWaveformDataSize[i] = ReadWaveformBlock(WAVEFORM_SIZE, m_analogWaveformData[i], hdWorkaround);
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

			//At this point all data has been read so the scope is free to go do its thing while we crunch the results.
			//Re-arm the trigger if not in one-shot mode
			if(!m_triggerOneShot)
			{
				sendOnly(":TRIGGER:MODE SINGLE");
				m_triggerArmed = true;
			}

			//Process analog waveforms
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
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
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
				s[GetOscilloscopeChannel(j)] = pending_waveforms[j][i];
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
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			//sendOnly("START");
			//sendOnly("MEMORY_SIZE 7K");
			sendOnly("STOP");
			sendOnly("TRIG_MODE SINGLE");
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":TRIGGER:STOP");
			sendOnly(":TRIGGER:MODE SINGLE");	 //always do single captures, just re-trigger
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void SiglentSCPIOscilloscope::StartSingleTrigger()
{
	//LogDebug("Start single trigger\n");

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			sendOnly("STOP");
			sendOnly("TRIG_MODE SINGLE");
			break;

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":TRIGGER:STOP");
			sendOnly(":TRIGGER:MODE SINGLE");
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void SiglentSCPIOscilloscope::Stop()
{
	if(!m_triggerArmed)
		return;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			m_transport->SendCommandImmediate("STOP");
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			m_transport->SendCommandImmediate(":TRIGGER:STOP");
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	m_triggerArmed = false;
	m_triggerOneShot = true;

	//Clear out any pending data (the user doesn't want it, and we don't want stale stuff hanging around)
	ClearPendingWaveforms();
}

void SiglentSCPIOscilloscope::ForceTrigger()
{
	// Don't allow more than one force at a time
	if(m_triggerForced)
		return;

	m_triggerForced = true;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			sendOnly("TRIG_MODE SINGLE");
			if(!m_triggerArmed)
				sendOnly("TRIG_MODE SINGLE");
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":TRIGGER:MODE SINGLE");
			if(!m_triggerArmed)
				sendOnly(":TRIGGER:MODE SINGLE");
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	m_triggerArmed = true;
	this_thread::sleep_for(c_trigger_delay);
}

float SiglentSCPIOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			reply = converse("C%zu:OFST?", i + 1);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			reply = converse(":CHANNEL%zu:OFFSET?", i + 1);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	float offset;
	sscanf(reply.c_str(), "%f", &offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void SiglentSCPIOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return;

	{
		switch(m_modelid)
		{
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS1000:
			case MODEL_SIGLENT_SDS2000XE:
				sendOnly("C%zu:OFST %1.2E", i + 1, offset);
				break;
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS800X_HD:
			case MODEL_SIGLENT_SDS2000XP:
			case MODEL_SIGLENT_SDS2000X_HD:
			case MODEL_SIGLENT_SDS5000X:
			case MODEL_SIGLENT_SDS6000A:
				sendOnly(":CHANNEL%zu:OFFSET %1.2E", i + 1, offset);
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

float SiglentSCPIOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			reply = converse("C%d:VOLT_DIV?", i + 1);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			reply = converse(":CHANNEL%d:SCALE?", i + 1);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	float volts_per_div;
	sscanf(reply.c_str(), "%f", &volts_per_div);

	float v = volts_per_div * 8;	//plot is 8 divisions high
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = v;
	return v;
}

void SiglentSCPIOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	float vdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			sendOnly("C%zu:VOLT_DIV %.4f", i + 1, vdiv);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":CHANNEL%zu:SCALE %.4f", i + 1, vdiv);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleRatesNonInterleaved()
{

	const uint64_t k = 1000;
	const uint64_t m = k*k;
	const uint64_t g = k*m;

	vector<uint64_t> ret;
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			ret = {1 * k,
			       2 * k,
			       5 * k,
			       10 * k,
			       20 * k,
			       50 * k,
			       100 * k,
			       200 * k,
			       500 * k,
			       1 * m,
			       2 * m,
			       5 * m,
			       10 * m,
			       20 * m,
			       50 * m,
			       100 * m,
			       250 * m,
			       500 * m,
			       1 * g};
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
			ret = {10 * 1000,
				20 * k,
				50 * k,
				100 * k,
				200 * k,
				500 * k,
				1 * m,
				2 * m,
				5 * m,
				10 * m,
				20 * m,
				50 * m,
				100 * m,
				200 * m,
				500 * m,
				1 * g};
			break;

		case MODEL_SIGLENT_SDS5000X:
			ret = {500,
				1250,
				2500,
				5000,
				12500,
				25 * k,
				50 * k,
				125 * k,
				250 * k,
				500 * k,
				1250 * k,
				2500 * k,
				5 * m,
				12500 * k,
				25 * m,
				50 * m,
				125 * m,
				250 * m,
				500 * m,
				1250 * m,
				2500 * m};
			break;

		case MODEL_SIGLENT_SDS6000A:
			ret = {10 * k,
				20 * k,
				50 * k,
				100 * k,
				200 * k,
				500 * k,
				1 * m,
				2 * m,
				5 * m,
				10 * m,
				20 * m,
				50 * m,
				100 * m,
				200 * m,
				500 * m,
				1 * g,
				5 * g,
				10 * g};
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	return ret;
}

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleRatesInterleaved()
{
	//no interleaving on SDS6000A
	if(m_modelid == MODEL_SIGLENT_SDS6000A)
		return GetSampleRatesNonInterleaved();

	vector<uint64_t> ret = GetSampleRatesNonInterleaved();
	for(size_t i=0; i<ret.size(); i++)
		ret[i] *= 2;
	return ret;
}

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret = {};

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			// According to programming guide and datasheet
			// {7K,70K,700K,7M} for non-interleaved mode
			ret = {7 * 1000, 70 * 1000, 700 * 1000, 7 * 1000 * 1000};
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
			ret = {10 * 1000, 100 * 1000, 1000 * 1000, 10 * 1000 * 1000};
			break;

		case MODEL_SIGLENT_SDS800X_HD:
			// Memory depth varies by speed, and by 1/2/4 channels
			// Using safe (4 channel) maximum values for now..
			if(m_maxBandwidth == 200)
			{
				ret =
				{
					10 * 1000,
					100 * 1000,
					1000 * 1000,
					10 * 1000 * 1000,
					25 * 1000 * 1000
				};
			}
			else
			{
				ret =
				{
					10 * 1000,
					100 * 1000,
					1000 * 1000,
					10 * 1000 * 1000
				};
			}
			break;

		case MODEL_SIGLENT_SDS5000X:
			ret = {
					5,
					12, //Should be 12.5 
					25,
					50,
					125,
					250,
					500,
					1250,
					2500,
					5 * 1000,
					12500,
					25 * 1000,
					50 * 1000,
					125 * 1000,
					250 * 1000,
					500 * 1000,
					1250 * 1000,
					2500 * 1000,
					5 * 1000 * 1000,
					12500 * 1000,
					25 * 1000 * 1000,
					50 * 1000 * 1000,
					125 * 1000 * 1000
				  };
			break;

		case MODEL_SIGLENT_SDS6000A:

			if(m_maxBandwidth == 2000)
			{
				ret =
				{
					2500,
					5000,
					25 * 1000,
					50 * 1000,
					250 * 1000,
					500 * 1000,
					2500 * 1000L,
					5000 * 1000L,
					12500 * 1000L

					//these depths need chunked download?? TODO
					/*,
					25000 * 1000L,
					50000 * 1000L,
					125000 * 1000L,
					250000 * 1000L,
					500000 * 1000L
					*/
				};
			}
			else
			{
				ret =
				{
					1250,
					2500,
					5000,
					25 * 1000,
					50 * 1000,
					250 * 1000,
					500 * 1000,
					2500 * 1000L,
					5000 * 1000L,
					12500 * 1000L

					//these depths need chunked download?? TODO
					/*,
					25000 * 1000L,
					50000 * 1000L,
					125000 * 1000L
					*/
				};
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

vector<uint64_t> SiglentSCPIOscilloscope::GetSampleDepthsInterleaved()
{
	//no interleaving on SDS6000A 2 GHz SKU
	if( (m_modelid == MODEL_SIGLENT_SDS6000A) && (m_maxBandwidth == 2000) )
		return GetSampleDepthsNonInterleaved();

	//Only the largest memory depth changes on SDS800X HD, ignore for now..
	if(m_modelid == MODEL_SIGLENT_SDS800X_HD)
		return GetSampleDepthsNonInterleaved();

	vector<uint64_t> ret = GetSampleDepthsNonInterleaved();
	for(size_t i=0; i<ret.size(); i++)
		ret[i] *= 2;
	return ret;
}

set<SiglentSCPIOscilloscope::InterleaveConflict> SiglentSCPIOscilloscope::GetInterleaveConflicts()
{
	set<InterleaveConflict> ret;

	//All scopes normally interleave channels 1/2 and 3/4.
	//If both channels in either pair is in use, that's a problem.
	ret.emplace(InterleaveConflict(GetOscilloscopeChannel(0), GetOscilloscopeChannel(1)));
	if(m_analogChannelCount > 2)
		ret.emplace(InterleaveConflict(GetOscilloscopeChannel(2), GetOscilloscopeChannel(3)));

	return ret;
}

uint64_t SiglentSCPIOscilloscope::GetSampleRate()
{
	double f;
	if(!m_sampleRateValid)
	{
		string reply;
		switch(m_modelid)
		{
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS1000:
			case MODEL_SIGLENT_SDS2000XE:
				reply = converse("SAMPLE_RATE?");
				break;

			// --------------------------------------------------
			case MODEL_SIGLENT_SDS800X_HD:
			case MODEL_SIGLENT_SDS2000XP:
			case MODEL_SIGLENT_SDS2000X_HD:
			case MODEL_SIGLENT_SDS5000X:
			case MODEL_SIGLENT_SDS6000A:
				reply = converse(":ACQUIRE:SRATE?");
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}

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
		//:ACQUIRE:MDEPTH can sometimes return incorrect values! It returns the *cap* on memory depth,
		// not the *actual* memory depth....we don't know that until we've collected samples

		// What you see below is the only observed method that seems to reliably get the *actual* memory depth.
		string reply;
		switch(m_modelid)
		{
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS1000:
			case MODEL_SIGLENT_SDS2000XE:
				reply = converse("MEMORY_SIZE?");
				break;
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS800X_HD:
			case MODEL_SIGLENT_SDS2000XP:
			case MODEL_SIGLENT_SDS2000X_HD:
			case MODEL_SIGLENT_SDS5000X:
			case MODEL_SIGLENT_SDS6000A:
				reply = converse(":ACQUIRE:MDEPTH?");
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
		f = Unit(Unit::UNIT_SAMPLEDEPTH).ParseString(reply);
		m_memoryDepth = static_cast<int64_t>(f);
		m_memoryDepthValid = true;
	}
	return m_memoryDepth;
}

void SiglentSCPIOscilloscope::SetSampleDepth(uint64_t depth)
{
	//Need to lock the mutex when setting depth because of the quirks around needing to change trigger mode too
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	//save original sample rate (scope often changes sample rate when adjusting memory depth)
	uint64_t rate = GetSampleRate();

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			// we can not change memory size in Run/Stop mode
			sendOnly("TRIG_MODE AUTO");
			switch(depth)
			{
				case 7000:
					sendOnly("MEMORY_SIZE 7K");
					break;
				case 14000:
					sendOnly("MEMORY_SIZE 14K");
					break;
				case 70000:
					sendOnly("MEMORY_SIZE 70K");
					break;
				case 140000:
					sendOnly("MEMORY_SIZE 140K");
					break;
				case 700000:
					sendOnly("MEMORY_SIZE 700K");
					break;
				case 1400000:
					sendOnly("MEMORY_SIZE 1.4M");
					break;
				case 7000000:
					sendOnly("MEMORY_SIZE 7M");
					break;
				case 14000000:
					sendOnly("MEMORY_SIZE 14M");
					break;
				default:
					LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
			}
			if(IsTriggerArmed())
			{
				// restart trigger
				sendOnly("TRIG_MODE SINGLE");
			}
			else
			{
				// change to stop mode
				sendOnly("TRIG_MODE STOP");
			}
			m_sampleRateValid = false;
			break;

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		
			// we can not change memory size in Run/Stop mode
			sendOnly("TRIG_MODE AUTO");

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
					LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
			}

			if(IsTriggerArmed())
			{
				// restart trigger
				sendOnly("TRIG_MODE SINGLE");
			}
			else
			{
				// change to stop mode
				sendOnly("TRIG_MODE STOP");
			}
			break;

		case MODEL_SIGLENT_SDS5000X:

			// we can not change memory size in Run/Stop mode
			sendOnly("TRIG_MODE AUTO");

			switch(depth)
			{
				case 5:
					sendOnly("ACQUIRE:MDEPTH 5");
					break;
				case 12:
					sendOnly("ACQUIRE:MDEPTH 12.5");
					break;
				case 25:
					sendOnly("ACQUIRE:MDEPTH 25");
					break;
				case 50:
					sendOnly("ACQUIRE:MDEPTH 50");
					break;
				case 125:
					sendOnly("ACQUIRE:MDEPTH 125");
					break;
				case 250:
					sendOnly("ACQUIRE:MDEPTH 250");
					break;
				case 500:
					sendOnly("ACQUIRE:MDEPTH 500");
					break;
				case 1250:
					sendOnly("ACQUIRE:MDEPTH 1.25k");
					break;
				case 2500:
					sendOnly("ACQUIRE:MDEPTH 2.5k");
					break;
				case 5000:
					sendOnly("ACQUIRE:MDEPTH 5k");
					break;
				case 12500:
					sendOnly("ACQUIRE:MDEPTH 12.5k");
					break;
				case 25000:
					sendOnly("ACQUIRE:MDEPTH 25k");
					break;
				case 50000:
					sendOnly("ACQUIRE:MDEPTH 50k");
					break;
				case 125000:
					sendOnly("ACQUIRE:MDEPTH 125k");
					break;
				case 250000:
					sendOnly("ACQUIRE:MDEPTH 250k");
					break;
				case 500000:
					sendOnly("ACQUIRE:MDEPTH 500k");
					break;
				case 1250000:
					sendOnly("ACQUIRE:MDEPTH 1.25M");
					break;
				case 2500000:
					sendOnly("ACQUIRE:MDEPTH 2.5M");
					break;
				case 5000000:
					sendOnly("ACQUIRE:MDEPTH 5M");
					break;
				case 12500000:
					sendOnly("ACQUIRE:MDEPTH 12.5M");
					break;
				case 25000000:
					sendOnly("ACQUIRE:MDEPTH 25M");
					break;
				case 50000000:
					sendOnly("ACQUIRE:MDEPTH 50M");
					break;
				case 125000000:
					sendOnly("ACQUIRE:MDEPTH 125M");
					break;
				default:
					LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
			}

			if(IsTriggerArmed())
			{
				// restart trigger
				sendOnly("TRIG_MODE SINGLE");
			}
			else
			{
				// change to stop mode
				sendOnly("TRIG_MODE STOP");
			}
			break;

		case MODEL_SIGLENT_SDS6000A:

			// we can not change memory size in Run/Stop mode
			sendOnly("TRIG_MODE AUTO");

			switch(depth)
			{
				case 1250:
					sendOnly("ACQUIRE:MDEPTH 1.25k");
					break;
				case 2500:
					sendOnly("ACQUIRE:MDEPTH 2.5k");
					break;
				case 5000:
					sendOnly("ACQUIRE:MDEPTH 5k");
					break;
				case 12500:
					sendOnly("ACQUIRE:MDEPTH 12.5k");
					break;
				case 25000:
					sendOnly("ACQUIRE:MDEPTH 25k");
					break;
				case 50000:
					sendOnly("ACQUIRE:MDEPTH 50k");
					break;
				case 125000:
					sendOnly("ACQUIRE:MDEPTH 125k");
					break;
				case 250000:
					sendOnly("ACQUIRE:MDEPTH 250k");
					break;
				case 500000:
					sendOnly("ACQUIRE:MDEPTH 500k");
					break;
				case 1250000:
					sendOnly("ACQUIRE:MDEPTH 1.25M");
					break;
				case 2500000:
					sendOnly("ACQUIRE:MDEPTH 2.5M");
					break;
				case 5000000:
					sendOnly("ACQUIRE:MDEPTH 5M");
					break;
				case 12500000:
					sendOnly("ACQUIRE:MDEPTH 12.5M");
					break;
				case 25000000:
					sendOnly("ACQUIRE:MDEPTH 25M");
					break;
				case 50000000:
					sendOnly("ACQUIRE:MDEPTH 50M");
					break;
				case 62500000:
					sendOnly("ACQUIRE:MDEPTH 62.5M");
					break;
				case 125000000:
					sendOnly("ACQUIRE:MDEPTH 125M");
					break;
				case 250000000:
					sendOnly("ACQUIRE:MDEPTH 250M");
					break;
				case 500000000:
					sendOnly("ACQUIRE:MDEPTH 500M");
					break;
			}

			if(IsTriggerArmed())
			{
				// restart trigger
				sendOnly("TRIG_MODE SINGLE");
			}
			else
			{
				// change to stop mode
				sendOnly("TRIG_MODE STOP");
			}

			//Force sample rate to be correct, adjusting time/div if needed
			SetSampleRate(GetSampleRate());

			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

	m_memoryDepthValid = false;

	//restore old sample rate
	SetSampleRate(rate);
}

void SiglentSCPIOscilloscope::SetSampleRate(uint64_t rate)
{
	m_sampleRate = rate;
	m_sampleRateValid = false;

	m_memoryDepthValid = false;
	double sampletime = GetSampleDepth() / (double)rate;
	double scale = sampletime / 10;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
			sendOnly(":TIMEBASE:SCALE %1.2E", scale);
			break;

		//Timebase must be multiples of 1-2-5 so truncate any fractional component
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			{
				char tmp[128];
				snprintf(tmp, sizeof(tmp), "%1.0E", scale);
				if(tmp[0] == '3')
					tmp[0] = '2';
				sendOnly(":TIMEBASE:SCALE %s", tmp);
			}
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
	m_memoryDepthValid = false;
}

void SiglentSCPIOscilloscope::EnableTriggerOutput()
{
	LogWarning("EnableTriggerOutput not implemented\n");
}

void SiglentSCPIOscilloscope::SetUseExternalRefclk(bool /*external*/)
{
	switch(m_modelid)
	{
		//Silently ignore request on models that do not have external refclk input
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS6000A:
			break;

		case MODEL_SIGLENT_SDS5000X:
			LogWarning("SetUseExternalRefclk not implemented\n");
			break;

		default:
			LogError("Unknown scope type\n");
			break;
	}

}

void SiglentSCPIOscilloscope::SetTriggerOffset(int64_t offset)
{
	//Siglents standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			sendOnly("TRIG_DELAY %1.2E", (halfwidth - offset) * SECONDS_PER_FS);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":TIMEBASE:DELAY %1.2E", (halfwidth - offset) * SECONDS_PER_FS);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

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
		switch(m_modelid)
		{
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS1000:
			case MODEL_SIGLENT_SDS2000XE:
				reply = converse("TRIG_DELAY?");
				break;
			// --------------------------------------------------
			case MODEL_SIGLENT_SDS800X_HD:
			case MODEL_SIGLENT_SDS2000XP:
			case MODEL_SIGLENT_SDS2000X_HD:
			case MODEL_SIGLENT_SDS5000X:
			case MODEL_SIGLENT_SDS6000A:
				reply = converse(":TIMEBASE:DELAY?");
				break;
			// --------------------------------------------------
			default:
				LogError("Unknown scope type\n");
				break;
				// --------------------------------------------------
		}
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
	m_triggerOffset = halfwidth - m_triggerOffset;

	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

void SiglentSCPIOscilloscope::SetDeskewForChannel(size_t channel, int64_t skew)
{
	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			sendOnly("C%zu:SKEW %1.2E", channel + 1, skew * SECONDS_PER_FS);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			sendOnly(":CHANNEL%zu:SKEW %1.2E", channel, skew * SECONDS_PER_FS);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

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
	string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			reply = converse("C%zu:SKEW?", channel + 1);
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			reply = converse(":CHANNEL%zu:SKEW?", channel + 1);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}

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
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			// <size>:={7K,70K,700K,7M} for non-interleaved mode.
			// <size>:={14K,140K,1.4M,14M}for interleave mode.

			if((m_channelsEnabled[0] == true) && (m_channelsEnabled[1] == true))
			{
				// Channel 1 and 2
				return false;
			}
			else if((m_channelsEnabled[3] == true) && (m_channelsEnabled[4] == true))
			{
				// Channel 3 and 4
				return false;
			}
			return true;

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
			if((m_channelsEnabled[0] == true) && (m_channelsEnabled[1] == true))
			{
				// Channel 1 and 2
				return false;
			}
			else if((m_channelsEnabled[3] == true) && (m_channelsEnabled[4] == true))
			{
				// Channel 3 and 4
				return false;
			}
			return true;
		case MODEL_SIGLENT_SDS6000A:
			return false;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			return false;
			// --------------------------------------------------
	}
}

bool SiglentSCPIOscilloscope::SetInterleaving(bool /* combine*/)
{
	//Setting interleaving is not supported, it's always hardware managed
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Analog bank configuration

//NOTE: As of PG01-E11A this command is undocumented.
//Only source for this information is email discussions with Angel from the SDS2000X+ firmware engineering team
//TODO: 12 bit mode for Asia market SDS6000 series scopes

bool SiglentSCPIOscilloscope::IsADCModeConfigurable()
{
	return (m_modelid == MODEL_SIGLENT_SDS2000XP);
}

vector<string> SiglentSCPIOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> v;
	v.push_back("8 bit");
	if(m_modelid == MODEL_SIGLENT_SDS2000XP)
		v.push_back("10 bit");
	return v;
}

size_t SiglentSCPIOscilloscope::GetADCMode(size_t /*channel*/)
{
	//Only SDS2000X+ has settable ADC resolution
	if(m_modelid != MODEL_SIGLENT_SDS2000XP)
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_adcModeValid)
			return m_adcMode;
	}

	auto reply = m_transport->SendCommandQueuedWithReply("ACQ:RES?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_adcModeValid = true;
	if(reply == "10Bits")
	{
		m_adcMode = ADC_MODE_10BIT;
		m_highDefinition = true;
		m_transport->SendCommandQueued(":WAVEFORM:WIDTH WORD");
	}
	else //if(reply == "8Bits")
	{
		m_adcMode = ADC_MODE_8BIT;
		m_highDefinition = false;
		m_transport->SendCommandQueued(":WAVEFORM:WIDTH BYTE");
	}

	return m_adcMode;
}

void SiglentSCPIOscilloscope::SetADCMode(size_t /*channel*/, size_t mode)
{
	//Only SDS2000X+ has settable ADC resolution
	if(m_modelid != MODEL_SIGLENT_SDS2000XP)
		return;

	//Update cache first
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_adcMode = (ADCMode)mode;
		if(mode == ADC_MODE_8BIT)
			m_highDefinition = false;
		else
			m_highDefinition = true;
	}

	//ADC mode cannot be changed while stopped
	m_transport->SendCommandQueued("TRIG_MODE AUTO");

	//Flush command queue and delay with query
	m_transport->SendCommandQueuedWithReply("TRIG_MODE?");

	if(mode == ADC_MODE_10BIT)
	{
		m_transport->SendCommandQueued("ACQ:RES 10Bits");
	}
	else //if(mode == ADC_MODE_8BIT)
	{
		m_transport->SendCommandQueued("ACQ:RES 8Bits");
	}

	//Re-arm trigger if previously armed
	if(IsTriggerArmed())
		m_transport->SendCommandQueued("TRIG_MODE SINGLE");
	else
		m_transport->SendCommandQueued("TRIG_MODE STOP");

	//Flush command queue and delay with query
	m_transport->SendCommandQueuedWithReply("TRIG_MODE?");

	if(mode == ADC_MODE_10BIT)
	{
		m_transport->SendCommandQueued(":WAVEFORM:WIDTH WORD");
	}
	else //if(mode == ADC_MODE_8BIT)
	{
		m_transport->SendCommandQueued(":WAVEFORM:WIDTH BYTE");
	}
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
	channel -= m_analogChannelCount + 1;

	string r = converse(":DIGITAL:THRESHOLD%d?", (channel / 8) + 1).c_str();

	// Look through the threshold table to see if theres a string match, return it if so
	uint32_t i = 0;
	while((c_sds2000xp_threshold_table[i].name) &&
		  (strncmp(c_sds2000xp_threshold_table[i].name, r.c_str(), strlen(c_sds2000xp_threshold_table[i].name))))
		i++;

	if(c_sds2000xp_threshold_table[i].name)
		return c_sds2000xp_threshold_table[i].val;

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
	channel -= m_analogChannelCount + 1;

	// Search through standard thresholds to see if one matches
	uint32_t i = 0;
	while((
		(c_sds2000xp_threshold_table[i].name) && (fabsf(level - c_sds2000xp_threshold_table[i].val)) > c_thresh_thresh))
		i++;

	if(c_sds2000xp_threshold_table[i].name)
		sendOnly(":DIGITAL:THRESHOLD%d %s", (channel / 8) + 1, (c_sds2000xp_threshold_table[i].name));
	else
	{
		do
		{
			sendOnly(":DIGITAL:THRESHOLD%d CUSTOM,%1.2E", (channel / 8) + 1, level);

		} while(fabsf((GetDigitalThreshold(channel + m_analogChannelCount + 1) - level)) > 0.1f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

void SiglentSCPIOscilloscope::PullTrigger()
{
	std::string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			reply = Trim(converse("TRIG_SELECT?"));
			// <trig_type>,SR,<source>,HT,<hold_type>,HV,<hold_value1>[,HV2,<hold_value2>]
			//EDGE,SR,C1,HT,OFF
			{
				vector<string> result;
				stringstream s_stream(reply);	 //create string stream from the string
				while(s_stream.good())
				{
					string substr;
					getline(s_stream, substr, ',');	   //get first string delimited by comma
					result.push_back(substr);
				}

				if(result[0] == "GLIT")
				{
			     	// Glitch/Pulse GLIT,SR,C1,HT,P2,HV,2.00E-09s,HV2,3.00E-09s
					PullPulseWidthTrigger();
				}
				else if(result[0] == "EDGE")
				{
					PullEdgeTrigger();
				}
				else
				{
					LogWarning("Unknown trigger type \"%s\"\n", reply.c_str());
					m_trigger = NULL;
					return;
				}
				auto chan = GetOscilloscopeChannelByHwName(result[2]);
				m_trigger->SetInput(0, StreamDescriptor(chan, 0), true);
			}
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			//Figure out what kind of trigger is active.
			reply = Trim(converse(":TRIGGER:TYPE?"));
			if(reply == "DROPout")
				PullDropoutTrigger();
			else if(reply == "EDGE")
				PullEdgeTrigger();
			else if(reply == "RUNT")
				PullRuntTrigger();
			else if(reply == "SLOPe")
				PullSlewRateTrigger();
			else if(reply == "UART")
				PullUartTrigger();
			else if(reply == "INTerval")
				PullPulseWidthTrigger();
			else if(reply == "WINDow")
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
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Reads the source of a trigger from the instrument
 */
void SiglentSCPIOscilloscope::PullTriggerSource(Trigger* trig, string triggerModeName)
{
	string reply = Trim(converse(":TRIGGER:%s:SOURCE?", triggerModeName.c_str()));
	auto chan = GetOscilloscopeChannelByHwName(reply);
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

	Unit fs(Unit::UNIT_FS);

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
			break;
	    // --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			//Level
			dt->SetLevel(stof(converse(":TRIGGER:DROPOUT:LEVEL?")));
		
			//Dropout time
			dt->SetDropoutTime(fs.ParseString(converse(":TRIGGER:DROPOUT:TIME?")));

			//Edge type
			if(Trim(converse(":TRIGGER:DROPOUT:SLOPE?")) == "RISING")
				dt->SetType(DropoutTrigger::EDGE_RISING);
			else
				dt->SetType(DropoutTrigger::EDGE_FALLING);

			//Reset type
			if(Trim(converse(":TRIGGER:DROPOUT:TYPE?")) == "EDGE")
				dt->SetResetType(DropoutTrigger::RESET_OPPOSITE);
			else
				dt->SetResetType(DropoutTrigger::RESET_NONE);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
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

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			// Level
			{
				string level = converse("C1:TRIG_LEVEL?");
				et->SetLevel(stof(level));

				// Slope
				GetTriggerSlope(et, Trim(converse("C1:TRIG_SLOPE?")));
				// <trig_source>:TRIG_SLOPE <trig_slope>
			}
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			//Level
			et->SetLevel(stof(converse(":TRIGGER:EDGE:LEVEL?")));

			//TODO: OptimizeForHF (changes hysteresis for fast signals)

			//Slope
			GetTriggerSlope(et, Trim(converse(":TRIGGER:EDGE:SLOPE?")));
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
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
	Unit fs(Unit::UNIT_FS);

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:

			// Level
			pt->SetLevel(stof(converse("C1:TRIG_LEVEL?"))); //,pt->GetInput(0).m_channel->GetHwname().c_str())));

			// Slope
			GetTriggerSlope(pt, Trim(converse("C1:TRIG_SLOPE?")));
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:

			//Level
			pt->SetLevel(stof(converse(":TRIGGER:INTERVAL:LEVEL?")));

			//Condition
			pt->SetCondition(GetCondition(converse(":TRIGGER:INTERVAL:LIMIT?")));

			//Min range
			pt->SetLowerBound(fs.ParseString(converse(":TRIGGER:INTERVAL:TLOWER?")));

			//Max range
			pt->SetUpperBound(fs.ParseString(converse(":TRIGGER:INTERVAL:TUPPER?")));

			//Slope
			GetTriggerSlope(pt, Trim(converse(":TRIGGER:INTERVAL:SLOPE?")));
			break;

	    // --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// ----
	}
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

	Unit v(Unit::UNIT_VOLTS);
	Unit fs(Unit::UNIT_FS);
	string reply;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:

			//Lower bound
			rt->SetLowerBound(v.ParseString(converse(":TRIGGER:RUNT:LLEVEL?")));

			//Upper bound
			rt->SetUpperBound(v.ParseString(converse(":TRIGGER:RUNT:HLEVEL?")));
            
			//Lower bound
			rt->SetLowerInterval(fs.ParseString(converse(":TRIGGER:RUNT:TLOWER?")));

			//Upper interval
			rt->SetUpperInterval(fs.ParseString(converse(":TRIGGER:RUNT:TUPPER?")));

			//Slope
			reply = Trim(converse(":TRIGGER:RUNT:POLARITY?"));
			if(reply == "POSitive")
				rt->SetSlope(RuntTrigger::EDGE_RISING);
			else if(reply == "NEGative")
				rt->SetSlope(RuntTrigger::EDGE_FALLING);

			//Condition
			rt->SetCondition(GetCondition(converse(":TRIGGER:RUNT:LIMIT?")));
			break;
			// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
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

	Unit v(Unit::UNIT_VOLTS);
	Unit fs(Unit::UNIT_FS);
	string reply ;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:

			//Lower bound
	        st->SetLowerBound(v.ParseString(converse(":TRIGGER:SLOPE:LLEVEL?")));

			//Upper bound
			st->SetUpperBound(v.ParseString(converse(":TRIGGER:SLOPE:HLEVEL?")));

	        //Lower interval
			st->SetLowerInterval(fs.ParseString(converse(":TRIGGER:SLOPE:TLOWER?")));

			//Upper interval
			st->SetUpperInterval(fs.ParseString(converse(":TRIGGER:SLOPE:TUPPER?")));

			//Slope
			reply = Trim(converse("TRIGGER:SLOPE:SLOPE?"));
			if(reply == "RISing")
				st->SetSlope(SlewRateTrigger::EDGE_RISING);
			else if(reply == "FALLing")
				st->SetSlope(SlewRateTrigger::EDGE_FALLING);
			else if(reply == "ALTernate")
				st->SetSlope(SlewRateTrigger::EDGE_ANY);

			//Condition
			st->SetCondition(GetCondition(converse("TRIGGER:SLOPE:LIMIT?")));
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
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

    string reply;
	string p1;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:

			//Bit rate
			ut->SetBitRate(stoi(converse(":TRIGGER:UART:BAUD?")));

			//Level
			ut->SetLevel(stof(converse(":TRIGGER:UART:LIMIT?")));

			//Parity
			reply = Trim(converse(":TRIGGER:UART:PARITY?"));
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
			p1 = Trim(converse(":TRIGGER:UART:DATA?"));
			ut->SetPatterns(p1, "", true);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
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
    
	Unit v(Unit::UNIT_VOLTS);

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
			break;
	    // --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:

			//Lower bound
			wt->SetLowerBound(v.ParseString(converse(":TRIGGER:WINDOW:LLEVEL?")));

			//Upper bound
			wt->SetUpperBound(v.ParseString(converse(":TRIGGER:WINDOW:HLEVEL?")));
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Processes the slope for an edge or edge-derived trigger
 */
void SiglentSCPIOscilloscope::GetTriggerSlope(EdgeTrigger* trig, string reply)

{
	reply = Trim(reply);

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			if(reply == "POS")
				trig->SetType(EdgeTrigger::EDGE_RISING);
			else if(reply == "NEG")
				trig->SetType(EdgeTrigger::EDGE_FALLING);
			else if(reply == "WINDOW")
				trig->SetType(EdgeTrigger::EDGE_ANY);
			else
				LogWarning("SDS1000:Unknown trigger slope %s\n", reply.c_str());
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			if(reply == "RISing")
				trig->SetType(EdgeTrigger::EDGE_RISING);
			else if(reply == "FALLing")
				trig->SetType(EdgeTrigger::EDGE_FALLING);
			else if(reply == "ALTernate")
				trig->SetType(EdgeTrigger::EDGE_ANY);
			else
				LogWarning("Unknown trigger slope %s\n", reply.c_str());
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Parses a trigger condition
 */
Trigger::Condition SiglentSCPIOscilloscope::GetCondition(string reply)
{
	reply = Trim(reply);

	if(reply == "LESSthan")
		return Trigger::CONDITION_LESS;
	else if(reply == "GREATerthan")
		return Trigger::CONDITION_GREATER;
	else if(reply == "INNer")
		return Trigger::CONDITION_BETWEEN;
	else if(reply == "OUTer")
		return Trigger::CONDITION_NOT_BETWEEN;

	//unknown
	LogWarning("Unknown trigger condition [%s]\n", reply.c_str());
	return Trigger::CONDITION_LESS;
}

void SiglentSCPIOscilloscope::PushTrigger()
{
	auto dt = dynamic_cast<DropoutTrigger*>(m_trigger);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto rt = dynamic_cast<RuntTrigger*>(m_trigger);
	auto st = dynamic_cast<SlewRateTrigger*>(m_trigger);
	auto ut = dynamic_cast<UartTrigger*>(m_trigger);
	auto wt = dynamic_cast<WindowTrigger*>(m_trigger);
	switch(m_modelid)
	{
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			if(dt)
			{
				PushDropoutTrigger(dt);
			}
			else if(pt)
			{
				PushPulseWidthTrigger(pt);
			}
			else if(rt)
			{
				PushRuntTrigger(rt);
			}
			else if(st)
			{
				PushSlewRateTrigger(st);
			}
			else if(ut)
			{
				PushUartTrigger(ut);
			}
			else if(wt)
			{
				PushWindowTrigger(wt);
			}

			// TODO: Add in PULSE, VIDEO, PATTERN, QUALITFIED, SPI, IIC, CAN, LIN, FLEXRAY and CANFD Triggers

			else if(et)	   //must be last
			{				
				// set default
				sendOnly("TRSE EDGE,SR,%s,HT,OFF", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				PushEdgeTrigger(et, "EDGE");
			}
			else
				LogWarning("Unknown trigger type (not an edge)\n");
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			if(dt)
			{
				sendOnly(":TRIGGER:TYPE DROPOUT");
				sendOnly(":TRIGGER:DROPOUT:SOURCE %s", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				PushDropoutTrigger(dt);
			}
			else if(pt)
			{
				sendOnly(":TRIGGER:TYPE INTERVAL");
				sendOnly(":TRIGGER:INTERVAL:SOURCE %s", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				PushPulseWidthTrigger(pt);
			}
			else if(rt)
			{
				sendOnly(":TRIGGER:TYPE RUNT");
				sendOnly(":TRIGGER:RUNT:SOURCE %s", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				PushRuntTrigger(rt);
			}
			else if(st)
			{
				sendOnly(":TRIGGER:TYPE SLOPE");
				sendOnly(":TRIGGER:SLOPE:SOURCE %s", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				PushSlewRateTrigger(st);
			}
			else if(ut)
			{
				sendOnly(":TRIGGER:TYPE UART");
				// TODO: Validate these trigger allocations
				sendOnly(":TRIGGER:UART:RXSOURCE %s", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				sendOnly(":TRIGGER:UART:TXSOURCE %s", m_trigger->GetInput(1).m_channel->GetHwname().c_str());
				PushUartTrigger(ut);
			}
			else if(wt)
			{
				sendOnly(":TRIGGER:TYPE WINDOW");
				sendOnly(":TRIGGER:WINDOW:SOURCE %s", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				PushWindowTrigger(wt);
			}

			// TODO: Add in PULSE, VIDEO, PATTERN, QUALITFIED, SPI, IIC, CAN, LIN, FLEXRAY and CANFD Triggers

			else if(et)	   //must be last
			{
				sendOnly(":TRIGGER:TYPE EDGE");
				sendOnly(":TRIGGER:EDGE:SOURCE %s", m_trigger->GetInput(0).m_channel->GetHwname().c_str());
				PushEdgeTrigger(et, "EDGE");
			}

			else
				LogWarning("Unknown trigger type (not an edge)\n");
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Pushes settings for a dropout trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
{
	
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
           //TODO
		   break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			PushFloat(":TRIGGER:DROPOUT:LEVEL", trig->GetLevel());
			PushFloat(":TRIGGER:DROPOUT:TIME", trig->GetDropoutTime() * SECONDS_PER_FS);
			sendOnly(":TRIGGER:DROPOUT:SLOPE %s", (trig->GetType() == DropoutTrigger::EDGE_RISING) ? "RISING" : "FALLING");
			sendOnly(":TRIGGER:DROPOUT:TYPE %s", (trig->GetResetType() == DropoutTrigger::RESET_OPPOSITE) ? "EDGE" : "STATE");
		    break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushEdgeTrigger(EdgeTrigger* trig, const std::string trigType)
{
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			{
				auto chan = trig->GetInput(0).m_channel;
				if(chan == NULL)
				{
					LogError("Trigger input 0 has null channel (probable bug in SiglentSCPIOscilloscope::PullTrigger())\n");
					return;
				}
				string source = chan->GetHwname();

				switch(trig->GetType())
				{
					case EdgeTrigger::EDGE_RISING:
						sendOnly("%s:TRIG_SLOPE POS", source.c_str());
						break;

					case EdgeTrigger::EDGE_FALLING:
						sendOnly("%s:TRIG_SLOPE NEG", source.c_str());
						break;

					case EdgeTrigger::EDGE_ANY:
						sendOnly("%s:TRIG_SLOPE WINDOW", source.c_str());
						break;

					default:
						LogWarning("Invalid trigger type %d\n", trig->GetType());
						break;
				}

				//Level
				sendOnly("%s:TRIG_LEVEL %1.2E", source.c_str(), trig->GetLevel());
				break;
			}

		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
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
			sendOnly(":TRIGGER:%s:LEVEL %1.2E", trigType.c_str(), trig->GetLevel());
			break;

		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			PushEdgeTrigger(trig, "INTERVAL");
			PushCondition(":TRIGGER:INTERVAL", trig->GetCondition());
			PushFloat(":TRIGGER:INTERVAL:TUPPER", trig->GetUpperBound() * SECONDS_PER_FS);
			PushFloat(":TRIGGER:INTERVAL:TLOWER", trig->GetLowerBound() * SECONDS_PER_FS);
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Pushes settings for a runt trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushRuntTrigger(RuntTrigger* trig)
{
	
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			PushCondition(":TRIGGER:RUNT", trig->GetCondition());
			PushFloat(":TRIGGER:RUNT:TUPPER", trig->GetUpperInterval() * SECONDS_PER_FS);
			PushFloat(":TRIGGER:RUNT:TLOWER", trig->GetLowerInterval() * SECONDS_PER_FS);
			PushFloat(":TRIGGER:RUNT:LLEVEL", trig->GetLowerBound());
			PushFloat(":TRIGGER:RUNT:HLEVEL", trig->GetUpperBound());

			sendOnly(":TRIGGER:RUNT:POLARITY %s", (trig->GetSlope() == RuntTrigger::EDGE_RISING) ? "POSITIVE" : "NEGATIVE");
		    break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}		
}

/**
	@brief Pushes settings for a slew rate trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
{
	PushCondition(":TRIGGER:SLOPE", trig->GetCondition());
	PushFloat(":TRIGGER:SLOPE:TUPPER", trig->GetUpperInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:SLOPE:TLOWER", trig->GetLowerInterval() * SECONDS_PER_FS);
	PushFloat(":TRIGGER:SLOPE:HLEVEL", trig->GetUpperBound());
	PushFloat(":TRIGGER:SLOPE:LLEVEL", trig->GetLowerBound());

	sendOnly(":TRIGGER:SLOPE:SLOPE %s",
		(trig->GetSlope() == SlewRateTrigger::EDGE_RISING)	? "RISING" :
		(trig->GetSlope() == SlewRateTrigger::EDGE_FALLING) ? "FALLING" :
																"ALTERNATE");
}

/**
	@brief Pushes settings for a UART trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushUartTrigger(UartTrigger* trig)
{
    float nstop;
	string pattern1;

	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			//TODO
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
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
			pattern1 = trig->GetPattern1();
			sendOnly(":TRIGGER:UART:DLENGTH \"%d\"", (int)pattern1.length() / 8);

			PushCondition(":TRIGGER:UART", trig->GetCondition());

			//Polarity
			sendOnly(":TRIGGER:UART:IDLE %s", (trig->GetPolarity() == UartTrigger::IDLE_HIGH) ? "HIGH" : "LOW");

		    nstop = trig->GetStopBits();
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
			// --------------------------------------------------
			break;
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Pushes settings for a window trigger to the instrument
 */
void SiglentSCPIOscilloscope::PushWindowTrigger(WindowTrigger* trig)
{
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
            //TODO
		    break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			PushFloat(":TRIGGER:WINDOW:LLEVEL", trig->GetLowerBound());
			PushFloat(":TRIGGER:WINDOW:HLEVEL", trig->GetUpperBound());
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
}

/**
	@brief Pushes settings for a trigger condition under a .Condition field
 */
void SiglentSCPIOscilloscope::PushCondition(const string& path, Trigger::Condition cond)
{
	switch(cond)
	{
		case Trigger::CONDITION_LESS:
			sendOnly("%s:LIMIT LESSTHAN", path.c_str());
			break;

		case Trigger::CONDITION_GREATER:
			sendOnly("%s:LIMIT GREATERTHAN", path.c_str());
			break;

		case Trigger::CONDITION_BETWEEN:
			sendOnly("%s:LIMIT INNER", path.c_str());
			break;

		case Trigger::CONDITION_NOT_BETWEEN:
			sendOnly("%s:LIMIT OUTER", path.c_str());
			break;

		//Other values are not legal here, it seems
		default:
			break;
	}
}

void SiglentSCPIOscilloscope::PushFloat(string path, float f)
{
	sendOnly("%s %1.2E", path.c_str(), f);
}

vector<string> SiglentSCPIOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	switch(m_modelid)
	{
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS1000:
		case MODEL_SIGLENT_SDS2000XE:
			ret.push_back(EdgeTrigger::GetTriggerName());
			ret.push_back(PulseWidthTrigger::GetTriggerName());
			//TODO add more
			break;
		// --------------------------------------------------
		case MODEL_SIGLENT_SDS800X_HD:
		case MODEL_SIGLENT_SDS2000XP:
		case MODEL_SIGLENT_SDS2000X_HD:
		case MODEL_SIGLENT_SDS5000X:
		case MODEL_SIGLENT_SDS6000A:
			ret.push_back(DropoutTrigger::GetTriggerName());
			ret.push_back(EdgeTrigger::GetTriggerName());
			ret.push_back(PulseWidthTrigger::GetTriggerName());
			ret.push_back(RuntTrigger::GetTriggerName());
			ret.push_back(SlewRateTrigger::GetTriggerName());
			if(m_hasUartTrigger)
				ret.push_back(UartTrigger::GetTriggerName());
			ret.push_back(WindowTrigger::GetTriggerName());
			break;
		// --------------------------------------------------
		default:
			LogError("Unknown scope type\n");
			break;
			// --------------------------------------------------
	}
	// TODO: Add in PULSE, VIDEO, PATTERN, QUALITFIED, SPI, IIC, CAN, LIN, FLEXRAY and CANFD Triggers
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator mode

//Per docs, this is almost the same API as the SDG series generators.
//But the SAG102I and integrated generator have only a single output.
//This code can likely be ported to work with SDG* fairly easily, though.

vector<FunctionGenerator::WaveShape> SiglentSCPIOscilloscope::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(SHAPE_SINE);
	ret.push_back(SHAPE_SQUARE);
	ret.push_back(SHAPE_NOISE);

	//Docs say this is supported, but doesn't seem to work on SDS2104X+
	//Might be SDG only?
	//ret.push_back(SHAPE_PRBS_NONSTANDARD);

	ret.push_back(SHAPE_DC);
	ret.push_back(SHAPE_STAIRCASE_UP);
	ret.push_back(SHAPE_STAIRCASE_DOWN);
	ret.push_back(SHAPE_STAIRCASE_UP_DOWN);
	ret.push_back(SHAPE_PULSE);

	//Docs say this is supported, but doesn't seem to work on SDS2104X+
	//Might be SDG only?
	//ret.push_back(SHAPE_NEGATIVE_PULSE);

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

bool SiglentSCPIOscilloscope::GetFunctionChannelActive(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgEnabled.find(chan) != m_awgEnabled.end())
			return m_awgEnabled[chan];
	}

	auto reply = m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":OUTP?", false);

	//Crack result
	//Note that both enable/disable and impedance are in the same command, so we get the other for free
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(reply.find("OFF") != string::npos)
			m_awgEnabled[chan] = false;
		else
			m_awgEnabled[chan] = true;

		if(reply.find("50"))
			m_awgImpedance[chan] = IMPEDANCE_50_OHM;
		else
			m_awgImpedance[chan] = IMPEDANCE_HIGH_Z;

		return m_awgEnabled[chan];
	}

}

void SiglentSCPIOscilloscope::SetFunctionChannelActive(int chan, bool on)
{
	string state;
	if(on)
		state = "ON";
	else
		state = "OFF";

	//Have to do this first, since it touches m_awgEnabled too
	string imp;
	if(GetFunctionChannelOutputImpedance(chan) == IMPEDANCE_50_OHM)
		imp = "50";
	else
		imp = "HZ";

	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP " + state + ",LOAD," + imp);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgEnabled[chan] = on;
}

float SiglentSCPIOscilloscope::GetFunctionChannelDutyCycle(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgDutyCycle.find(chan) != m_awgDutyCycle.end())
			return m_awgDutyCycle[chan];
	}

	//Get lots of config settings from the hardware, then return newly updated cache entry
	GetFunctionChannelShape(chan);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_awgDutyCycle[chan];
}

void SiglentSCPIOscilloscope::SetFunctionChannelDutyCycle(int chan, float duty)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV DUTY," + to_string(round(duty * 100)));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgDutyCycle[chan] = duty;
}

float SiglentSCPIOscilloscope::GetFunctionChannelAmplitude(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgRange.find(chan) != m_awgRange.end())
			return m_awgRange[chan];
	}

	//Get lots of config settings from the hardware, then return newly updated cache entry
	GetFunctionChannelShape(chan);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_awgRange[chan];
}

void SiglentSCPIOscilloscope::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV AMP," + to_string(amplitude));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgRange[chan] = amplitude;
}

float SiglentSCPIOscilloscope::GetFunctionChannelOffset(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgOffset.find(chan) != m_awgOffset.end())
			return m_awgOffset[chan];
	}

	//Get lots of config settings from the hardware, then return newly updated cache entry
	GetFunctionChannelShape(chan);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_awgOffset[chan];
}

void SiglentSCPIOscilloscope::SetFunctionChannelOffset(int chan, float offset)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV OFST," + to_string(offset));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgOffset[chan] = offset;
}

float SiglentSCPIOscilloscope::GetFunctionChannelFrequency(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgFrequency.find(chan) != m_awgFrequency.end())
			return m_awgFrequency[chan];
	}

	//Get lots of config settings from the hardware, then return newly updated cache entry
	GetFunctionChannelShape(chan);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_awgFrequency[chan];
}

void SiglentSCPIOscilloscope::SetFunctionChannelFrequency(int chan, float hz)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV FRQ," + to_string(hz));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgFrequency[chan] = hz;
}

/**
	@brief Parses a name-value set expressed as pairs of comma separated values

	Expected format: COMMAND? Name1, Value1, Name2, Value2

	If forwardMap is true, returns name -> value. If false, returns value -> name.
 */
map<string, string> SiglentSCPIOscilloscope::ParseCommaSeparatedNameValueList(string str, bool forwardMap)
{
	str += ',';
	size_t ispace = str.find(' ');
	string tmpName;
	string tmpVal;
	bool firstHalf = true;
	map<string, string> ret;
	for(size_t i=ispace+1; i<str.length(); i++)
	{
		if(str[i] == ',')
		{
			//Done with name
			if(firstHalf)
				firstHalf = false;

			//Done with value
			else
			{
				firstHalf = true;

				if(forwardMap)
					ret[tmpName] = tmpVal;
				else
					ret[tmpVal] = tmpName;

				tmpName = "";
				tmpVal = "";
			}
		}

		//ignore spaces, some commands have them and others don't - doesn't seem to matter
		else if(isspace(str[i]))
			continue;

		else if(firstHalf)
			tmpName += str[i];
		else
			tmpVal += str[i];
	}
	return ret;
}

FunctionGenerator::WaveShape SiglentSCPIOscilloscope::GetFunctionChannelShape(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgShape.find(chan) != m_awgShape.end())
			return m_awgShape[chan];
	}

	//Query the basic wave parameters
	auto reply = m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?", false);
	auto areply = m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":ARWV?", false);

	//Crack the replies
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		auto bswv = ParseCommaSeparatedNameValueList(reply);

		//Some of the fields  are redundant, we don't care about all of them.

		Unit volts(Unit::UNIT_VOLTS);
		m_awgRange[chan] = volts.ParseString(bswv["AMP"]);
		m_awgOffset[chan] = volts.ParseString(bswv["OFST"]);

		Unit hz(Unit::UNIT_HZ);
		m_awgFrequency[chan] = hz.ParseString(bswv["FRQ"]);

		Unit percent(Unit::UNIT_PERCENT);
		m_awgDutyCycle[chan] = percent.ParseString(bswv["DUTY"]);

		//TODO: RISE/FALL seems only supported on SDGs, not scope integrated generator

		//TODO: PHSE is phase (not relevant for single channel integrated func gens, but will matter when
		//we support multichannel SDGs

		auto shape = bswv["WVTP"];
		if(shape == "SINE")
			m_awgShape[chan] = FunctionGenerator::SHAPE_SINE;
		else if(shape == "SQUARE")
			m_awgShape[chan] = FunctionGenerator::SHAPE_SQUARE;
		else if(shape == "RAMP")
		{
			LogWarning("wave type RAMP unimplemented\n");
		}
		else if(shape == "PULSE")
			m_awgShape[chan] = FunctionGenerator::SHAPE_PULSE;
		else if(shape == "NOISE")
			m_awgShape[chan] = FunctionGenerator::SHAPE_NOISE;
		else if(shape == "DC")
			m_awgShape[chan] = FunctionGenerator::SHAPE_DC;
		else if(shape == "PRBS")
		{
			//TODO: LENGTH if type is PRBS?
			//Might only be supported on SDGs
			m_awgShape[chan] = FunctionGenerator::SHAPE_PRBS_NONSTANDARD;
		}
		else if(shape == "IQ")
		{
			//TODO
			LogWarning("wave type IQ unimplemented\n");
		}
		else if(shape == "ARB")
		{
			string name = areply.substr(areply.find("NAME,") + 5);

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
				LogWarning("Arb shape %s unimplemented\n", name.c_str());
		}
		else
			LogWarning("wave type %s unimplemented\n", shape.c_str());

		return m_awgShape[chan];
	}
}

void SiglentSCPIOscilloscope::SetFunctionChannelShape(int chan, FunctionGenerator::WaveShape shape)
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
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV WVTP," + basicType);
	if(basicType == "ARB")
	{
		//Returns map of memory slots ("M10") to waveform names
		//Mapping is explicitly not stable, so we have to check for each instrument
		//(but can be cached for a given session)
		auto stl = m_transport->SendCommandQueuedWithReply("STL?");
		auto arbmap = ParseCommaSeparatedNameValueList(stl, false);

		m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":ARWV INDEX," + arbmap[arbType].substr(1));
	}

	//Update cache
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgShape[chan] = shape;
}

bool SiglentSCPIOscilloscope::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return false;
}

FunctionGenerator::OutputImpedance SiglentSCPIOscilloscope::GetFunctionChannelOutputImpedance(int chan)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_awgImpedance.find(chan) != m_awgImpedance.end())
			return m_awgImpedance[chan];
	}

	//Get output enable status and impedance from the hardware, then return newly updated cache entry
	GetFunctionChannelActive(chan);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_awgImpedance[chan];
}

void SiglentSCPIOscilloscope::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	//Have to do this first, since it touches m_awgImpedance
	string state;
	if(GetFunctionChannelActive(chan))
		state = "ON";
	else
		state = "OFF";

	string imp;
	if(z == IMPEDANCE_50_OHM)
		imp = "50";
	else
		imp = "HZ";

	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP " + state + ",LOAD," + imp);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_awgImpedance[chan] = z;
}

/**
	@brief Forces 16-bit transfer mode on/off when for HD models
 */
void SiglentSCPIOscilloscope::ForceHDMode(bool mode)
{
	if((m_modelid == MODEL_SIGLENT_SDS800X_HD || m_modelid == MODEL_SIGLENT_SDS2000X_HD) && mode != m_highDefinition)
	{
		m_highDefinition = mode;
		sendOnly(":WAVEFORM:WIDTH %s", m_highDefinition ? "WORD" : "BYTE");
	}
}
