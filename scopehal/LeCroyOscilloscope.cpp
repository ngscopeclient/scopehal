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
#include "LeCroyOscilloscope.h"
#include "base64.h"
#include <locale>
#include <immintrin.h>
#include <omp.h>
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyOscilloscope::LeCroyOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_hasFastSampleRate(false)
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

void LeCroyOscilloscope::SharedCtorInit()
{
	m_digitalChannelCount = 0;

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

	//Desired format for waveform data
	//Only use increased bit depth if the scope actually puts content there!
	if(m_highDefinition)
		m_transport->SendCommand("COMM_FORMAT DEF9,WORD,BIN");
	else
		m_transport->SendCommand("COMM_FORMAT DEF9,BYTE,BIN");

	//Always use "max memory" config for setting sample depth
	m_transport->SendCommand("VBS 'app.Acquisition.Horizontal.Maximize=\"SetMaximumMemory\"'");

	//If interleaving, disable the extra channels
	if(IsInterleaving())
	{
		m_channelsEnabled[0] = false;
		m_channelsEnabled[3] = false;
	}

	//Clear the state-change register to we get rid of any history we don't care about
	PollTrigger();
}

void LeCroyOscilloscope::IdentifyHardware()
{
	//Turn off headers (complicate parsing and add fluff to the packets)
	m_transport->SendCommand("CHDR OFF");

	//Ask for the ID
	m_transport->SendCommand("*IDN?");
	string reply = m_transport->ReadReply();
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

	if(m_model.find("WS3") == 0)
		m_modelid = MODEL_WAVESURFER_3K;
	else if(m_model.find("HDO9") == 0)
		m_modelid = MODEL_HDO_9K;
	else if(m_model.find("DDA5") == 0)
		m_modelid = MODEL_DDA_5K;
	else if(m_model.find("WAVERUNNER8") == 0)
		m_modelid = MODEL_WAVERUNNER_8K;
	else if(m_model.find("SDA3") == 0)
		m_modelid = MODEL_SDA_3K;
	else if (m_vendor.compare("SIGLENT") == 0)
	{
		// TODO: if LeCroy and Siglent classes get split, then this should obviously
		// move to the Siglent class.
		if (m_model.compare(0, 4, "SDS2") == 0 && m_model.back() == 'X')
			m_modelid = MODEL_SIGLENT_SDS2000X;
	}

	//TODO: better way of doing this?
	if(m_model.find("HD") != string::npos)
		m_highDefinition = true;
}

void LeCroyOscilloscope::DetectOptions()
{
	m_transport->SendCommand("*OPT?");
	string reply = m_transport->ReadReply();
	if(reply.length() > 3)
	{
		//Read options until we hit a null
		vector<string> options;
		string opt;
		for(unsigned int i=0; i<reply.length(); i++)
		{
			if(reply[i] == 0)
			{
				options.push_back(opt);
				break;
			}

			else if(reply[i] == ',')
			{
				options.push_back(opt);
				opt = "";
			}

			//skip newlines
			else if(reply[i] == '\n')
				continue;

			else
				opt += reply[i];
		}
		if(opt != "")
			options.push_back(opt);

		//Print out the option list and do processing for each
		LogDebug("Installed options:\n");
		if(options.empty())
			LogDebug("* None\n");
		for(auto o : options)
		{
			//If we have an LA module installed, add the digital channels
			if( (o == "MSXX") && !m_hasLA)
			{
				LogDebug("* MSXX (logic analyzer)\n");
				AddDigitalChannels(16);
			}

			//If we have the voltmeter installed, make a note of that
			else if(o == "DVM")
			{
				m_hasDVM = true;
				LogDebug("* DVM (digital voltmeter / frequency counter)\n");

				SetMeterAutoRange(false);
			}

			//If we have the function generator installed, remember that
			else if(o == "AFG")
			{
				m_hasFunctionGen = true;
				LogDebug("* AFG (function generator)\n");
			}

			//Look for M option (extra sample rate and memory)
			else if(o == "-M")
			{
				m_hasFastSampleRate = true;
				LogDebug("* -M (extra sample rate and memory)\n");
			}

			//Ignore protocol decodes, we do those ourselves
			else if( (o == "I2C") || (o == "UART") || (o == "SPI") )
			{
				LogDebug("* %s (protocol decode, ignoring)\n", o.c_str());
			}

			//Ignore UI options
			else if(o == "XWEB")
			{
				LogDebug("* %s (UI option, ignoring)\n", o.c_str());
			}

			//No idea what it is
			else
				LogDebug("* %s (not yet implemented)\n", o.c_str());
		}
	}

	//If we don't have a code for the LA software option, but are a -MS scope, add the LA
	if(!m_hasLA && (m_model.find("-MS") != string::npos))
		AddDigitalChannels(16);
}

/**
	@brief Creates digital channels for the oscilloscope
 */
void LeCroyOscilloscope::AddDigitalChannels(unsigned int count)
{
	m_hasLA = true;
	LogIndenter li;

	m_digitalChannelCount = count;

	char chn[32];
	for(unsigned int i=0; i<count; i++)
	{
		snprintf(chn, sizeof(chn), "Digital%d", i);
		auto chan = new OscilloscopeChannel(
			this,
			chn,
			OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
			GetDefaultChannelColor(m_channels.size()),
			1,
			m_channels.size(),
			true);
		m_channels.push_back(chan);
		m_digitalChannels.push_back(chan);
	}

	//Set the threshold to "user defined" vs using a canned family
	m_transport->SendCommand("VBS? 'app.LogicAnalyzer.MSxxLogicFamily0 = \"USERDEFINED\" '");
	m_transport->SendCommand("VBS? 'app.LogicAnalyzer.MSxxLogicFamily1 = \"USERDEFINED\" '");
}

/**
	@brief Figures out how many analog channels we have, and add them to the device

	If you're lucky, the last digit of the model number will be the number of channels (HDO9204)

	But, since we can't have nice things, theres are plenty of exceptions. Known formats so far:
	* WAVERUNNER8104-MS has 4 channels (plus 16 digital)
	* DDA5005 / DDA5005A have 4 channels
	* SDA3010 have 4 channels
 */
void LeCroyOscilloscope::DetectAnalogChannels()
{
	//General model format is family, number, suffix. Not all are always present.
	//Trim off alphabetic characters from the start of the model number
	size_t pos;
	for(pos=0; pos < m_model.length(); pos++)
	{
		if(isalpha(m_model[pos]))
			continue;
		else if(isdigit(m_model[pos]))
			break;
		else
		{
			LogError("Unrecognized character (not alphanumeric) in model number %s\n", m_model.c_str());
			return;
		}
	}

	//Now we should be able to read the model number
	int modelNum = atoi(m_model.c_str() + pos);

	//Last digit of the model number is normally the number of channels (WAVESURFER3022, HDO8108)
	int nchans = modelNum % 10;

	//DDA5005 and similar have 4 channels despite a model number ending in 5
	//SDA3010 have 4 channels despite a model number ending in 0
	if(m_modelid == MODEL_DDA_5K || m_modelid == MODEL_SDA_3K)
		nchans = 4;

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("C1");
		chname[1] += i;

		//Color the channels based on LeCroy's standard color sequence (yellow-pink-cyan-green)
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#ffff80";
				break;

			case 1:
				color = "#ff8080";
				break;

			case 2:
				color = "#80ffff";
				break;

			case 3:
				color = "#80ff80";
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
}

LeCroyOscilloscope::~LeCroyOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device information

string LeCroyOscilloscope::GetDriverNameInternal()
{
	return "lecroy";
}

OscilloscopeChannel* LeCroyOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

void LeCroyOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if(m_trigger)
		delete m_trigger;
	m_trigger = NULL;

	m_channelVoltageRanges.clear();
	m_channelOffsets.clear();
	m_channelsEnabled.clear();
	m_channelDeskew.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;
	m_interleavingValid = false;
}

/**
	@brief See what measurement capabilities we have
 */
unsigned int LeCroyOscilloscope::GetMeasurementTypes()
{
	unsigned int type = 0;
	if(m_hasDVM)
	{
		type |= DC_VOLTAGE;
		type |= DC_RMS_AMPLITUDE;
		type |= AC_RMS_AMPLITUDE;
		type |= FREQUENCY;
	}
	return type;
}

/**
	@brief See what features we have
 */
unsigned int LeCroyOscilloscope::GetInstrumentTypes()
{
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasDVM)
		type |= INST_DMM;
	if(m_hasFunctionGen)
		type |= INST_FUNCTION;
	return type;
}

string LeCroyOscilloscope::GetName()
{
	return m_model;
}

string LeCroyOscilloscope::GetVendor()
{
	return m_vendor;
}

string LeCroyOscilloscope::GetSerial()
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool LeCroyOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//Disable end channels if interleaving
	if(m_interleaving)
	{
		if( (i == 0) || (i == 3) )
			return false;
	}

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
		string cmd = m_channels[i]->GetHwname() + ":TRACE?";
		m_transport->SendCommand(cmd);
		string reply = m_transport->ReadReply();
		if(reply.find("OFF") == 0)	//may have a trailing newline, ignore that
			m_channelsEnabled[i] = false;
		else
			m_channelsEnabled[i] = true;
	}

	//Digital
	else
	{
		//See if the channel is on
		m_transport->SendCommand(string("VBS? 'return = app.LogicAnalyzer.Digital1.") + m_channels[i]->GetHwname() + "'");
		string str = m_transport->ReadReply();
		if(str == "0")
			m_channelsEnabled[i] = false;
		else
			m_channelsEnabled[i] = true;
	}

	return m_channelsEnabled[i];
}

void LeCroyOscilloscope::EnableChannel(size_t i)
{
	//LogDebug("enable channel %d\n", i);
	lock_guard<recursive_mutex> lock(m_mutex);
	//LogDebug("got mutex\n");

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
	{
		//Disable interleaving if we created a conflict
		auto chan = m_channels[i];
		if(IsInterleaving())
		{
			auto conflicts = GetInterleaveConflicts();
			for(auto c : conflicts)
			{
				if( (c.first->IsEnabled() || (c.first == chan) ) &&
					(c.second->IsEnabled() || (c.second == chan) ) )
				{
					SetInterleaving(false);
					break;
				}
			}
		}

		m_transport->SendCommand(chan->GetHwname() + ":TRACE ON");
	}

	//Trigger can't be enabled
	else if(i == m_extTrigChannel->GetIndex())
	{
	}

	//Digital channel
	else
	{
		//If we have NO digital channels enabled, enable the first digital bus
		bool anyDigitalEnabled = false;
		for(auto c : m_digitalChannels)
		{
			if(m_channelsEnabled[c->GetIndex()])
			{
				anyDigitalEnabled = true;
				break;
			}
		}

		if(!anyDigitalEnabled)
			m_transport->SendCommand("VBS 'app.LogicAnalyzer.Digital1.UseGrid=\"YT1\"'");

		//Enable this channel on the hardware
		m_transport->SendCommand(string("VBS 'app.LogicAnalyzer.Digital1.") + m_channels[i]->GetHwname() + " = 1'");
		char tmp[128];
		size_t nbit = (i - m_digitalChannels[0]->GetIndex());
		snprintf(tmp, sizeof(tmp), "VBS 'app.LogicAnalyzer.Digital1.BitIndex%zu = %zu'", nbit, nbit);
		m_transport->SendCommand(tmp);
	}

	m_channelsEnabled[i] = true;
}

void LeCroyOscilloscope::DisableChannel(size_t i)
{
	//LogDebug("enable channel %d\n", i);
	lock_guard<recursive_mutex> lock(m_mutex);
	//LogDebug("got mutex\n");

	m_channelsEnabled[i] = false;

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE OFF");

	//Trigger can't be enabled
	else if(i == m_extTrigChannel->GetIndex())
	{
	}

	//Digital channel
	else
	{
		//If we have NO digital channels enabled, disable the first digital bus
		bool anyDigitalEnabled = false;
		for(auto c : m_digitalChannels)
		{
			if(m_channelsEnabled[c->GetIndex()])
			{
				anyDigitalEnabled = true;
				break;
			}
		}

		if(!anyDigitalEnabled)
			m_transport->SendCommand("VBS 'app.LogicAnalyzer.Digital1.UseGrid=\"NotOnGrid\"'");

		//Disable this channel
		m_transport->SendCommand(string("VBS 'app.LogicAnalyzer.Digital1.") + m_channels[i]->GetHwname() + " = 0'");
	}
}

OscilloscopeChannel::CouplingType LeCroyOscilloscope::GetChannelCoupling(size_t i)
{
	if(i >= m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING?");
	string reply = m_transport->ReadReply().substr(0,3);	//trim off trailing newline, all coupling codes are 3 chars

	if(reply == "A1M")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if(reply == "D1M")
		return OscilloscopeChannel::COUPLE_DC_1M;
	else if(reply == "D50")
		return OscilloscopeChannel::COUPLE_DC_50;
	else if(reply == "GND")
		return OscilloscopeChannel::COUPLE_GND;

	//invalid
	LogWarning("LeCroyOscilloscope::GetChannelCoupling got invalid coupling %s\n", reply.c_str());
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void LeCroyOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(i >= m_analogChannelCount)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING A1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING D1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING D50");
			break;

		//treat unrecognized as ground
		case OscilloscopeChannel::COUPLE_GND:
		default:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING GND");
			break;
	}
}

double LeCroyOscilloscope::GetChannelAttenuation(size_t i)
{
	if(i > m_analogChannelCount)
		return 1;

	//TODO: support ext/10
	if(i == m_extTrigChannel->GetIndex())
		return 1;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":ATTENUATION?");
	string reply = m_transport->ReadReply();

	double d;
	sscanf(reply.c_str(), "%lf", &d);
	return d;
}

void LeCroyOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if(i >= m_analogChannelCount)
		return;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:ATTENUATION %f", m_channels[i]->GetHwname().c_str(), atten);

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(cmd);
}

int LeCroyOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i > m_analogChannelCount)
		return 0;

	lock_guard<recursive_mutex> lock(m_mutex);

	string cmd = "BANDWIDTH_LIMIT?";
	m_transport->SendCommand(cmd);
	string reply = m_transport->ReadReply();

	size_t index = reply.find(m_channels[i]->GetHwname());
	if(index == string::npos)
		return 0;

	char chbw[16];
	sscanf(reply.c_str() + index + 3, "%15[^,\n]", chbw);	//offset 3 for "Cn,"
	string sbw(chbw);

	if(sbw == "OFF")
		return 0;
	else if(sbw == "ON")		//apparently "on" means lowest possible B/W?
		return 20;				//this isn't documented anywhere in the MAUI remote control manual
	else if(sbw == "20MHZ")
		return 20;
	else if(sbw == "200MHZ")
		return 200;
	else if(sbw == "500MHZ")
		return 500;
	else if(sbw == "1GHZ")
		return 1000;
	else if(sbw == "2GHZ")
		return 2000;
	else if(sbw == "3GHZ")
		return 3000;
	else if(sbw == "4GHZ")
		return 4000;
	else if(sbw == "6GHZ")
		return 6000;

	LogWarning("LeCroyOscilloscope::GetChannelCoupling got invalid coupling %s\n", reply.c_str());
	return 0;
}

void LeCroyOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char cmd[128];
	if(limit_mhz == 0)
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,OFF", m_channels[i]->GetHwname().c_str());
	else
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,%uMHZ", m_channels[i]->GetHwname().c_str(), limit_mhz);

	m_transport->SendCommand(cmd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

bool LeCroyOscilloscope::GetMeterAutoRange()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.AutoRange'");
	string str = m_transport->ReadReply();
	int ret;
	sscanf(str.c_str(), "%d", &ret);
	return ret ? true : false;
}

void LeCroyOscilloscope::SetMeterAutoRange(bool enable)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if(enable)
		m_transport->SendCommand("VBS 'app.acquisition.DVM.AutoRange = 1'");
	else
		m_transport->SendCommand("VBS 'app.acquisition.DVM.AutoRange = 0'");
}

void LeCroyOscilloscope::StartMeter()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 1'");
}

void LeCroyOscilloscope::StopMeter()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 0'");
}

double LeCroyOscilloscope::GetVoltage()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.Voltage'");
	string str = m_transport->ReadReply();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

double LeCroyOscilloscope::GetCurrent()
{
	//DMM does not support current
	return 0;
}

double LeCroyOscilloscope::GetTemperature()
{
	//DMM does not support current
	return 0;
}

double LeCroyOscilloscope::GetPeakToPeak()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.Amplitude'");
	string str = m_transport->ReadReply();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

double LeCroyOscilloscope::GetFrequency()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.Frequency'");
	string str = m_transport->ReadReply();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

int LeCroyOscilloscope::GetMeterChannelCount()
{
	return m_analogChannelCount;
}

string LeCroyOscilloscope::GetMeterChannelName(int chan)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return m_channels[chan]->m_displayname;
}

int LeCroyOscilloscope::GetCurrentMeterChannel()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.DvmSource'");
	string str = m_transport->ReadReply();
	int i;
	sscanf(str.c_str(), "C%d", &i);
	return i - 1;	//scope channels are 1 based
}

void LeCroyOscilloscope::SetCurrentMeterChannel(int chan)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char cmd[128];
	snprintf(
		cmd,
		sizeof(cmd),
		"VBS 'app.acquisition.DVM.DvmSource = \"C%d\"",
		chan + 1);	//scope channels are 1 based
	m_transport->SendCommand(cmd);
}

Multimeter::MeasurementTypes LeCroyOscilloscope::GetMeterMode()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.DvmMode'");
	string str = m_transport->ReadReply();

	//trim off trailing whitespace
	while(isspace(str[str.length()-1]))
		str.resize(str.length() - 1);

	if(str == "DC")
		return Multimeter::DC_VOLTAGE;
	else if(str == "DC RMS")
		return Multimeter::DC_RMS_AMPLITUDE;
	else if(str == "ACRMS")
		return Multimeter::AC_RMS_AMPLITUDE;
	else if(str == "Frequency")
		return Multimeter::FREQUENCY;
	else
	{
		LogError("Invalid meter mode \"%s\"\n", str.c_str());
		return Multimeter::DC_VOLTAGE;
	}
}

void LeCroyOscilloscope::SetMeterMode(Multimeter::MeasurementTypes type)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	string stype;
	switch(type)
	{
		case Multimeter::DC_VOLTAGE:
			stype = "DC";
			break;

		case Multimeter::DC_RMS_AMPLITUDE:
			stype = "DC RMS";
			break;

		case Multimeter::AC_RMS_AMPLITUDE:
			stype = "ACRMS";
			break;

		case Multimeter::FREQUENCY:
			stype = "Frequency";
			break;

		//not implemented, disable
		case Multimeter::AC_CURRENT:
		case Multimeter::DC_CURRENT:
		case Multimeter::TEMPERATURE:
			LogWarning("unsupported multimeter mode\n");
			return;

	}

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "VBS 'app.acquisition.DVM.DvmMode = \"%s\"'", stype.c_str());
	m_transport->SendCommand(cmd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator mode

int LeCroyOscilloscope::GetFunctionChannelCount()
{
	if(m_hasFunctionGen)
		return 1;
	else
		return 0;
}

string LeCroyOscilloscope::GetFunctionChannelName(int /*chan*/)
{
	return "FUNC";
}

bool LeCroyOscilloscope::GetFunctionChannelActive(int /*chan*/)
{
	LogWarning("LeCroyOscilloscope::GetFunctionChannelActive unimplemented\n");
	return false;
}

void LeCroyOscilloscope::SetFunctionChannelActive(int /*chan*/, bool on)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	if(on)
		m_transport->SendCommand("VBS 'app.wavesource.enable=True'");
	else
		m_transport->SendCommand("VBS 'app.wavesource.enable=False'");
}

float LeCroyOscilloscope::GetFunctionChannelDutyCycle(int /*chan*/)
{
	//app.wavesource.dutycycle
	LogWarning("LeCroyOscilloscope::GetFunctionChannelDutyCycle unimplemented\n");
	return false;
}

void LeCroyOscilloscope::SetFunctionChannelDutyCycle(int /*chan*/, float /*duty*/)
{
	//app.wavesource.dutycycle
}

float LeCroyOscilloscope::GetFunctionChannelAmplitude(int /*chan*/)
{
	//app.wavesource.amplitude
	LogWarning("LeCroyOscilloscope::GetFunctionChannelAmplitude unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelAmplitude(int /*chan*/, float /*amplitude*/)
{
	//app.wavesource.amplitude
}

float LeCroyOscilloscope::GetFunctionChannelOffset(int /*chan*/)
{
	//app.wavesource.offset
	LogWarning("LeCroyOscilloscope::GetFunctionChannelOffset unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelOffset(int /*chan*/, float /*offset*/)
{
	//app.wavesource.offset
}

float LeCroyOscilloscope::GetFunctionChannelFrequency(int /*chan*/)
{
	//app.wavesource.frequency
	LogWarning("LeCroyOscilloscope::GetFunctionChannelFrequency unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelFrequency(int /*chan*/, float hz)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.frequency = %f'", hz);
	m_transport->SendCommand(tmp);
}

FunctionGenerator::WaveShape LeCroyOscilloscope::GetFunctionChannelShape(int /*chan*/)
{
	//app.wavesource.shape

	LogWarning("LeCroyOscilloscope::GetFunctionChannelShape unimplemented\n");
	return FunctionGenerator::SHAPE_SINE;
}

void LeCroyOscilloscope::SetFunctionChannelShape(int /*chan*/, WaveShape /*shape*/)
{
	//app.wavesource.shape
}

float LeCroyOscilloscope::GetFunctionChannelRiseTime(int /*chan*/)
{
	//app.wavesource.risetime
	LogWarning("LeCroyOscilloscope::GetFunctionChannelRiseTime unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelRiseTime(int /*chan*/, float sec)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.risetime = %f'", sec);
	m_transport->SendCommand(tmp);
}

float LeCroyOscilloscope::GetFunctionChannelFallTime(int /*chan*/)
{
	//app.wavesource.falltime
	LogWarning("LeCroyOscilloscope::GetFunctionChannelFallTime unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelFallTime(int /*chan*/, float sec)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.falltime = %f'", sec);
	m_transport->SendCommand(tmp);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

bool LeCroyOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

Oscilloscope::TriggerMode LeCroyOscilloscope::PollTrigger()
{
	//LogDebug("Polling trigger\n");

	//Read the Internal State Change Register
	string sinr;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("INR?");
		sinr = m_transport->ReadReply();
	}
	//LogDebug("Got trigger state\n");
	int inr = atoi(sinr.c_str());

	//See if we got a waveform
	if(inr & 0x0001)
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	//No waveform, but ready for one?
	if(inr & 0x2000)
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//Stopped, no data available
	//TODO: how to handle auto / normal trigger mode?
	return TRIGGER_MODE_RUN;
}

bool LeCroyOscilloscope::ReadWaveformBlock(string& data)
{
	//Prefix "DESC,\n" or "DAT1,\n". Always seems to be 6 chars and start with a D.
	//Next is the length header. Looks like #9000000346. #9 followed by nine ASCII length digits.
	//Ignore that too.
	string tmp = m_transport->ReadReply();
	size_t offset = tmp.find("D");

	//Copy the rest of the block
	data = tmp.substr(offset + 16);

	return true;
}

/**
	@brief Optimized function for checking channel enable status en masse with less round trips to the scope
 */
void LeCroyOscilloscope::BulkCheckChannelEnableState()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Check enable state in the cache.
	vector<int> uncached;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(m_channelsEnabled.find(i) == m_channelsEnabled.end())
			uncached.push_back(i);
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	//Batched implementation
	if(m_transport->IsCommandBatchingSupported())
	{
		for(auto i : uncached)
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE?");
		for(auto i : uncached)
		{
			string reply = m_transport->ReadReply();
			if(reply == "OFF")
				m_channelsEnabled[i] = false;
			else
				m_channelsEnabled[i] = true;
		}
	}

	//Unoptimized fallback for use with transports that can't handle batching
	else
	{
		for(auto i : uncached)
		{
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE?");

			string reply = m_transport->ReadReply();
			if(reply == "OFF")
				m_channelsEnabled[i] = false;
			else
				m_channelsEnabled[i] = true;
		}
	}

	/*
	//Check digital status
	//TODO: better per-lane queries
	m_transport->SendCommand("Digital1:TRACE?");

	string reply = m_transport->ReadReply();
	if(reply == "OFF")
	{
		for(size_t i=0; i<m_digitalChannelCount; i++)
			m_channelsEnabled[m_digitalChannels[i]->GetIndex()] = false;
	}
	else
	{
		for(size_t i=0; i<m_digitalChannelCount; i++)
			m_channelsEnabled[m_digitalChannels[i]->GetIndex()] = true;
	}*/
}

bool LeCroyOscilloscope::ReadWavedescs(
	vector<string>& wavedescs,
	bool* enabled,
	unsigned int& firstEnabledChannel,
	bool& any_enabled)
{
	//(Note: with VICP framing we cannot use semicolons to separate commands)
	BulkCheckChannelEnableState();
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		enabled[i] = IsChannelEnabled(i);
		if(enabled[i])
			any_enabled = true;
	}
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		wavedescs.push_back("");

		//If NO channels are enabled, query channel 1's WAVEDESC.
		//Per phone conversation w/ Honam @ LeCroy apps, this will be updated even if channel is turned off
		if(enabled[i] || (!any_enabled && i==0))
		{
			if(firstEnabledChannel == UINT_MAX)
				firstEnabledChannel = i;
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? DESC");
		}
	}
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i] || (!any_enabled && i==0))
		{
			if(!ReadWaveformBlock(wavedescs[i]))
				LogError("ReadWaveformBlock for wavedesc %u failed\n", i);
		}
	}

	//Check length, complain if a wavedesc comes back too short
	size_t expected_wavedesc_size = 346;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(!enabled[i] && !(!any_enabled && i==0))
			continue;

		if(wavedescs[i].size() < expected_wavedesc_size)
		{
			LogError("Got wavedesc of %zu bytes (expected %zu)\n", wavedescs[i].size(), expected_wavedesc_size);
			return false;
		}
	}
	return true;
}

void LeCroyOscilloscope::RequestWaveforms(bool* enabled, uint32_t num_sequences, bool denabled)
{
	//Ask for all analog waveforms
	bool sent_wavetime = false;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			//If a multi-segment capture, ask for the trigger time data
			if( (num_sequences > 1) && !sent_wavetime)
			{
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? TIME");
				sent_wavetime = true;
			}

			//Ask for the data
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? DAT1");
		}
	}

	//Ask for the digital waveforms
	if(denabled)
		m_transport->SendCommand("Digital1:WF?");
}

time_t LeCroyOscilloscope::ExtractTimestamp(unsigned char* wavedesc, double& basetime)
{
	/*
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
	snprintf(tblock, sizeof(tblock), "%d-%d-%d %d:%02d:%02d",
		*reinterpret_cast<uint16_t*>(wavedesc + 308),
		wavedesc[307],
		wavedesc[306],
		wavedesc[305],
		wavedesc[304],
		seconds);
	locale cur_locale;
	auto& tget = use_facet< time_get<char> >(cur_locale);
	istringstream stream(tblock);
	ios::iostate state;
	char format[] = "%F %T";
	tget.get(stream, time_get<char>::iter_type(), stream, state, &tstruc, format, format+strlen(format));
	return mktime(&tstruc);
}

vector<WaveformBase*> LeCroyOscilloscope::ProcessAnalogWaveform(
	const char* data,
	size_t datalen,
	string& wavedesc,
	uint32_t num_sequences,
	time_t ttime,
	double basetime,
	double* wavetime)
{
	vector<WaveformBase*> ret;

	//Parse the wavedesc headers
	auto pdesc = (unsigned char*)(&wavedesc[0]);
	//uint32_t wavedesc_len = *reinterpret_cast<uint32_t*>(pdesc + 36);
	//uint32_t usertext_len = *reinterpret_cast<uint32_t*>(pdesc + 40);
	float v_gain = *reinterpret_cast<float*>(pdesc + 156);
	float v_off = *reinterpret_cast<float*>(pdesc + 160);
	float interval = *reinterpret_cast<float*>(pdesc + 176) * 1e12f;
	double h_off = *reinterpret_cast<double*>(pdesc + 180) * 1e12f;	//ps from start of waveform to trigger
	double h_off_frac = fmodf(h_off, interval);						//fractional sample position, in ps
	if(h_off_frac < 0)
		h_off_frac = interval + h_off_frac;		//double h_unit = *reinterpret_cast<double*>(pdesc + 244);

	//Raw waveform data
	size_t num_samples;
	if(m_highDefinition)
		num_samples = datalen/2;
	else
		num_samples = datalen;
	size_t num_per_segment = num_samples / num_sequences;
	int16_t* wdata = (int16_t*)&data[0];
	int8_t* bdata = (int8_t*)&data[0];

	//Update cache with settings from this trigger
	m_memoryDepth = num_per_segment;
	m_memoryDepthValid = true;

	for(size_t j=0; j<num_sequences; j++)
	{
		//Set up the capture we're going to store our data into
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = round(interval);

		cap->m_triggerPhase = h_off_frac;
		cap->m_startTimestamp = ttime;

		//Parse the time
		if(num_sequences > 1)
			cap->m_startPicoseconds = static_cast<int64_t>( (basetime + wavetime[j*2]) * 1e12f );
		else
			cap->m_startPicoseconds = static_cast<int64_t>(basetime * 1e12f);

		cap->Resize(num_per_segment);

		//Convert raw ADC samples to volts
		//TODO: Optimized AVX conversion for 16-bit samples
		float* samps = reinterpret_cast<float*>(&cap->m_samples[0]);
		if(m_highDefinition)
		{
			int16_t* base = wdata + j*num_per_segment;

			for(unsigned int k=0; k<num_per_segment; k++)
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
					for(size_t i=0; i<numblocks; i++)
					{
						//Last block gets any extra that didn't divide evenly
						size_t nsamp = blocksize;
						if(i == lastblock)
							nsamp = num_per_segment - i*blocksize;

						Convert8BitSamplesAVX2(
							(int64_t*)&cap->m_offsets[i*blocksize],
							(int64_t*)&cap->m_durations[i*blocksize],
							samps + i*blocksize,
							bdata + j*num_per_segment + i*blocksize,
							v_gain,
							v_off,
							nsamp,
							i*blocksize);
					}
				}

				//Small waveforms get done single threaded to avoid overhead
				else
				{
					Convert8BitSamplesAVX2(
						(int64_t*)&cap->m_offsets[0],
						(int64_t*)&cap->m_durations[0],
						samps,
						bdata + j*num_per_segment,
						v_gain,
						v_off,
						num_per_segment,
						0);
				}
			}
			else
			{
				Convert8BitSamples(
					(int64_t*)&cap->m_offsets[0],
					(int64_t*)&cap->m_durations[0],
					samps,
					bdata + j*num_per_segment,
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
void LeCroyOscilloscope::Convert8BitSamples(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase)
{
	for(unsigned int k=0; k<count; k++)
	{
		offs[k] = ibase + k;
		durs[k] = 1;
		pout[k] = pin[k] * gain - offset;
	}
}

/**
	@brief Optimized version of Convert8BitSamples()
 */
__attribute__((target("avx2")))
void LeCroyOscilloscope::Convert8BitSamplesAVX2(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase)
{
	unsigned int end = count - (count % 32);

	int64_t __attribute__ ((aligned(32))) ones_x4[] = {1, 1, 1, 1};
	int64_t __attribute__ ((aligned(32))) fours_x4[] = {4, 4, 4, 4};
	int64_t __attribute__ ((aligned(32))) count_x4[] =
	{
		ibase + 0,
		ibase + 1,
		ibase + 2,
		ibase + 3
	};

	__m256i all_ones = _mm256_load_si256(reinterpret_cast<__m256i*>(ones_x4));
	__m256i all_fours = _mm256_load_si256(reinterpret_cast<__m256i*>(fours_x4));
	__m256i counts = _mm256_load_si256(reinterpret_cast<__m256i*>(count_x4));

	__m256 gains = { gain, gain, gain, gain, gain, gain, gain, gain };
	__m256 offsets = { offset, offset, offset, offset, offset, offset, offset, offset };

	for(unsigned int k=0; k<end; k += 32)
	{
		//This is likely a lot faster, but assumes we have 64 byte alignment on pin which is not guaranteed.
		//TODO: fix alignment
		//__m256i raw_samples = _mm256_load_si256(reinterpret_cast<__m256i*>(pin + k));

		//Load all 32 raw ADC samples, without assuming alignment
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
		_mm256_store_ps(pout + k, 		block0_float);
		_mm256_store_ps(pout + k + 8,	block1_float);
		_mm256_store_ps(pout + k + 16,	block2_float);
		_mm256_store_ps(pout + k + 24,	block3_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(unsigned int k=end; k<count; k++)
	{
		offs[k] = ibase + k;
		durs[k] = 1;
		pout[k] = pin[k] * gain - offset;
	}
}

map<int, DigitalWaveform*> LeCroyOscilloscope::ProcessDigitalWaveform(string& data)
{
	map<int, DigitalWaveform*> ret;

	//See what channels are enabled
	string tmp = data.substr(data.find("SelectedLines=") + 14);
	tmp = tmp.substr(0, 16);
	bool enabledChannels[16];
	for(int i=0; i<16; i++)
		enabledChannels[i] = (tmp[i] == '1');

	//Quick and dirty string searching. We only care about a small fraction of the XML
	//so no sense bringing in a full parser.
	tmp = data.substr(data.find("<HorPerStep>") + 12);
	tmp = tmp.substr(0, tmp.find("</HorPerStep>"));
	float interval = atof(tmp.c_str()) * 1e12f;
	//LogDebug("Sample interval: %.2f ps\n", interval);

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
	epoch.tm_wday = 6;				//Jan 1 2000 was a Saturday
	epoch.tm_yday = 0;
	epoch.tm_isdst = now.tm_isdst;
	time_t epoch_stamp = mktime(&epoch);

	//Pull out nanoseconds from the timestamp and convert to picoseconds since that's the scopehal fine time unit
	const int64_t ns_per_sec = 1000000000;
	int64_t start_ns = timestamp % ns_per_sec;
	int64_t start_ps = 1000 * start_ns;
	int64_t start_sec = (timestamp - start_ns) / ns_per_sec;
	time_t start_time = epoch_stamp + start_sec;

	//Pull out the actual binary data (Base64 coded)
	tmp = data.substr(data.find("<BinaryData>") + 12);
	tmp = tmp.substr(0, tmp.find("</BinaryData>"));

	//Decode the base64
	base64_decodestate bstate;
	base64_init_decodestate(&bstate);
	unsigned char* block = new unsigned char[tmp.length()];	//base64 is smaller than plaintext, leave room
	base64_decode_block(tmp.c_str(), tmp.length(), (char*)block, &bstate);

	//We have each channel's data from start to finish before the next (no interleaving).
	//TODO: Multithread across waveforms
	unsigned int icapchan = 0;
	for(unsigned int i=0; i<m_digitalChannelCount; i++)
	{
		if(enabledChannels[i])
		{
			DigitalWaveform* cap = new DigitalWaveform;
			cap->m_timescale = interval;

			//Capture timestamp
			cap->m_startTimestamp = start_time;
			cap->m_startPicoseconds = start_ps;

			//Preallocate memory assuming no deduplication possible
			cap->Resize(num_samples);

			//Save the first sample (can't merge with sample -1 because that doesn't exist)
			size_t base = icapchan*num_samples;
			size_t k = 0;
			cap->m_offsets[0] = 0;
			cap->m_durations[0] = 1;
			cap->m_samples[0] = block[base];

			//Read and de-duplicate the other samples
			//TODO: can we vectorize this somehow?
			bool last = block[base];
			for(size_t j=1; j<num_samples; j++)
			{
				bool sample = block[base + j];

				//Deduplicate consecutive samples with same value
				if(last == sample)
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

			//See how much space we saved
			/*
			LogDebug("%s: %zu samples deduplicated to %zu (%.1f %%)\n",
				m_digitalChannels[i]->m_displayname.c_str(),
				num_samples,
				k,
				(k * 100.0f) / num_samples);
			*/

			//Done, save data and go on to next
			ret[m_digitalChannels[i]->GetIndex()] = cap;
			icapchan ++;
		}

		//No data here for us!
		else
			ret[m_digitalChannels[i]->GetIndex()] = NULL;
	}
	delete[] block;
	return ret;
}

bool LeCroyOscilloscope::AcquireData()
{
	//State for this acquisition (may be more than one waveform)
	uint32_t num_sequences = 1;
	map<int, vector<WaveformBase*> > pending_waveforms;
	double start = GetTime();
	time_t ttime = 0;
	double basetime = 0;
	bool denabled = false;
	map<int, string> analogWaveformData;
	string wavetime;
	bool enabled[8] = {false};
	vector<string> wavedescs;
	double* pwtime = NULL;
	string digitalWaveformData;

	//Acquire the data (but don't parse it)
	{
		lock_guard<recursive_mutex> lock(m_mutex);

		//Get the wavedescs for all channels
		unsigned int firstEnabledChannel = UINT_MAX;
		bool any_enabled = true;
		if(!ReadWavedescs(wavedescs, enabled, firstEnabledChannel, any_enabled))
			return false;

		//Grab the WAVEDESC from the first enabled channel
		unsigned char* pdesc = NULL;
		for(unsigned int i=0; i<m_analogChannelCount; i++)
		{
			if(enabled[i] || (!any_enabled && i==0))
			{
				pdesc = (unsigned char*)(&wavedescs[i][0]);
				break;
			}
		}

		//See if any digital channels are enabled
		if(m_digitalChannelCount > 0)
		{
			m_cacheMutex.lock();
			for(size_t i=0; i<m_digitalChannels.size(); i++)
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
			//Figure out when the first trigger happened.
			//Read the timestamps if we're doing segmented capture
			ttime = ExtractTimestamp(pdesc, basetime);
			if(num_sequences > 1)
				wavetime = m_transport->ReadReply();
			pwtime = reinterpret_cast<double*>(&wavetime[16]);	//skip 16-byte SCPI header

			//Read the data from each analog waveform
			for(unsigned int i=0; i<m_analogChannelCount; i++)
			{
				if(enabled[i])
					analogWaveformData[i] = m_transport->ReadReply();
			}
		}

		//Read the data from the digital waveforms, if enabled
		if(denabled)
		{
			if(!ReadWaveformBlock(digitalWaveformData))
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
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("TRIG_MODE SINGLE");
		m_triggerArmed = true;
	}

	//Process analog waveforms
	vector< vector<WaveformBase*> > waveforms;
	waveforms.resize(m_analogChannelCount);
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			waveforms[i] = ProcessAnalogWaveform(
				&analogWaveformData[i][16],			//skip 16-byte SCPI header DATA,\n#9xxxxxxxx
				analogWaveformData[i].size() - 16,
				wavedescs[i],
				num_sequences,
				ttime,
				basetime,
				pwtime);
		}
	}

	//Save analog waveform data
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(!enabled[i])
			continue;

		//Done, update the data
		for(size_t j=0; j<num_sequences; j++)
			pending_waveforms[i].push_back(waveforms[i][j]);
	}

	//TODO: proper support for sequenced capture when digital channels are active
	//(seems like this doesn't work right on at least wavesurfer 3000 series)
	if(denabled)
	{
		//This is a weird XML-y format but I can't find any other way to get it :(
		map<int, DigitalWaveform*> digwaves = ProcessDigitalWaveform(digitalWaveformData);

		//Done, update the data
		for(auto it : digwaves)
			pending_waveforms[it.first].push_back(it.second);
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	for(size_t i=0; i<num_sequences; i++)
	{
		SequenceSet s;
		for(size_t j=0; j<m_channels.size(); j++)
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

void LeCroyOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//m_transport->SendCommand("TRIG_MODE NORM");
	m_transport->SendCommand("TRIG_MODE SINGLE");	//always do single captures, just re-trigger
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void LeCroyOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//LogDebug("Start single trigger\n");
	m_transport->SendCommand("TRIG_MODE SINGLE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void LeCroyOscilloscope::Stop()
{
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("TRIG_MODE STOP");
	}

	m_triggerArmed = false;
	m_triggerOneShot = true;

	//Clear out any pending data (the user doesn't want it, and we don't want stale stuff hanging around)
	ClearPendingWaveforms();
}

double LeCroyOscilloscope::GetChannelOffset(size_t i)
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

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFSET?");

	string reply = m_transport->ReadReply();
	double offset;
	sscanf(reply.c_str(), "%lf", &offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void LeCroyOscilloscope::SetChannelOffset(size_t i, double offset)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return;

	{
		lock_guard<recursive_mutex> lock2(m_mutex);
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "%s:OFFSET %f", m_channels[i]->GetHwname().c_str(), offset);
		m_transport->SendCommand(tmp);
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

double LeCroyOscilloscope::GetChannelVoltageRange(size_t i)
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

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":VOLT_DIV?");

	string reply = m_transport->ReadReply();
	double volts_per_div;
	sscanf(reply.c_str(), "%lf", &volts_per_div);

	double v = volts_per_div * 8;	//plot is 8 divisions high on all MAUI scopes
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = v;
	return v;
}

void LeCroyOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	double vdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV %.4f", m_channels[i]->GetHwname().c_str(), vdiv);
	m_transport->SendCommand(cmd);
}

vector<uint64_t> LeCroyOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	//Not all scopes can go this slow
	if(m_modelid == MODEL_WAVERUNNER_8K)
		ret.push_back(1000);

	const int64_t k = 1000;
	const int64_t m = k*k;
	const int64_t g = k*m;

	//These rates are supported by all known scopes
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
	ret.push_back(100 * m);
	ret.push_back(200 * m);
	ret.push_back(500 * m);

	ret.push_back(1 * g);
	ret.push_back(2 * g);

	//Some scopes can go faster
	switch(m_modelid)
	{
		case MODEL_DDA_5K:
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			break;

		case MODEL_WAVERUNNER_8K:
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			if(m_hasFastSampleRate)
				ret.push_back(20 * g);
			break;

		case MODEL_HDO_9K:
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			ret.push_back(20 * g);
			break;

		default:
			break;
	}

	return ret;
}

vector<uint64_t> LeCroyOscilloscope::GetSampleRatesInterleaved()
{
	//Same as non-interleaved, plus double, for all known scopes
	vector<uint64_t> ret = GetSampleRatesNonInterleaved();
	ret.push_back(ret[ret.size()-1] * 2);
	return ret;
}

vector<uint64_t> LeCroyOscilloscope::GetSampleDepthsNonInterleaved()
{
	const int64_t k = 1000;
	const int64_t m = k*k;

	vector<uint64_t> ret;

	//Standard sample depths for everything.
	//The front panel allows going as low as 2 samples on some instruments, but don't allow that here.
	//Going below 1K has no measurable perfomance boost.
	ret.push_back(1 * k);
	ret.push_back(2 * k);
	ret.push_back(5 * k);
	ret.push_back(10 * k);
	ret.push_back(20 * k);
	ret.push_back(40 * k);			//20/40 Gsps scopes can use values other than 1/2/5.
									//TODO: figure out which models allow this
	ret.push_back(50 * k);
	ret.push_back(80 * k);
	ret.push_back(100 * k);
	ret.push_back(200 * k);
	ret.push_back(500 * k);

	ret.push_back(1 * m);
	ret.push_back(2 * m);
	ret.push_back(5 * m);
	ret.push_back(10 * m);

	switch(m_modelid)
	{
		//TODO: are there any options between 10M and 24M? is there a 20M?
		//TODO: XXL option gives 48M
		case MODEL_DDA_5K:
			ret.push_back(24 * m);
			break;

		//TODO: seems like we can have multiples of 400 instead of 500 sometimes?
		case MODEL_HDO_9K:
			ret.push_back(25 * m);
			ret.push_back(50 * m);
			ret.push_back(64 * m);
			break;

		//deep memory option gives us 4x the capacity
		case MODEL_WAVERUNNER_8K:
			ret.push_back(16 * m);
			if(m_hasFastSampleRate)
			{
				ret.push_back(32 * m);
				ret.push_back(64 * m);
			}
			break;

		//TODO: add more models here
		default:
			break;
	}

	return ret;
}

vector<uint64_t> LeCroyOscilloscope::GetSampleDepthsInterleaved()
{
	const int64_t k = 1000;
	const int64_t m = k*k;

	vector<uint64_t> ret = GetSampleDepthsNonInterleaved();

	switch(m_modelid)
	{
		//DDA5 is weird, not a power of two
		//TODO: XXL option gives 100M, with 48M on all channels
		case MODEL_DDA_5K:
			ret.push_back(48 * m);
			break;

		//no deep-memory option here
		case MODEL_HDO_9K:
			ret.push_back(128 * m);
			break;

		case MODEL_WAVERUNNER_8K:
			if(m_hasFastSampleRate)
				ret.push_back(128 * m);
			else
				ret.push_back(32 * m);
			break;

		//TODO: add more models here
		default:
			break;
	}

	return ret;
}

set<LeCroyOscilloscope::InterleaveConflict> LeCroyOscilloscope::GetInterleaveConflicts()
{
	set<InterleaveConflict> ret;

	//All scopes normally interleave channels 1/2 and 3/4.
	//If both channels in either pair is in use, that's a problem.
	ret.emplace(InterleaveConflict(m_channels[0], m_channels[1]));
	if(m_analogChannelCount > 2)
		ret.emplace(InterleaveConflict(m_channels[2], m_channels[3]));

	//Waverunner 8 only allows interleaving of 2 and 3.
	//Any use of 1 or 4 disqualifies interleaving.
	if(m_modelid == MODEL_WAVERUNNER_8K)
	{
		ret.emplace(InterleaveConflict(m_channels[0], m_channels[0]));
		ret.emplace(InterleaveConflict(m_channels[3], m_channels[3]));
	}

	return ret;
}

uint64_t LeCroyOscilloscope::GetSampleRate()
{
	if(!m_sampleRateValid)
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("VBS? 'return = app.Acquisition.Horizontal.SampleRate'");
		string reply = m_transport->ReadReply();

		sscanf(reply.c_str(), "%ld", &m_sampleRate);
		m_sampleRateValid = true;
	}

	return m_sampleRate;
}

uint64_t LeCroyOscilloscope::GetSampleDepth()
{
	if(!m_memoryDepthValid)
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("MSIZ?");
		string reply = m_transport->ReadReply();
		float size;
		sscanf(reply.c_str(), "%f", &size);

		m_memoryDepth = size;
		m_memoryDepthValid = true;
	}

	return m_memoryDepth;
}

void LeCroyOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "MSIZ %ld", depth);
	m_transport->SendCommand(tmp);
	m_memoryDepth = depth;
}

void LeCroyOscilloscope::SetSampleRate(uint64_t rate)
{
	uint64_t ps_per_sample = 1000000000000L / rate;
	double time_per_sample = ps_per_sample * 1.0e-12;
	double time_per_plot = time_per_sample * GetSampleDepth();
	double time_per_div = time_per_plot / 10;
	m_sampleRate = rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "TDIV %.0e", time_per_div);
	m_transport->SendCommand(tmp);
}

void LeCroyOscilloscope::EnableTriggerOutput()
{
	//Enable 400ns trigger-out pulse, 1V p-p
	m_transport->SendCommand("VBS? 'app.Acquisition.AuxOutput.AuxMode=\"TriggerOut\"'");
	m_transport->SendCommand("VBS? 'app.Acquisition.AuxOutput.TrigOutPulseWidth=4e-7'");
	m_transport->SendCommand("VBS? 'app.Acquisition.AuxOutput.Amplitude=1'");
}

void LeCroyOscilloscope::SetUseExternalRefclk(bool external)
{
	if(external)
		m_transport->SendCommand("RCLK EXTERNAL");
	else
		m_transport->SendCommand("RCLK INTERNAL");
}

void LeCroyOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//LeCroy's standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(1e12f * halfdepth / rate));

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "TRDL %e", (offset - halfwidth) * 1e-12);
	m_transport->SendCommand(tmp);

	//Don't update the cache because the scope is likely to round the offset we ask for.
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_triggerOffsetValid = false;
}

int64_t LeCroyOscilloscope::GetTriggerOffset()
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
		m_transport->SendCommand("TRDL?");
		reply = m_transport->ReadReply();
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Result comes back in scientific notation
	double sec;
	sscanf(reply.c_str(), "%le", &sec);
	m_triggerOffset = static_cast<int64_t>(round(sec * 1e12));

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(1e12f * halfdepth / rate));
	m_triggerOffset += halfwidth;

	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

void LeCroyOscilloscope::SetDeskewForChannel(size_t channel, int64_t skew)
{
	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS? 'app.Acquisition.%s.Deskew=%e'",
		m_channels[channel]->GetHwname().c_str(),
		skew * 1e-12
		);
	m_transport->SendCommand(tmp);

	//Update cache
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew;
}

int64_t LeCroyOscilloscope::GetDeskewForChannel(size_t channel)
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
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS? 'return = app.Acquisition.%s.Deskew'", m_channels[channel]->GetHwname().c_str());
	m_transport->SendCommand(tmp);
	string reply = m_transport->ReadReply();

	//Value comes back as floating point ps
	float skew;
	sscanf(reply.c_str(), "%f", &skew);
	int64_t skew_ps = round(skew * 1e12f);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew_ps;

	return skew_ps;
}

bool LeCroyOscilloscope::IsInterleaving()
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_interleavingValid)
			return m_interleaving;
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("COMBINE_CHANNELS?");
	auto reply = m_transport->ReadReply();
	if(reply[0] == '1')
		m_interleaving = false;
	else if(reply[0] == '2')
		m_interleaving = true;

	//We don't support "auto" mode. Default to off for now
	else
	{
		m_transport->SendCommand("COMBINE_CHANNELS 1");
		m_interleaving = false;
	}

	m_interleavingValid = true;
	return m_interleaving;
}

bool LeCroyOscilloscope::SetInterleaving(bool combine)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Setting to "off" always is possible
	if(!combine)
	{
		m_transport->SendCommand("COMBINE_CHANNELS 1");

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_interleaving = false;
		m_interleavingValid = true;
	}

	//Turning on requires we check for conflicts
	else if(!CanInterleave())
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_interleaving = false;
		m_interleavingValid = true;
	}

	//All good, turn it on for real
	else
	{
		m_transport->SendCommand("COMBINE_CHANNELS 2");

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_interleaving = true;
		m_interleavingValid = true;
	}

	return m_interleaving;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> LeCroyOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;

	if(m_hasLA)
	{
		for(size_t n=0; n<2; n++)
		{
			DigitalBank bank;

			for(size_t i=0; i<8; i++)
				bank.push_back(m_digitalChannels[i + n*8]);

			banks.push_back(bank);
		}
	}

	return banks;
}

Oscilloscope::DigitalBank LeCroyOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank ret;
	if(m_hasLA)
	{
		if(channel <= m_digitalChannels[7]->GetIndex() )
		{
			for(size_t i=0; i<8; i++)
				ret.push_back(m_digitalChannels[i]);
		}
		else
		{
			for(size_t i=0; i<8; i++)
				ret.push_back(m_digitalChannels[i+8]);
		}
	}
	return ret;
}

bool LeCroyOscilloscope::IsDigitalHysteresisConfigurable()
{
	return true;
}

bool LeCroyOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float LeCroyOscilloscope::GetDigitalHysteresis(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if(channel <= m_digitalChannels[7]->GetIndex() )
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxHysteresis0'");
	else
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxHysteresis1'");

	return atof(m_transport->ReadReply().c_str());
}

float LeCroyOscilloscope::GetDigitalThreshold(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if(channel <= m_digitalChannels[7]->GetIndex() )
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxThreshold0'");
	else
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxThreshold1'");

	return atof(m_transport->ReadReply().c_str());
}

void LeCroyOscilloscope::SetDigitalHysteresis(size_t channel, float level)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[128];
	if(channel <= m_digitalChannels[7]->GetIndex() )
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxHysteresis0 = %e'", level);
	else
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxHysteresis1 = %e'", level);
	m_transport->SendCommand(tmp);
}

void LeCroyOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[128];
	if(channel <= m_digitalChannels[7]->GetIndex() )
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxThreshold0 = %e'", level);
	else
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxThreshold1 = %e'", level);
	m_transport->SendCommand(tmp);
}

void LeCroyOscilloscope::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Figure out what kind of trigger is active.
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Type'");
	string reply = m_transport->ReadReply();
	if (reply == "Edge")
		PullEdgeTrigger();

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
void LeCroyOscilloscope::PullEdgeTrigger()
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
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

	//Source channel
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Source'");
	string reply = m_transport->ReadReply();
	auto chan = GetChannelByHwName(reply);
	et->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source %s\n", reply.c_str());

	//Level
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Edge.Level'");
	et->SetLevel(stof(m_transport->ReadReply()));

	//TODO: OptimizeForHF (changes hysteresis for fast signals)

	//Slope
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Edge.Slope'");
	reply = m_transport->ReadReply();
	if(reply == "Positive")
		et->SetType(EdgeTrigger::EDGE_RISING);
	else if(reply == "Negative")
		et->SetType(EdgeTrigger::EDGE_FALLING);
	else if(reply == "Either")
		et->SetType(EdgeTrigger::EDGE_ANY);
	else
		LogDebug("Unknown trigger slope %s\n", reply.c_str());
}

void LeCroyOscilloscope::PushTrigger()
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
void LeCroyOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Type
	m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"Edge\"");

	//Source
	char tmp[128];
	snprintf(
		tmp,
		sizeof(tmp),
		"VBS? 'app.Acquisition.Trigger.Source = \"%s\"'",
		trig->GetInput(0).m_channel->GetHwname().c_str());
	m_transport->SendCommand(tmp);

	//Level
	snprintf(
		tmp,
		sizeof(tmp),
		"VBS? 'app.Acquisition.Edge.Level = \"%f\"'",
		trig->GetLevel());
	m_transport->SendCommand(tmp);

	//Slope
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Edge.Slope = \"Positive\"'");
			break;

		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Edge.Slope = \"Negative\"'");
			break;

		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Edge.Slope = \"Either\"'");
			break;

		default:
			LogWarning("Invalid trigger type %d\n", trig->GetType());
			break;
	}
}
