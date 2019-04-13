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
#include "RohdeSchwarzHMC8012Multimeter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RohdeSchwarzHMC8012Multimeter::RohdeSchwarzHMC8012Multimeter(string hostname, unsigned short port)
	: m_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
	, m_hostname(hostname)
	, m_port(port)
{
	LogDebug("Connecting to R&S HMC8012 DMM at %s:%d\n", hostname.c_str(), port);

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

	//Ask for the ID
	SendCommand("*IDN?");
	string reply = ReadReply();
	char vendor[128] = "";
	char model[128] = "";
	char serial[128] = "";
	char swversion[128] = "";
	if(4 != sscanf(reply.c_str(), "%127[^,],%127[^,],%127[^,],%127s",
		vendor, model, serial, swversion))
	{
		LogError("Bad IDN response %s\n", reply.c_str());
		return;
	}
	m_vendor = vendor;
	m_model = model;
	m_serial = serial;
	m_fwVersion = swversion;

	m_mode = GetMeterMode();
}

RohdeSchwarzHMC8012Multimeter::~RohdeSchwarzHMC8012Multimeter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command helpers

bool RohdeSchwarzHMC8012Multimeter::SendCommand(string cmd)
{
	cmd += "\n";
	return m_socket.SendLooped((unsigned char*)cmd.c_str(), cmd.length());
}

string RohdeSchwarzHMC8012Multimeter::ReadReply()
{
	string ret;
	unsigned char tmp;

	while(1)
	{
		if(!m_socket.RecvLooped(&tmp, 1))
			return "";

		if(tmp == '\n')
			return ret;
		else
			ret += tmp;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string RohdeSchwarzHMC8012Multimeter::GetName()
{
	return m_model;
}

string RohdeSchwarzHMC8012Multimeter::GetVendor()
{
	return m_vendor;
}

string RohdeSchwarzHMC8012Multimeter::GetSerial()
{
	return m_serial;
}

unsigned int RohdeSchwarzHMC8012Multimeter::GetInstrumentTypes()
{
	return INST_DMM;
}

unsigned int RohdeSchwarzHMC8012Multimeter::GetMeasurementTypes()
{
	return DC_VOLTAGE | FREQUENCY | DC_CURRENT | AC_CURRENT;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

bool RohdeSchwarzHMC8012Multimeter::GetMeterAutoRange()
{
	switch(m_mode)
	{
		case DC_CURRENT:
			SendCommand("SENSE:CURR:DC:RANGE:AUTO?");
			break;

		//TODO
		default:
			LogError("GetMeterAutoRange not implemented yet for modes other than DC_CURRENT\n");
			return false;
	}

	string str = ReadReply();
	return (str == "1");
}

void RohdeSchwarzHMC8012Multimeter::SetMeterAutoRange(bool enable)
{
	switch(m_mode)
	{
		case DC_CURRENT:
			if(enable)
				SendCommand("SENSE:CURR:DC:RANGE:AUTO 1");
			else
				SendCommand("SENSE:CURR:DC:RANGE:AUTO 0");
			break;

		default:
			LogError("SetMeterAutoRange not implemented yet for modes other than DC_CURRENT\n");
	}
}

void RohdeSchwarzHMC8012Multimeter::StartMeter()
{
	//cannot be started or stopped
}

void RohdeSchwarzHMC8012Multimeter::StopMeter()
{
	//cannot be started or stopped
}

double RohdeSchwarzHMC8012Multimeter::GetVoltage()
{
	//assume we're in the right mode for now
	SendCommand("READ?");
	string str = ReadReply();
	double d;
	sscanf(str.c_str(), "%lf", &d);
	return d;
}

double RohdeSchwarzHMC8012Multimeter::GetPeakToPeak()
{
	//assume we're in the right mode for now
	SendCommand("READ?");
	string str = ReadReply();
	double d;
	sscanf(str.c_str(), "%lf", &d);
	return d;
}

double RohdeSchwarzHMC8012Multimeter::GetFrequency()
{
	//assume we're in the right mode for now
	SendCommand("READ?");
	string str = ReadReply();
	double d;
	sscanf(str.c_str(), "%lf", &d);
	return d;
}

double RohdeSchwarzHMC8012Multimeter::GetCurrent()
{
	//assume we're in the right mode for now
	SendCommand("READ?");
	string str = ReadReply();
	double d;
	sscanf(str.c_str(), "%lf", &d);
	return d;
}

int RohdeSchwarzHMC8012Multimeter::GetMeterChannelCount()
{
	return 1;
}

string RohdeSchwarzHMC8012Multimeter::GetMeterChannelName(int /*chan*/)
{
	return "VIN";
}

int RohdeSchwarzHMC8012Multimeter::GetCurrentMeterChannel()
{
	return 0;
}

void RohdeSchwarzHMC8012Multimeter::SetCurrentMeterChannel(int /*chan*/)
{
	//nop
}

Multimeter::MeasurementTypes RohdeSchwarzHMC8012Multimeter::GetMeterMode()
{
	SendCommand("CONF?");
	string str = ReadReply();

	char mode[32];
	sscanf(str.c_str(), "\"%31[^,]", mode);
	string smode = mode;

	if(smode == "CURR")
		return DC_CURRENT;
	else if(smode == "CURR:AC")
		return AC_CURRENT;

	//unknown, pick something
	else
		return DC_VOLTAGE;
}

void RohdeSchwarzHMC8012Multimeter::SetMeterMode(Multimeter::MeasurementTypes type)
{
	switch(type)
	{
		case DC_VOLTAGE:
			SendCommand("MEAS:VOLT:DC?");
			break;

		case DC_CURRENT:
			SendCommand("MEAS:CURR:DC?");
			break;

		case AC_CURRENT:
			SendCommand("MEAS:CURR:AC?");
			break;

		//whatever it is, not supported
		default:
			break;
	}

	m_mode = type;

	//Wait for, and discard, the reply to make sure the change took effect
	ReadReply();
}
