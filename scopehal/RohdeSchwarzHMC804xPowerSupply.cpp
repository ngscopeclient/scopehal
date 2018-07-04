/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
#include "RohdeSchwarzHMC804xPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RohdeSchwarzHMC804xPowerSupply::RohdeSchwarzHMC804xPowerSupply(string hostname, unsigned short port)
	: m_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
	, m_hostname(hostname)
	, m_port(port)
	, m_activeChannel(-1)
{
	LogDebug("Connecting to R&S HMC804x PSU at %s:%d\n", hostname.c_str(), port);

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
	char hwversion[128] = "";
	char swversion[128] = "";
	if(5 != sscanf(reply.c_str(), "%127[^,],%127[^,],%127[^,],%127[^,],%127s",
		vendor, model, serial, hwversion, swversion))
	{
		LogError("Bad IDN response %s\n", reply.c_str());
		return;
	}
	m_vendor = vendor;
	m_model = model;
	m_serial = serial;
	m_hwVersion = hwversion;
	m_fwVersion = swversion;

	//Figure out how many channels we have
	m_channelCount = atoi(model + strlen("HMC804"));
}

RohdeSchwarzHMC804xPowerSupply::~RohdeSchwarzHMC804xPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command helpers

bool RohdeSchwarzHMC804xPowerSupply::SendCommand(string cmd)
{
	cmd += "\n";
	return m_socket.SendLooped((unsigned char*)cmd.c_str(), cmd.length());
}

string RohdeSchwarzHMC804xPowerSupply::ReadReply()
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

string RohdeSchwarzHMC804xPowerSupply::GetName()
{
	return m_model;
}

string RohdeSchwarzHMC804xPowerSupply::GetVendor()
{
	return m_vendor;
}

string RohdeSchwarzHMC804xPowerSupply::GetSerial()
{
	return m_serial;
}

unsigned int RohdeSchwarzHMC804xPowerSupply::GetInstrumentTypes()
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool RohdeSchwarzHMC804xPowerSupply::IsPowerConstantCurrent(int chan)
{
	int reg = GetStatusRegister(chan);
	return (reg & 0x02);	//CC bit
}

int RohdeSchwarzHMC804xPowerSupply::GetStatusRegister(int chan)
{
	SelectChannel(chan);

	//Get status register
	SendCommand("stat:ques:cond?");
	string ret = ReadReply();
	return atoi(ret.c_str());
}

int RohdeSchwarzHMC804xPowerSupply::GetPowerChannelCount()
{
	return m_channelCount;
}

string RohdeSchwarzHMC804xPowerSupply::GetPowerChannelName(int chan)
{
	char tmp[] = "CH1";
	tmp[2] += chan;
	return string(tmp);
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerVoltageActual(int chan)
{
	SelectChannel(chan);
	SendCommand("meas:volt?");

	string ret = ReadReply();
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerVoltageNominal(int chan)
{
	SelectChannel(chan);
	SendCommand("volt?");

	string ret = ReadReply();
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerCurrentActual(int chan)
{
	SelectChannel(chan);
	SendCommand("meas:curr?");

	string ret = ReadReply();
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerCurrentNominal(int chan)
{
	SelectChannel(chan);
	SendCommand("curr?");

	string ret = ReadReply();
	return atof(ret.c_str());
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerChannelActive(int chan)
{
	SelectChannel(chan);
	SendCommand("outp?");

	string ret = ReadReply();
	return atoi(ret.c_str()) ? true : false;
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	SelectChannel(chan);

	if(enable)
		SendCommand("fuse on");
	else
		SendCommand("fuse off");
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	SelectChannel(chan);
	SendCommand("fuse:stat?");

	string ret = ReadReply();
	return atoi(ret.c_str()) ? true : false;
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	SelectChannel(chan);
	SendCommand("fuse:trip?");

	string ret = ReadReply();
	return atoi(ret.c_str()) ? true : false;
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerVoltage(int chan, double volts)
{
	SelectChannel(chan);

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "volt %.3f\n", volts);
	SendCommand(cmd);
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerCurrent(int chan, double amps)
{
	SelectChannel(chan);

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "curr %.3f\n", amps);
	SendCommand(cmd);
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	SelectChannel(chan);

	if(on)
		SendCommand("outp on");
	else
		SendCommand("outp off");
}

bool RohdeSchwarzHMC804xPowerSupply::GetMasterPowerEnable()
{
	//not uspported in single channel device, return "always on"
	if(m_channelCount == 1)
		return true;

	SendCommand("outp:mast?");

	string ret = ReadReply();
	return atoi(ret.c_str()) ? true : false;

	return false;
}

void RohdeSchwarzHMC804xPowerSupply::SetMasterPowerEnable(bool enable)
{
	//not supported in single channel device
	if(m_channelCount == 1)
		return;

	if(enable)
		SendCommand("outp:mast on");
	else
		SendCommand("outp:mast off");
}

bool RohdeSchwarzHMC804xPowerSupply::SelectChannel(int chan)
{
	//per HMC804x SCPI manual page 26, this command is neither supported nor required
	//for the single channel device
	if(m_channelCount == 1)
		return true;

	//Early-out if we're already on the requested channel
	if(m_activeChannel == chan)
		return true;

	string cmd = "inst:nsel 1";
	cmd[cmd.length()-1] += chan;
	if(SendCommand(cmd))
	{
		m_activeChannel = chan;
		return true;
	}
	else
	{
		m_activeChannel = -1;
		return false;
	}
}
