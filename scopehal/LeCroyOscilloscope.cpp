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
#include "ProtocolDecoder.h"
#include "base64.h"
#include <locale>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyOscilloscope::LeCroyOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleRateValid(false)
	, m_sampleRate(1)
	, m_memoryDepthValid(false)
	, m_memoryDepth(1)
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
	m_transport->SendCommand("VBS? 'app.Acquisition.Horizontal.Maximize=\"SetMaximumMemory\"'");

	//Disable channel interleaving until we support this properly
	m_transport->SendCommand("COMBINE_CHANNELS 1");

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

	//Enable all of them
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

	m_triggerChannelValid = false;
	m_triggerLevelValid = false;
	m_triggerType = TRIGGER_TYPE_DONTCARE;
	m_triggerTypeValid = false;
	m_channelVoltageRanges.clear();
	m_channelOffsets.clear();
	m_channelsEnabled.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
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
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE ON");

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
			m_transport->SendCommand("VBS? 'app.LogicAnalyzer.Digital1.UseGrid=\"YT1\"'");

		//Enable this channel on the hardware
		m_transport->SendCommand(string("VBS? 'app.LogicAnalyzer.Digital1.") + m_channels[i]->GetHwname() + " = 1'");
		char tmp[128];
		size_t nbit = (i - m_digitalChannels[0]->GetIndex());
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.Digital1.BitIndex%zu = %zu'", nbit, nbit);
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
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE ON");

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
			m_transport->SendCommand("VBS? 'app.LogicAnalyzer.Digital1.UseGrid=\"NotOnGrid\"'");

		//Disable this channel
		m_transport->SendCommand(string("VBS? 'app.LogicAnalyzer.Digital1.") + m_channels[i]->GetHwname() + " = 0'");
	}
}

OscilloscopeChannel::CouplingType LeCroyOscilloscope::GetChannelCoupling(size_t i)
{
	if(i > m_analogChannelCount)
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

void LeCroyOscilloscope::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//FIXME
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

void LeCroyOscilloscope::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//FIXME
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

void LeCroyOscilloscope::ResetTriggerConditions()
{
	//FIXME
}

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
	localtime_r(&tnow, &tstruc);

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
	string& data,
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
		num_samples = data.size()/2;
	else
		num_samples = data.size();
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

		//Convert raw ADC samples to volts
		cap->Resize(num_per_segment);
		int64_t* offs = reinterpret_cast<int64_t*>(&cap->m_offsets[0]);
		int64_t* durs = reinterpret_cast<int64_t*>(&cap->m_durations[0]);
		float* samps = reinterpret_cast<float*>(&cap->m_samples[0]);
		if(m_highDefinition)
		{
			int16_t* base = wdata + j*num_per_segment;

			for(unsigned int k=0; k<num_per_segment; k++)
			{
				offs[k] = k;
				durs[k] = 1;
				samps[k] = base[k] * v_gain - v_off;
			}
		}
		else
		{
			int8_t* base = bdata + j*num_per_segment;

			for(unsigned int k=0; k<num_per_segment; k++)
			{
				offs[k] = k;
				durs[k] = 1;
				samps[k] = base[k] * v_gain - v_off;
			}
		}

		ret.push_back(cap);
	}

	return ret;
}

map<int, DigitalWaveform*> LeCroyOscilloscope::ProcessDigitalWaveform(
	string& data,
	time_t ttime,
	double basetime)
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
	int num_samples = atoi(tmp.c_str());
	//LogDebug("Expecting %d samples\n", num_samples);

	/*
	Nanoseconds since Jan 1 2000. Use this instead?
	tmp = data.substr(data.find("<FirstEventTime>") + 16);
	tmp = tmp.substr(0, tmp.find("</FirstEventTime>"));
	*/

	//Pull out the actual binary data (Base64 coded)
	tmp = data.substr(data.find("<BinaryData>") + 12);
	tmp = tmp.substr(0, tmp.find("</BinaryData>"));

	//Decode the base64
	base64_decodestate bstate;
	base64_init_decodestate(&bstate);
	unsigned char* block = new unsigned char[tmp.length()];	//base64 is smaller than plaintext, leave room
	base64_decode_block(tmp.c_str(), tmp.length(), (char*)block, &bstate);

	//We have each channel's data from start to finish before the next (no interleaving).
	unsigned int icapchan = 0;
	for(unsigned int i=0; i<m_digitalChannelCount; i++)
	{
		if(enabledChannels[i])
		{
			DigitalWaveform* cap = new DigitalWaveform;
			cap->m_timescale = interval;

			//Capture timestamp
			cap->m_startTimestamp = ttime;
			cap->m_startPicoseconds = static_cast<int64_t>(basetime * 1e12f);
			cap->Resize(num_samples);
			#pragma omp parallel for
			for(int j=0; j<num_samples; j++)
			{
				cap->m_offsets[j] = j;
				cap->m_durations[j] = 1;
				cap->m_samples[j] = block[icapchan*num_samples + j];
			}

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

bool LeCroyOscilloscope::AcquireData(bool toQueue)
{
	//State for this acquisition (may be more than one waveform)
	uint32_t num_sequences = 1;
	map<int, vector<WaveformBase*> > pending_waveforms;
	double start = GetTime();
	time_t ttime;
	double basetime;
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

		//Figure out how many sequences we have
		unsigned char* pdesc = NULL;
		for(unsigned int i=0; i<m_analogChannelCount; i++)
		{
			if(enabled[i] || (!any_enabled && i==0))
			{
				pdesc = (unsigned char*)(&wavedescs[i][0]);
				break;
			}
		}
		if(pdesc == NULL)
		{
			//no enabled channels. abort
			//TODO: handle the case of digital channels enabled, but no analog
			return false;
		}
		uint32_t trigtime_len = *reinterpret_cast<uint32_t*>(pdesc + 48);
		if(trigtime_len > 0)
			num_sequences = trigtime_len / 16;

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

		//Ask for every enabled channel up front, so the scope can send us the next while we parse the first
		RequestWaveforms(enabled, num_sequences, denabled);

		//Figure out when the first trigger happened.
		//Read the timestamps if we're doing segmented capture
		ttime = ExtractTimestamp(pdesc, basetime);
		if(num_sequences > 1)
		{
			if(!ReadWaveformBlock(wavetime))
			{
				LogError("fail to read wavetime\n");
				return false;
			}
		}
		pwtime = reinterpret_cast<double*>(&wavetime[0]);

		//Read the data from each analog waveform
		for(unsigned int i=0; i<m_analogChannelCount; i++)
		{
			if(enabled[i])
			{
				if(!ReadWaveformBlock(analogWaveformData[i]))
				{
					LogError("fail to read waveform\n");
					return false;
				}
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
				analogWaveformData[i],
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
		{
			if(!toQueue)
				m_channels[i]->SetData(NULL);
			continue;
		}
		//Done, update the data
		if(toQueue)
		{
			for(size_t j=0; j<num_sequences; j++)
				pending_waveforms[i].push_back(waveforms[i][j]);
		}
		else
		{
			for(size_t j=0; j<num_sequences; j++)
			{
				if(j == 0)
					m_channels[i]->SetData(waveforms[i][j]);
				else
					pending_waveforms[i].push_back(waveforms[i][j]);
			}
		}
	}

	//TODO: proper support for sequenced capture when digital channels are active
	//(seems like this doesn't work right on at least wavesurfer 3000 series)
	if(denabled)
	{
		//This is a weird XML-y format but I can't find any other way to get it :(
		map<int, DigitalWaveform*> digwaves = ProcessDigitalWaveform(digitalWaveformData, ttime, basetime);

		//Done, update the data
		if(toQueue)
		{
			for(auto it : digwaves)
				pending_waveforms[it.first].push_back(it.second);
		}
		else
		{
			for(auto it : digwaves)
				m_channels[it.first]->SetData(it.second);
		}
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = num_sequences-1;
	if(toQueue)				//if saving to queue, the 0'th segment counts too
		num_pending ++;
	for(size_t i=0; i<num_pending; i++)
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
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("TRIG_MODE STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;

	//Clear out any pending data (the user doesn't want it, and we don't want stale stuff hanging around)
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.clear();
	m_pendingWaveformsMutex.unlock();
}

size_t LeCroyOscilloscope::GetTriggerChannelIndex()
{
	//Check cache
	//No locking, worst case we return a result a few seconds old
	if(m_triggerChannelValid)
		return m_triggerChannel;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("TRIG_SELECT?");
	string reply = m_transport->ReadReply();

	char ignored1[32];
	char ignored2[32];
	char source[32] = "";
	sscanf(reply.c_str(), "%31[^,],%31[^,],%31[^,],\n", ignored1, ignored2, source);

	//Update cache
	if(source[0] == 'D')					//Digital channel numbers are 0 based
	{
		int digitalChannelNum = atoi(source+1);
		if((unsigned)digitalChannelNum >= m_digitalChannelCount)
		{
			m_triggerChannel = 0;
			LogWarning("Trigger is configured for digital channel %s, but we only have %u digital channels\n",
				source, m_digitalChannelCount);
		}

		else
			m_triggerChannel = m_digitalChannels[digitalChannelNum]->GetIndex();
	}
	else if(isdigit(source[1]))				//but analog are 1 based, yay!
		m_triggerChannel = source[1] - '1';
	else if(strstr(source, "EX") == source)	//EX or EX10 for /1 or /10
		m_triggerChannel = m_extTrigChannel->GetIndex();
	else
	{
		LogError("Unknown source %s (reply %s)\n", source, reply.c_str());
		m_triggerChannel = 0;
	}
	m_triggerChannelValid = true;
	return m_triggerChannel;
}

void LeCroyOscilloscope::SetTriggerChannelIndex(size_t i)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//For now, always set trigger mode to edge
	m_transport->SendCommand(string("TRIG_SELECT EDGE,SR,") + m_channels[i]->GetHwname());

	//TODO: support digital channels

	//Update cache
	m_triggerChannel = i;
	m_triggerChannelValid = true;
}

float LeCroyOscilloscope::GetTriggerVoltage()
{
	//Digital channels don't have a meaningful trigger voltage
	if(GetTriggerChannelIndex() > m_extTrigChannel->GetIndex())
		return 0;

	//Check cache.
	//No locking, worst case we return a just-invalidated (but still fresh-ish) result.
	if(m_triggerLevelValid)
		return m_triggerLevel;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("TRLV?");
	string reply = m_transport->ReadReply();
	sscanf(reply.c_str(), "%f", &m_triggerLevel);
	m_triggerLevelValid = true;
	return m_triggerLevel;
}

void LeCroyOscilloscope::SetTriggerVoltage(float v)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%s:TRLV %.3f V", m_channels[m_triggerChannel]->GetHwname().c_str(), v);
	m_transport->SendCommand(tmp);

	//Update cache
	m_triggerLevelValid = true;
	m_triggerLevel = v;
}

Oscilloscope::TriggerType LeCroyOscilloscope::GetTriggerType()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if(m_triggerTypeValid)
		return m_triggerType;

	m_transport->SendCommand("TRIG_SLOPE?");
	string reply = m_transport->ReadReply();

	m_triggerTypeValid = true;

	//TODO: TRIG_SELECT to verify its an edge trigger

	//note newline at end of reply
	if(reply == "POS\n")
		return (m_triggerType = Oscilloscope::TRIGGER_TYPE_RISING);
	else if(reply == "NEG\n")
		return (m_triggerType = Oscilloscope::TRIGGER_TYPE_FALLING);
	else if(reply == "EIT\n")
		return (m_triggerType = Oscilloscope::TRIGGER_TYPE_CHANGE);

	//TODO: handle other types
	return (m_triggerType = Oscilloscope::TRIGGER_TYPE_DONTCARE);
}

void LeCroyOscilloscope::SetTriggerType(Oscilloscope::TriggerType type)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_triggerType = type;
	m_triggerTypeValid = true;

	switch(type)
	{
		case Oscilloscope::TRIGGER_TYPE_RISING:
			m_transport->SendCommand(m_channels[m_triggerChannel]->GetHwname() + ":TRSL POS");
			break;

		case Oscilloscope::TRIGGER_TYPE_FALLING:
			m_transport->SendCommand(m_channels[m_triggerChannel]->GetHwname() + ":TRSL NEG");
			break;

		case Oscilloscope::TRIGGER_TYPE_CHANGE:
			m_transport->SendCommand(m_channels[m_triggerChannel]->GetHwname() + ":TRSL EIT");
			break;

		default:
			LogWarning("Unsupported trigger type\n");
			break;
	}
}

void LeCroyOscilloscope::SetTriggerForChannel(
	OscilloscopeChannel* /*channel*/,
	vector<TriggerType> /*triggerbits*/)
{
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
	ret.push_back(50 * k);
	ret.push_back(100 * k);
	ret.push_back(200 * k);
	ret.push_back(500 * k);

	ret.push_back(1 * m);
	ret.push_back(2 * m);
	ret.push_back(5 * m);
	ret.push_back(10 * m);

	switch(m_modelid)
	{
		//TODO: even deeper memory support for 8K-M series
		case MODEL_WAVERUNNER_8K:
			ret.push_back(16 * m);
			break;

		//TODO: seems like we can have multiples of 400 instead of 500 sometimes?
		case MODEL_HDO_9K:
			ret.push_back(25 * m);
			ret.push_back(50 * m);
			ret.push_back(64 * m);
			break;

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

	//Waverunner 8K allows merging buffers from C2/C3 to get deeper memory
	if(m_modelid == MODEL_WAVERUNNER_8K)
		ret.push_back(32 * m);

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
		m_transport->SendCommand("TDIV?");
		string reply = m_transport->ReadReply();

		/*
			Instead of having a sane API for accessing the actual sample rate, LeCroy scopes report time per "division".
			There are ten divisions in the entire plot area... then we have to check the memory depth too!
		 */
		float time_per_div;
		sscanf(reply.c_str(), "%f", &time_per_div);
		double time_per_plot = time_per_div * 10;
		uint64_t depth = GetSampleDepth();
		double time_per_sample = time_per_plot / (double)depth;
		uint64_t ps_per_sample = round(time_per_sample / 1.0e-12);
		m_sampleRate = 1000000000000L / ps_per_sample;
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
