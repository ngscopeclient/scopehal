/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyOscilloscope::LeCroyOscilloscope(string hostname, unsigned short port)
	: m_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
	, m_hostname(hostname)
	, m_port(port)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_highDefinition(false)
{
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

	//Look at options and see if we have digital channels too
	SendCommand("*OPT?", true);
	string reply = ReadSingleBlockString(true);
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
			//If we have the LA module installed, add the digital channels
			if(o == "MSXX")
			{
				m_hasLA = true;
				LogDebug("* MSXX (logic analyzer)\n");
				LogIndenter li;

				//Only add the channels if we're showing them
				//TODO: better way of doing this!!!
				SendCommand("WAVEFORM_SETUP SP,0,NP,0,FP,0,SN,0");
				SendCommand("Digital1:WF?");
				string data;
				if(!ReadWaveformBlock(data))
					return;
				if(data == "")
				{
					LogDebug("No logic analyzer probe connected\n");
					continue;
				}
				string tmp = data.substr(data.find("SelectedLines=") + 14);
				tmp = tmp.substr(0, 16);
				if(tmp == "0000000000000000")
				{
					LogDebug("No digital channels enabled\n");
					//TODO: allow turning them on/off dynamically
				}

				else
				{
					m_digitalChannelCount = 16;

					char chn[8];
					for(int i=0; i<16; i++)
					{
						snprintf(chn, sizeof(chn), "D%d", i);
						auto chan = new OscilloscopeChannel(
							this,
							chn,
							OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
							GetDefaultChannelColor(m_channels.size()),
							1,
							m_channels.size());
						m_channels.push_back(chan);
						m_digitalChannels.push_back(chan);
					}
				}
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

			//No idea what it is
			else
				LogDebug("* %s (not yet implemented)\n", o.c_str());
		}
	}

	//Desired format for waveform data
	//Only use increased bit depth if the scope actually puts content there!
	if(m_highDefinition)
		SendCommand("COMM_FORMAT DEF9,WORD,BIN");
	else
		SendCommand("COMM_FORMAT DEF9,BYTE,BIN");

	//Clear the state-change register to we get rid of any history we don't care about
	PollTrigger();
}

void LeCroyOscilloscope::IdentifyHardware()
{
	//Turn off headers (complicate parsing and add fluff to the packets)
	SendCommand("CHDR OFF", true);

	//Ask for the ID
	SendCommand("*IDN?", true);
	string reply = ReadSingleBlockString();
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
	if(m_model.find("WS3") == 0)
		m_modelid = MODEL_WAVESURFER_3K;
	else if(m_model.find("WAVERUNNER8") == 0)
		m_modelid = MODEL_WAVERUNNER_8K;
	else
		m_modelid = MODEL_UNKNOWN;

	//TODO: better way of doing this?
	if(m_model.find("HD") != string::npos)
		m_highDefinition = true;
}

void LeCroyOscilloscope::DetectAnalogChannels()
{
	//Last digit of the model number is the number of channels
	int nchans = m_model[m_model.length() - 1] - '0';
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

OscilloscopeChannel* LeCroyOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

void LeCroyOscilloscope::FlushConfigCache()
{
	m_triggerChannel = 0;
	m_triggerChannelValid = false;
	m_triggerLevel = 0;
	m_triggerLevelValid = false;
	m_triggerType = TRIGGER_TYPE_DONTCARE;
	m_triggerTypeValid = false;
	m_channelVoltageRanges.clear();
	m_channelOffsets.clear();
	m_channelsEnabled.clear();
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

//TODO: None of these 3 functions support LA stuff
bool LeCroyOscilloscope::IsChannelEnabled(size_t i)
{
	if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
		return m_channelsEnabled[i];

	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//TODO: handle digital channels, for now just claim they're off
	if(i >= m_analogChannelCount)
		return false;

	//See if the channel is enabled, hide it if not
	string cmd = m_channels[i]->GetHwname() + ":TRACE?";
	SendCommand(cmd);
	string reply = ReadSingleBlockString(true);
	if(reply == "OFF")
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

void LeCroyOscilloscope::EnableChannel(size_t i)
{
	SendCommand(m_channels[i]->GetHwname() + ":TRACE ON");
}

void LeCroyOscilloscope::DisableChannel(size_t i)
{
	SendCommand(m_channels[i]->GetHwname() + ":TRACE OFF");
}

OscilloscopeChannel::CouplingType LeCroyOscilloscope::GetChannelCoupling(size_t i)
{
	if(i > m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	SendCommand(m_channels[i]->GetHwname() + ":COUPLING?");
	string reply = ReadSingleBlockString(true);

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
	//FIXME
}

double LeCroyOscilloscope::GetChannelAttenuation(size_t i)
{
	if(i > m_analogChannelCount)
		return 1;

	SendCommand(m_channels[i]->GetHwname() + ":ATTENUATION?");
	string reply = ReadSingleBlockString(true);

	double d;
	sscanf(reply.c_str(), "%lf", &d);
	return d;
}

void LeCroyOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	//FIXME
}

int LeCroyOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i > m_analogChannelCount)
		return 0;

	string cmd = "BANDWIDTH_LIMIT?";
	SendCommand(cmd);
	string reply = ReadSingleBlockString(true);

	size_t index = reply.find(m_channels[i]->GetHwname());
	if(index == string::npos)
		return 0;

	char chbw[16];
	sscanf(reply.c_str() + index + 3, "%15[^,]", chbw);	//offset 3 for "Cn,"
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
	char cmd[128];
	if(limit_mhz == 0)
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,OFF", m_channels[i]->GetHwname().c_str());
	else
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,%uMHZ", m_channels[i]->GetHwname().c_str(), limit_mhz);

	SendCommand(cmd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

bool LeCroyOscilloscope::GetMeterAutoRange()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.AutoRange'");
	string str = ReadSingleBlockString();
	int ret;
	sscanf(str.c_str(), "%d", &ret);
	return ret ? true : false;
}

void LeCroyOscilloscope::SetMeterAutoRange(bool enable)
{
	if(enable)
		SendCommand("VBS 'app.acquisition.DVM.AutoRange = 1'");
	else
		SendCommand("VBS 'app.acquisition.DVM.AutoRange = 0'");
}

void LeCroyOscilloscope::StartMeter()
{
	SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 1'");
}

void LeCroyOscilloscope::StopMeter()
{
	SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 0'");
}

double LeCroyOscilloscope::GetVoltage()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.Voltage'");
	string str = ReadSingleBlockString();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

double LeCroyOscilloscope::GetCurrent()
{
	//DMM does not support current
	return 0;
}

double LeCroyOscilloscope::GetPeakToPeak()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.Amplitude'");
	string str = ReadSingleBlockString();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

double LeCroyOscilloscope::GetFrequency()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.Frequency'");
	string str = ReadSingleBlockString();
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
	return m_channels[chan]->m_displayname;
}

int LeCroyOscilloscope::GetCurrentMeterChannel()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.DvmSource'");
	string str = ReadSingleBlockString();
	int i;
	sscanf(str.c_str(), "C%d", &i);
	return i - 1;	//scope channels are 1 based
}

void LeCroyOscilloscope::SetCurrentMeterChannel(int chan)
{
	char cmd[128];
	snprintf(
		cmd,
		sizeof(cmd),
		"VBS 'app.acquisition.DVM.DvmSource = \"C%d\"",
		chan + 1);	//scope channels are 1 based
	SendCommand(cmd);
}

Multimeter::MeasurementTypes LeCroyOscilloscope::GetMeterMode()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.DvmMode'");
	string str = ReadSingleBlockString();

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
	string stype;
	switch(type)
	{
		//not implemented, disable
		case Multimeter::AC_CURRENT:
		case Multimeter::DC_CURRENT:
			return;

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
	}

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "VBS 'app.acquisition.DVM.DvmMode = \"%s\"'", stype.c_str());
	SendCommand(cmd);
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
}

void LeCroyOscilloscope::SetFunctionChannelActive(int /*chan*/, bool on)
{
	if(on)
		SendCommand("VBS 'app.wavesource.enable=True'");
	else
		SendCommand("VBS 'app.wavesource.enable=False'");
}

float LeCroyOscilloscope::GetFunctionChannelDutyCycle(int /*chan*/)
{
	//app.wavesource.dutycycle
}

void LeCroyOscilloscope::SetFunctionChannelDutyCycle(int /*chan*/, float duty)
{
	//app.wavesource.dutycycle
}

float LeCroyOscilloscope::GetFunctionChannelAmplitude(int /*chan*/)
{
	//app.wavesource.amplitude
}

void LeCroyOscilloscope::SetFunctionChannelAmplitude(int /*chan*/, float amplitude)
{
	//app.wavesource.amplitude
}

float LeCroyOscilloscope::GetFunctionChannelOffset(int /*chan*/)
{
	//app.wavesource.offset
}

void LeCroyOscilloscope::SetFunctionChannelOffset(int /*chan*/, float offset)
{
	//app.wavesource.offset
}

float LeCroyOscilloscope::GetFunctionChannelFrequency(int /*chan*/)
{
	//app.wavesource.frequency
}

void LeCroyOscilloscope::SetFunctionChannelFrequency(int /*chan*/, float hz)
{
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.frequency = %f'", hz);
	SendCommand(tmp);
}

FunctionGenerator::WaveShape LeCroyOscilloscope::GetFunctionChannelShape(int /*chan*/)
{
	//app.wavesource.shape
}

void LeCroyOscilloscope::SetFunctionChannelShape(int /*chan*/, WaveShape shape)
{
	//app.wavesource.shape
}

float LeCroyOscilloscope::GetFunctionChannelRiseTime(int /*chan*/)
{
	//app.wavesource.risetime
}

void LeCroyOscilloscope::SetFunctionChannelRiseTime(int /*chan*/, float sec)
{
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.risetime = %f'", sec);
	SendCommand(tmp);
}

float LeCroyOscilloscope::GetFunctionChannelFallTime(int /*chan*/)
{
	//app.wavesource.falltime
}

void LeCroyOscilloscope::SetFunctionChannelFallTime(int /*chan*/, float sec)
{
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.falltime = %f'", sec);
	SendCommand(tmp);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

void LeCroyOscilloscope::ResetTriggerConditions()
{
	//FIXME
}

Oscilloscope::TriggerMode LeCroyOscilloscope::PollTrigger()
{
	//LogDebug("Polling trigger\n");

	//Read the Internal State Change Register
	SendCommand("INR?");
	string sinr = ReadSingleBlockString();
	int inr = atoi(sinr.c_str());

	//See if we got a waveform
	if(inr & 0x0001)
		return TRIGGER_MODE_TRIGGERED;

	//No waveform, but ready for one?
	if(inr & 0x2000)
		return TRIGGER_MODE_RUN;

	//Stopped, no data available
	//TODO: how to handle auto / normal trigger mode?
	return TRIGGER_MODE_RUN;
}

bool LeCroyOscilloscope::ReadWaveformBlock(string& data)
{
	//First packet is just a header "DAT1,\n". Throw it away.
	ReadData();

	//Second blocks is a header including the message length. Parse that.
	string lhdr = ReadSingleBlockString();
	unsigned int num_bytes = atoi(lhdr.c_str() + 2);
	if(num_bytes == 0)
	{
		ReadData();
		return true;
	}
	//LogDebug("Expecting %d bytes (%d samples)\n", num_bytes, num_samples);

	//Done with headers, data comes next
	//TODO: do progress feedback eventually
	/*
	float base_progress = i*1.0f / m_analogChannelCount;
	float expected_header_time = 0.25;
	float expected_data_time = 0.09f * num_samples / 1000;
	float expected_total_time = expected_header_time + expected_data_time;
	float header_fraction = expected_header_time / expected_total_time;
	base_progress += header_fraction / m_analogChannelCount;
	progress_callback(base_progress);
	*/

	//Read the data
	data.clear();
	while(true)
	{
		string payload = ReadData();
		data += payload;
		if(data.size() >= num_bytes)
			break;
		//float local_progress = data.size() * 1.0f / num_bytes;
		//progress_callback(base_progress + local_progress / m_analogChannelCount);
	}

	//Throw away the newline at the end
	ReadData();

	if(data.size() != num_bytes)
	{
		LogError("bad rx block size (got %zu, expected %u)\n", data.size(), num_bytes);
		return false;
	}

	return true;
}

/**
	@brief Optimized function for checking channel enable status en masse with less round trips to the scope
 */
void LeCroyOscilloscope::BulkCheckChannelEnableState()
{
	for(unsigned int i=0; i<m_analogChannelCount; i++)
		SendCommand(m_channels[i]->GetHwname() + ":TRACE?");
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		string reply = ReadSingleBlockString();
		if(reply == "OFF")
			m_channelsEnabled[i] = false;
		else
			m_channelsEnabled[i] = true;
	}
}

bool LeCroyOscilloscope::AcquireData(sigc::slot1<int, float> progress_callback)
{
	//LogDebug("Acquire data\n");

	double start = GetTime();

	//Read the wavedesc for every enabled channel in batch mode first
	//(With VICP framing we cannot use semicolons to separate commands)
	vector<string> wavedescs;
	string cmd;
	bool enabled[4] = {false};
	BulkCheckChannelEnableState();
	for(unsigned int i=0; i<m_analogChannelCount; i++)
		enabled[i] = IsChannelEnabled(i);
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		wavedescs.push_back("");
		if(enabled[i])
			SendCommand(m_channels[i]->GetHwname() + ":WF? DESC");
	}
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
			ReadWaveformBlock(wavedescs[i]);
	}

	//Figure out how many sequences we have
	unsigned char* pdesc = NULL;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			pdesc = (unsigned char*)(&wavedescs[i][0]);
			break;
		}
	}
	if(pdesc == NULL)
	{
		//no enabled channels. abort
		return false;
	}
	uint32_t trigtime_len = *reinterpret_cast<uint32_t*>(pdesc + 48);
	uint32_t num_sequences = 1;
	if(trigtime_len > 0)
		num_sequences = trigtime_len / 16;

	//Ask for every enabled channel up front, so the scope can send us the next while we parse the first
	bool sent_wavetime = false;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			//If a multi-segment capture, ask for the trigger time data
			if( (num_sequences > 1) && !sent_wavetime)
			{
				SendCommand(m_channels[i]->GetHwname() + ":WF? TIME");
				sent_wavetime = true;
			}

			//Ask for the data
			SendCommand(m_channels[i]->GetHwname() + ":WF? DAT1");
		}
	}

	//Read the timestamps if we're doing segmented capture
	string wavetime;
	if(num_sequences > 1)
	{
		if(!ReadWaveformBlock(wavetime))
		{
			LogError("fail to read wavetime\n");
			return false;
		}
	}
	double* pwtime = reinterpret_cast<double*>(&wavetime[0]);

	map<int, vector<AnalogCapture*> > pending_waveforms;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(!enabled[i])
		{
			m_channels[i]->SetData(NULL);
			continue;
		}

		float fbase = i*1.0f / m_analogChannelCount;
		progress_callback(fbase);

		//Parse the wavedesc headers
		unsigned char* pdesc = (unsigned char*)(&wavedescs[i][0]);
		//uint32_t wavedesc_len = *reinterpret_cast<uint32_t*>(pdesc + 36);
		//uint32_t usertext_len = *reinterpret_cast<uint32_t*>(pdesc + 40);
		float v_gain = *reinterpret_cast<float*>(pdesc + 156);
		float v_off = *reinterpret_cast<float*>(pdesc + 160);
		float interval = *reinterpret_cast<float*>(pdesc + 176) * 1e12f;
		double h_off = *reinterpret_cast<double*>(pdesc + 180) * 1e12f;	//ps from start of waveform to trigger
		double h_off_frac = fmodf(h_off, interval);						//fractional sample position, in ps
		if(h_off_frac < 0)
			h_off_frac = interval + h_off_frac;		//double h_unit = *reinterpret_cast<double*>(pdesc + 244);

		//Timestamp is a somewhat complex format that needs some shuffling around.
		double fseconds = *reinterpret_cast<double*>(pdesc + 296);
		uint8_t seconds = floor(fseconds);
		double basetime = fseconds - seconds;
		time_t tnow = time(NULL);
		struct tm* now = localtime(&tnow);
		struct tm tstruc;
		tstruc.tm_sec = seconds;
		tstruc.tm_min = pdesc[304];
		tstruc.tm_hour = pdesc[305];
		tstruc.tm_mday = pdesc[306];
		tstruc.tm_mon = pdesc[307];
		tstruc.tm_year = *reinterpret_cast<uint16_t*>(pdesc+308);
		tstruc.tm_wday = now->tm_wday;
		tstruc.tm_yday = now->tm_yday;
		tstruc.tm_isdst = now->tm_isdst;

		//Read the actual waveform data
		string data;
		if(!ReadWaveformBlock(data))
		{
			LogError("fail to read waveform\n");
			break;
		}

		//Raw waveform data
		size_t num_samples;
		if(m_highDefinition)
			num_samples = data.size()/2;
		else
			num_samples = data.size();
		size_t num_per_segment = num_samples / num_sequences;
		int16_t* wdata = (int16_t*)&data[0];
		int8_t* bdata = (int8_t*)&data[0];

		for(size_t j=0; j<num_sequences; j++)
		{
			//Set up the capture we're going to store our data into
			AnalogCapture* cap = new AnalogCapture;
			cap->m_timescale = round(interval);

			cap->m_triggerPhase = h_off_frac;
			cap->m_startTimestamp = mktime(&tstruc);

			//Parse the time
			if(num_sequences > 1)
			{
				double trigger_delta = pwtime[j*2];
				//double trigger_offset = pwtime[j*2 + 1];
				//LogDebug("trigger delta for segment %lu: %.3f us\n", j, trigger_delta * 1e9f);
				cap->m_startPicoseconds = static_cast<int64_t>( (basetime + trigger_delta) * 1e12f );
			}
			else
				cap->m_startPicoseconds = static_cast<int64_t>(basetime * 1e12f);

			//Decode the samples
			cap->m_samples.resize(num_per_segment);
			for(unsigned int i=0; i<num_per_segment; i++)
			{
				if(m_highDefinition)
					cap->m_samples[i] = AnalogSample(i, 1, wdata[i + j*num_per_segment] * v_gain - v_off);
				else
					cap->m_samples[i] = AnalogSample(i, 1, bdata[i + j*num_per_segment] * v_gain - v_off);
			}

			//Done, update the data
			if(j == 0)
				m_channels[i]->SetData(cap);
			else
				pending_waveforms[i].push_back(cap);
		}
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	for(size_t i=0; i<num_sequences-1; i++)
	{
		SequenceSet s;
		for(size_t j=0; j<m_analogChannelCount; j++)
		{
			if(enabled[j])
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}

	double dt = GetTime() - start;
	LogTrace("Waveform download took %.3f ms\n", dt * 1000);

	if(num_sequences > 1)
	{
		//LeCroy's LA is derpy and doesn't support sequenced capture!
		//(at least in wavesurfer 3000 series)
		for(unsigned int i=0; i<m_digitalChannelCount; i++)
			m_channels[m_analogChannelCount + i]->SetData(NULL);
	}

	else if(m_digitalChannelCount > 0)
	{
		//If no digital channels are enabled, skip this step
		bool enabled = false;
		for(size_t i=0; i<m_digitalChannels.size(); i++)
		{
			if(m_digitalChannels[i]->IsEnabled())
			{
				enabled = true;
				break;
			}
		}

		if(enabled)
		{
			//Ask for the waveform. This is a weird XML-y format but I can't find any other way to get it :(
			string cmd = "Digital1:WF?";
			SendCommand(cmd);
			string data;
			if(!ReadWaveformBlock(data))
				return false;

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

			//Pull out the actual binary data (Base64 coded)
			tmp = data.substr(data.find("<BinaryData>") + 12);
			tmp = tmp.substr(0, tmp.find("</BinaryData>"));

			//Decode the base64
			base64_decodestate state;
			base64_init_decodestate(&state);
			unsigned char* block = new unsigned char[tmp.length()];	//base64 is smaller than plaintext, leave room
			base64_decode_block(tmp.c_str(), tmp.length(), (char*)block, &state);

			//We have each channel's data from start to finish before the next (no interleaving).
			unsigned int icapchan = 0;
			for(unsigned int i=0; i<m_digitalChannelCount; i++)
			{
				if(enabledChannels[icapchan])
				{
					DigitalCapture* cap = new DigitalCapture;
					cap->m_timescale = interval;

					for(int j=0; j<num_samples; j++)
						cap->m_samples.push_back(DigitalSample(j, 1, block[icapchan*num_samples + j]));

					//Done, update the data
					m_digitalChannels[i]->SetData(cap);

					//Go to next channel in the capture
					icapchan ++;
				}
				else
				{
					//No data here for us!
					m_digitalChannels[i]->SetData(NULL);
				}
			}

			delete[] block;
		}
	}

	//Refresh protocol decoders
	for(size_t i=0; i<m_channels.size(); i++)
	{
		ProtocolDecoder* decoder = dynamic_cast<ProtocolDecoder*>(m_channels[i]);
		if(decoder != NULL)
			decoder->Refresh();
	}

	return true;
}

void LeCroyOscilloscope::Start()
{
	SendCommand("TRIG_MODE NORM");
}

void LeCroyOscilloscope::StartSingleTrigger()
{
	//LogDebug("Start single trigger\n");
	SendCommand("TRIG_MODE SINGLE");
}

void LeCroyOscilloscope::Stop()
{
	SendCommand("TRIG_MODE STOP");
}

size_t LeCroyOscilloscope::GetTriggerChannelIndex()
{
	//Check cache
	if(m_triggerChannelValid)
		return m_triggerChannel;

	SendCommand("TRIG_SELECT?");
	string reply = ReadSingleBlockString();

	char ignored1[32];
	char ignored2[32];
	char source[32] = "";
	sscanf(reply.c_str(), "%31[^,],%31[^,],%31[^,],\n", ignored1, ignored2, source);

	//TODO: support digital channels

	//Update cache
	if(isdigit(source[1]))
		m_triggerChannel = source[1] - '1';
	else if(!strcmp(source, "EX"))
		m_triggerChannel = m_extTrigChannel->GetIndex();
	else
	{
		LogError("Unknown source %s\n", source);
		m_triggerChannel = 0;
	}
	m_triggerChannelValid = true;
	return m_triggerChannel;
}

void LeCroyOscilloscope::SetTriggerChannelIndex(size_t i)
{
	//For now, always set trigger mode to edge
	SendCommand(string("TRIG_SELECT EDGE,SR,") + m_channels[i]->GetHwname());

	//TODO: support digital channels

	//Update cache
	m_triggerChannel = i;
	m_triggerChannelValid = true;
}

float LeCroyOscilloscope::GetTriggerVoltage()
{
	//Check cache
	if(m_triggerLevelValid)
		return m_triggerLevel;

	SendCommand("TRLV?");
	string reply = ReadSingleBlockString();
	sscanf(reply.c_str(), "%f", &m_triggerLevel);
	m_triggerLevelValid = true;
	return m_triggerLevel;
}

void LeCroyOscilloscope::SetTriggerVoltage(float v)
{
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%s:TRLV %.3f V", m_channels[m_triggerChannel]->GetHwname().c_str(), v);
	SendCommand(tmp);

	//Update cache
	m_triggerLevelValid = true;
	m_triggerLevel = v;
}

Oscilloscope::TriggerType LeCroyOscilloscope::GetTriggerType()
{
	if(m_triggerTypeValid)
		return m_triggerType;

	SendCommand("TRIG_SLOPE?");
	string reply = ReadSingleBlockString();

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
	m_triggerType = type;
	m_triggerTypeValid = true;

	switch(type)
	{
		case Oscilloscope::TRIGGER_TYPE_RISING:
			SendCommand(m_channels[m_triggerChannel]->GetHwname() + ":TRSL POS");
			break;

		case Oscilloscope::TRIGGER_TYPE_FALLING:
			SendCommand(m_channels[m_triggerChannel]->GetHwname() + ":TRSL NEG");
			break;

		case Oscilloscope::TRIGGER_TYPE_CHANGE:
			SendCommand(m_channels[m_triggerChannel]->GetHwname() + ":TRSL EIT");
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
	if(m_channelOffsets.find(i) != m_channelOffsets.end())
		return m_channelOffsets[i];

	SendCommand(m_channels[i]->GetHwname() + ":OFFSET?");

	string reply = ReadSingleBlockString();
	double offset;
	sscanf(reply.c_str(), "%lf", &offset);

	m_channelOffsets[i] = offset;
	return offset;
}

void LeCroyOscilloscope::SetChannelOffset(size_t i, double offset)
{
	//TODO
}

double LeCroyOscilloscope::GetChannelVoltageRange(size_t i)
{
	if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
		return m_channelVoltageRanges[i];

	SendCommand(m_channels[i]->GetHwname() + ":VOLT_DIV?");

	string reply = ReadSingleBlockString();
	double volts_per_div;
	sscanf(reply.c_str(), "%lf", &volts_per_div);

	double v = volts_per_div * 8;	//plot is 8 divisions high on all MAUI scopes
	m_channelVoltageRanges[i] = v;
	return v;
}

void LeCroyOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	double vdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV %.4f", m_channels[i]->GetHwname().c_str(), vdiv);
	SendCommand(cmd);
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
	if(m_modelid == MODEL_WAVERUNNER_8K)
	{
		ret.push_back(5 * g);
		ret.push_back(10 * g);
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
	ret.push_back(5 * k);
	ret.push_back(10 * k);
	ret.push_back(50 * k);
	ret.push_back(100 * k);
	ret.push_back(500 * k);

	ret.push_back(1 * m);
	ret.push_back(5 * m);
	ret.push_back(10 * m);

	//Waverunner 8K has deeper memory.
	//TODO: even deeper memory support for 8K-M series
	if(m_modelid == MODEL_WAVERUNNER_8K)
		ret.push_back(16 * m);

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
