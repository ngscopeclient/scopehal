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
#include "LeCroyVICPOscilloscope.h"
#include "ProtocolDecoder.h"
#include "base64.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyVICPOscilloscope::LeCroyVICPOscilloscope(string hostname, unsigned short port)
	: m_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
	, m_hostname(hostname)
	, m_port(port)
	, m_nextSequence(1)
	, m_lastSequence(1)
	, m_hasLA(false)
	, m_hasDVM(false)
{
	FlushConfigCache();

	LogDebug("Connecting to VICP oscilloscope at %s:%d\n", hostname.c_str(), port);

	if(!m_socket.Connect(hostname, port))
	{
		LogError("Couldn't connect to socket");
		return;
	}
	if(!m_socket.DisableNagle())
	{
		LogError("Couldn't disable Nagle\n");
		return;
	}

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

	//Last digit of the model number is the number of channels
	int nchans = m_model[m_model.length() - 1] - '0';
	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("CH1");
		chname[2] += i;

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
	m_digitalChannelCount = 0;

	//Add the external trigger input
	m_extTrigChannel = new OscilloscopeChannel(
		this,
		"EXT",
		OscilloscopeChannel::CHANNEL_TYPE_TRIGGER,
		"",
		1,
		m_channels.size(),
		true);
	m_channels.push_back(m_extTrigChannel);

	//Look at options and see if we have digital channels too
	SendCommand("*OPT?", true);
	reply = ReadSingleBlockString(true);
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
				string cmd = "Digital1:WF?";
				SendCommand(cmd);
				string data;
				if(!ReadWaveformBlock(data))
					return;
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

			//No idea what it is
			else
				LogDebug("* %s (not yet implemented)\n", o.c_str());
		}
	}

	//Desired format for waveform data
	SendCommand("COMM_FORMAT DEF9,WORD,BIN");

	//Clear the state-change register to we get rid of any history we don't care about
	PollTrigger();
}

LeCroyVICPOscilloscope::~LeCroyVICPOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VICP protocol logic

bool LeCroyVICPOscilloscope::SendCommand(string cmd, bool eoi)
{
	//Operation and flags header
	string payload;
	uint8_t op 	= OP_DATA;
	if(eoi)
		op |= OP_EOI;
	//TODO: remote, clear, poll flags
	payload += op;
	payload += 0x01;							//protocol version number
	payload += GetNextSequenceNumber(eoi);
	payload += '\0';							//reserved

	//Next 4 header bytes are the message length (network byte order)
	uint32_t len = cmd.length();
	payload += (len >> 24) & 0xff;
	payload += (len >> 16) & 0xff;
	payload += (len >> 8)  & 0xff;
	payload += (len >> 0)  & 0xff;

	//Add message data
	payload += cmd;

	//Actually send it
	if(!m_socket.SendLooped((const unsigned char*)payload.c_str(), payload.size()))
		return false;

	return true;
}

uint8_t LeCroyVICPOscilloscope::GetNextSequenceNumber(bool eoi)
{
	m_lastSequence = m_nextSequence;

	//EOI increments the sequence number.
	//Wrap mod 256, but skip zero!
	if(eoi)
	{
		m_nextSequence ++;
		if(m_nextSequence == 0)
			m_nextSequence = 1;
	}

	return m_lastSequence;
}

/**
	@brief Read exactly one packet from the socket
 */
string LeCroyVICPOscilloscope::ReadData()
{
	//Read the header
	unsigned char header[8];
	if(!m_socket.RecvLooped(header, 8))
		return "";

	//Sanity check
	if(header[1] != 1)
	{
		LogError("Bad VICP protocol version\n");
		return "";
	}
	if(header[2] != m_lastSequence)
	{
		//LogError("Bad VICP sequence number %d (expected %d)\n", header[2], m_lastSequence);
		//return "";
	}
	if(header[3] != 0)
	{
		LogError("Bad VICP reserved field\n");
		return "";
	}

	//TODO: pay attention to header?

	//Read the message data
	uint32_t len = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];
	string ret;
	ret.resize(len);
	if(!m_socket.RecvLooped((unsigned char*)&ret[0], len))
		return "";

	return ret;
}

string LeCroyVICPOscilloscope::ReadSingleBlockString(bool trimNewline)
{
	string payload = ReadData();

	if(trimNewline && (payload.length() > 0) )
	{
		int iend = payload.length() - 1;
		if(trimNewline && (payload[iend] == '\n'))
			payload.resize(iend);
	}

	payload += "\0";
	return payload;
}

string LeCroyVICPOscilloscope::ReadMultiBlockString()
{
	//Read until we get the closing quote
	string data;
	bool first  = true;
	while(true)
	{
		string payload = ReadSingleBlockString();
		data += payload;
		if(!first && payload.find("\"") != string::npos)
			break;
		first = false;
	}
	return data;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device information

OscilloscopeChannel* LeCroyVICPOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

void LeCroyVICPOscilloscope::FlushConfigCache()
{
	m_triggerChannel = 0;
	m_triggerChannelValid = false;
	m_triggerLevel = 0;
	m_triggerLevelValid = false;
	m_triggerType = TRIGGER_TYPE_DONTCARE;
	m_triggerTypeValid = false;
	m_channelVoltageRanges.clear();
}

/**
	@brief See what measurement capabilities we have
 */
unsigned int LeCroyVICPOscilloscope::GetMeasurementTypes()
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
unsigned int LeCroyVICPOscilloscope::GetInstrumentTypes()
{
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasDVM)
		type |= INST_DMM;
	return type;
}

string LeCroyVICPOscilloscope::GetName()
{
	return m_model;
}

string LeCroyVICPOscilloscope::GetVendor()
{
	return m_vendor;
}

string LeCroyVICPOscilloscope::GetSerial()
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

//TODO: None of these 3 functions support LA stuff
//TODO: cache enable state, at least for a while?
bool LeCroyVICPOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//TODO: handle digital channels, for now just claim they're off
	if(i >= m_analogChannelCount)
		return false;

	//See if the channel is enabled, hide it if not
	string cmd = "C1:TRACE?";
	cmd[1] += i;
	SendCommand(cmd);
	string reply = ReadSingleBlockString(true);
	if(reply == "OFF")
		return false;
	return true;
}

void LeCroyVICPOscilloscope::EnableChannel(size_t i)
{
	string cmd = "C1:TRACE ON";
	cmd[1] += i;
	SendCommand(cmd);
}

void LeCroyVICPOscilloscope::DisableChannel(size_t i)
{
	string cmd = "C1:TRACE OFF";
	cmd[1] += i;
	SendCommand(cmd);
}

OscilloscopeChannel::CouplingType LeCroyVICPOscilloscope::GetChannelCoupling(size_t i)
{
	if(i > m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	string cmd = "C1:COUPLING?";
	cmd[1] += i;
	SendCommand(cmd);
	string reply = ReadSingleBlockString(true);

	if(reply == "A1M")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if(reply == "D1M")
		return OscilloscopeChannel::COUPLE_DC_1M;
	else if(reply == "D50")
		return OscilloscopeChannel::COUPLE_DC_50;
	else if(reply == "GND")
		return OscilloscopeChannel::COUPLE_GND;

	LogWarning("LeCroyVICPOscilloscope::GetChannelCoupling got invalid coupling %s\n", reply.c_str());

	//invalid
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void LeCroyVICPOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	//FIXME
}

double LeCroyVICPOscilloscope::GetChannelAttenuation(size_t i)
{
	if(i > m_analogChannelCount)
		return 1;

	string cmd = "C1:ATTENUATION?";
	cmd[1] += i;
	SendCommand(cmd);
	string reply = ReadSingleBlockString(true);

	double d;
	sscanf(reply.c_str(), "%lf", &d);
	return d;
}

void LeCroyVICPOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	//FIXME
}

/**
	@brief Gets the bandwidth limiter for the channel, or 0 if no limit
 */
int LeCroyVICPOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i > m_analogChannelCount)
		return 0;

	string cmd = "BANDWIDTH_LIMIT?";
	SendCommand(cmd);
	string reply = ReadSingleBlockString(true);

	string search = "C1";
	search[1] += i;
	size_t index = reply.find(search);
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

	LogWarning("LeCroyVICPOscilloscope::GetChannelCoupling got invalid coupling %s\n", reply.c_str());
	return 0;
}

void LeCroyVICPOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	char cmd[128];
	if(limit_mhz == 0)
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT C%zu,OFF", i+1);
	else
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT C%zu,%uMHZ", i+1, limit_mhz);

	SendCommand(cmd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

bool LeCroyVICPOscilloscope::GetMeterAutoRange()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.AutoRange'");
	string str = ReadSingleBlockString();
	int ret;
	sscanf(str.c_str(), "%d", &ret);
	return ret ? true : false;
}

void LeCroyVICPOscilloscope::SetMeterAutoRange(bool enable)
{
	if(enable)
		SendCommand("VBS 'app.acquisition.DVM.AutoRange = 1'");
	else
		SendCommand("VBS 'app.acquisition.DVM.AutoRange = 0'");
}

void LeCroyVICPOscilloscope::StartMeter()
{
	SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 1'");
}

void LeCroyVICPOscilloscope::StopMeter()
{
	SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 0'");
}

double LeCroyVICPOscilloscope::GetVoltage()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.Voltage'");
	string str = ReadSingleBlockString();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

double LeCroyVICPOscilloscope::GetCurrent()
{
	//DMM does not support current
	return 0;
}

double LeCroyVICPOscilloscope::GetPeakToPeak()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.Amplitude'");
	string str = ReadSingleBlockString();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

double LeCroyVICPOscilloscope::GetFrequency()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.Frequency'");
	string str = ReadSingleBlockString();
	double ret;
	sscanf(str.c_str(), "%lf", &ret);
	return ret;
}

int LeCroyVICPOscilloscope::GetMeterChannelCount()
{
	return m_analogChannelCount;
}

string LeCroyVICPOscilloscope::GetMeterChannelName(int chan)
{
	return m_channels[chan]->m_displayname;
}

int LeCroyVICPOscilloscope::GetCurrentMeterChannel()
{
	SendCommand("VBS? 'return = app.acquisition.DVM.DvmSource'");
	string str = ReadSingleBlockString();
	int i;
	sscanf(str.c_str(), "C%d", &i);
	return i - 1;	//scope channels are 1 based
}

void LeCroyVICPOscilloscope::SetCurrentMeterChannel(int chan)
{
	char cmd[128];
	snprintf(
		cmd,
		sizeof(cmd),
		"VBS 'app.acquisition.DVM.DvmSource = \"C%d\"",
		chan + 1);	//scope channels are 1 based
	SendCommand(cmd);
}

Multimeter::MeasurementTypes LeCroyVICPOscilloscope::GetMeterMode()
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

void LeCroyVICPOscilloscope::SetMeterMode(Multimeter::MeasurementTypes type)
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
// Triggering

void LeCroyVICPOscilloscope::ResetTriggerConditions()
{
	//FIXME
}

Oscilloscope::TriggerMode LeCroyVICPOscilloscope::PollTrigger()
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

bool LeCroyVICPOscilloscope::ReadWaveformBlock(string& data)
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

bool LeCroyVICPOscilloscope::AcquireData(sigc::slot1<int, float> progress_callback)
{
	//LogDebug("Acquire data\n");

	//See how many captures we have (if using sequence mode)
	SendCommand("SEQUENCE?");
	string seqinfo = ReadSingleBlockString();
	unsigned int num_sequences = 1;
	if(seqinfo.find("ON") != string::npos)
	{
		float max_samples;
		sscanf(seqinfo.c_str(), "ON,%u,%f", &num_sequences, &max_samples);
	}
	//if(num_sequences > 1)
	//	LogDebug("Capturing %u sequences\n", num_sequences);

	//Figure out the trigger delay in the capture (nominal zero is MIDDLE of capture!)
	SendCommand("TRDL?");
	string sdelay = ReadSingleBlockString();
	float delay;
	sscanf(sdelay.c_str(), "%f", &delay);

	//Convert to offset from START of capture (add 5 divisions)
	SendCommand("TDIV?");
	string stdiv = ReadSingleBlockString();
	float tdiv;
	sscanf(stdiv.c_str(), "%f", &tdiv);
	float trigoff = tdiv*5 + delay;
	LogTrace("    Trigger offset from start of capture: %.3f ns (delay %f ns, tdiv %f ns)\n", trigoff * 1e9,
		delay * 1e9, tdiv * 1e9);

	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		//If the channel is invisible, don't waste time capturing data
		if(!m_channels[i]->IsEnabled())
		{
			m_channels[i]->SetData(NULL);
			continue;
		}

		//Set up the capture we're going to store our data into
		AnalogCapture* cap = new AnalogCapture;

		for(unsigned int j=0; j<num_sequences; j++)
		{
			//LogDebug("Channel %u block %u\n", i, j);

			float fbase = i*1.0f / m_analogChannelCount;

			fbase += (j*1.0f / num_sequences) / m_analogChannelCount;
			progress_callback(fbase);

			//Ask for the segment of interest
			string cmd = "WAVEFORM_SETUP SP,0,NP,0,FP,0,SN,";
			char tmp[128];
			snprintf(tmp, sizeof(tmp), "%u", j + 1);	//segment 0 = "all", 1 = first part of capture
			cmd += tmp;
			SendCommand(cmd);

			//Ask for the wavedesc (in raw binary)
			cmd = "C1:WF? 'DESC'";
			cmd[1] += i;
			SendCommand(cmd);
			string wavedesc;
			if(!ReadWaveformBlock(wavedesc))
				break;

			//Parse the wavedesc headers
			//Ref: http://qtwork.tudelft.nl/gitdata/users/guen/qtlabanalysis/analysis_modules/general/lecroy.py
			unsigned char* pdesc = (unsigned char*)(&wavedesc[0]);
			//uint32_t wavedesc_len = *reinterpret_cast<uint32_t*>(pdesc + 36);
			//LogDebug("    Wavedesc len: %d\n", wavedesc_len);
			//uint32_t usertext_len = *reinterpret_cast<uint32_t*>(pdesc + 40);
			//LogDebug("    Usertext len: %d\n", usertext_len);
			//uint32_t trigtime_len = *reinterpret_cast<uint32_t*>(pdesc + 48);
			//LogDebug("    Trigtime len: %d\n", trigtime_len);
			float v_gain = *reinterpret_cast<float*>(pdesc + 156);
			float v_off = *reinterpret_cast<float*>(pdesc + 160);
			float interval = *reinterpret_cast<float*>(pdesc + 176) * 1e12f;
			double h_off = *reinterpret_cast<double*>(pdesc + 180) * 1e12f;	//ps from start of waveform to trigger
			double h_off_frac = fmodf(h_off, interval);						//fractional sample position, in ps
			if(h_off_frac < 0)
				h_off_frac = interval + h_off_frac;
			cap->m_triggerPhase = h_off_frac;	//TODO: handle this properly in segmented mode?
												//We might have multiple offsets
			//double h_unit = *reinterpret_cast<double*>(pdesc + 244);
			//double trig_time = *reinterpret_cast<double*>(pdesc + 296);	//ps ref some arbitrary unit
			//LogDebug("V: gain=%f off=%f\n", v_gain, v_off);
			//LogDebug("    H: off=%lf\n", h_off);
			//LogDebug("    Trigger time: %.0f ps\n", trig_time * 1e12f);
			//LogDebug("Sample interval: %.2f ps\n", interval);

			double trigtime = 0;
			if( (num_sequences > 1) && (j > 0) )
			{
				//If a multi-segment capture, ask for the trigger time data
				cmd = "C1:WF? 'TIME'";
				cmd[1] += i;
				SendCommand(cmd);
				string wavetime;
				if(!ReadWaveformBlock(wavetime))
					break;

				double* ptrigtime = reinterpret_cast<double*>(&wavetime[0]);
				trigtime = ptrigtime[0];
				//double trigoff = ptrigtime[1];	//offset to point 0 from trigger time
			}

			int64_t trigtime_samples = trigtime * 1e12f / interval;
			//int64_t trigoff_samples = trigoff * 1e12f / interval;
			//LogDebug("    Trigger time: %.3f sec (%lu samples)\n", trigtime, trigtime_samples);
			//LogDebug("    Trigger offset: %.3f sec (%lu samples)\n", trigoff, trigoff_samples);

			//double dt = GetTime() - start;
			//start = GetTime();
			//LogDebug("Headers took %.3f ms\n", dt * 1000);

			if(j == 0)
				cap->m_timescale = round(interval);

			//Ask for the actual data (in raw binary)
			cmd = "C1:WF? 'DAT1'";
			cmd[1] += i;
			SendCommand(cmd);
			string data;
			if(!ReadWaveformBlock(data))
				break;
			//dt = GetTime() - start;
			//LogDebug("RX took %.3f ms\n", dt * 1000);

			//If we have samples already in the capture, stretch the final one to our trigger offset
			if(cap->m_samples.size())
			{
				auto& last_sample = cap->m_samples[cap->m_samples.size()-1];
				last_sample.m_duration = trigtime_samples - last_sample.m_offset;
			}

			//Decode the samples
			unsigned int num_samples = data.size()/2;
			//LogDebug("Got %u samples\n", num_samples);
			int16_t* wdata = (int16_t*)&data[0];
			for(unsigned int i=0; i<num_samples; i++)
				cap->m_samples.push_back(AnalogSample(i + trigtime_samples, 1, wdata[i] * v_gain - v_off));
		}

		//Done, update the data
		m_channels[i]->SetData(cap);
	}

	if(num_sequences > 1)
	{
		//LeCroy's LA is derpy and doesn't support sequenced capture!
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
			SendCommand("WAVEFORM_SETUP SP,0,NP,0,FP,0,SN,0");

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

void LeCroyVICPOscilloscope::Start()
{
	SendCommand("TRIG_MODE NORM");
}

void LeCroyVICPOscilloscope::StartSingleTrigger()
{
	//LogDebug("Start single trigger\n");
	SendCommand("TRIG_MODE SINGLE");
}

void LeCroyVICPOscilloscope::Stop()
{
	SendCommand("TRIG_MODE STOP");
}

size_t LeCroyVICPOscilloscope::GetTriggerChannelIndex()
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

void LeCroyVICPOscilloscope::SetTriggerChannelIndex(size_t i)
{
	//For now, always set trigger mode to edge
	char cmd[128];
	if(i < m_analogChannelCount)
		snprintf(cmd, sizeof(cmd), "TRIG_SELECT EDGE,SR,C%zu", i+1);
	else if(i == m_extTrigChannel->GetIndex())
		snprintf(cmd, sizeof(cmd), "TRIG_SELECT EDGE,SR,EX");
	else
	{
		LogError("Invalid trigger channel\n");
		return;
	}
	SendCommand(cmd);

	//TODO: support digital channels

	//Update cache
	m_triggerChannel = i;
	m_triggerChannelValid = true;
}

float LeCroyVICPOscilloscope::GetTriggerVoltage()
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

void LeCroyVICPOscilloscope::SetTriggerVoltage(float v)
{
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "C%zu:TRLV %.3f V", m_triggerChannel + 1, v);
	SendCommand(tmp);

	//Update cache
	m_triggerLevelValid = true;
	m_triggerLevel = v;
}

Oscilloscope::TriggerType LeCroyVICPOscilloscope::GetTriggerType()
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

void LeCroyVICPOscilloscope::SetTriggerType(Oscilloscope::TriggerType type)
{
	m_triggerType = type;
	m_triggerTypeValid = true;

	char tmp[32] = "";

	switch(type)
	{
		case Oscilloscope::TRIGGER_TYPE_RISING:
			snprintf(tmp, sizeof(tmp), "C%zu:TRSL POS", m_triggerChannel + 1);
			break;

		case Oscilloscope::TRIGGER_TYPE_FALLING:
			snprintf(tmp, sizeof(tmp), "C%zu:TRSL NEG", m_triggerChannel + 1);
			break;

		case Oscilloscope::TRIGGER_TYPE_CHANGE:
			snprintf(tmp, sizeof(tmp), "C%zu:TRSL EIT", m_triggerChannel + 1);
			break;

		default:
			LogWarning("Unsupported trigger type\n");
			break;
	}

	SendCommand(tmp);
}

void LeCroyVICPOscilloscope::SetTriggerForChannel(
	OscilloscopeChannel* /*channel*/,
	vector<TriggerType> /*triggerbits*/)
{
}

double LeCroyVICPOscilloscope::GetChannelVoltageRange(size_t i)
{
	if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
		return m_channelVoltageRanges[i];

	char cmd[] = "C1:VOLT_DIV?";
	cmd[1] += i;
	SendCommand(cmd);

	string reply = ReadSingleBlockString();
	double volts_per_div;
	sscanf(reply.c_str(), "%lf", &volts_per_div);

	double v = volts_per_div * 8;	//plot is 8 divisions high on all MAUI scopes
	m_channelVoltageRanges[i] = v;
	return v;
}

void LeCroyVICPOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	double vdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "C%zu:VOLT_DIV %.4f", i+1, vdiv);
	SendCommand(cmd);
}
