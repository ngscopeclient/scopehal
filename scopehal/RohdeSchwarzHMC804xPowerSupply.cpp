/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

RohdeSchwarzHMC804xPowerSupply::RohdeSchwarzHMC804xPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_activeChannel(-1)
{
	//Figure out how many channels we have
	m_channelCount = atoi(m_model.c_str() + strlen("HMC804"));
}

RohdeSchwarzHMC804xPowerSupply::~RohdeSchwarzHMC804xPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string RohdeSchwarzHMC804xPowerSupply::GetDriverNameInternal()
{
	return "rs_hmc804x";
}

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
	m_transport->SendCommand("stat:ques:cond?");
	string ret = m_transport->ReadReply();
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
	m_transport->SendCommand("meas:volt?");

	string ret = m_transport->ReadReply();
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerVoltageNominal(int chan)
{
	SelectChannel(chan);
	m_transport->SendCommand("volt?");

	string ret = m_transport->ReadReply();
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerCurrentActual(int chan)
{
	SelectChannel(chan);
	m_transport->SendCommand("meas:curr?");

	string ret = m_transport->ReadReply();
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerCurrentNominal(int chan)
{
	SelectChannel(chan);
	m_transport->SendCommand("curr?");

	string ret = m_transport->ReadReply();
	return atof(ret.c_str());
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerChannelActive(int chan)
{
	SelectChannel(chan);
	m_transport->SendCommand("outp?");

	string ret = m_transport->ReadReply();
	return atoi(ret.c_str()) ? true : false;
}

bool RohdeSchwarzHMC804xPowerSupply::IsSoftStartEnabled(int chan)
{
	SelectChannel(chan);
	m_transport->SendCommand("volt:ramp?");
	string ret = m_transport->ReadReply();
	return atoi(ret.c_str()) ? true : false;
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	SelectChannel(chan);

	if(enable)
		m_transport->SendCommand("fuse on");
	else
		m_transport->SendCommand("fuse off");
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	SelectChannel(chan);
	m_transport->SendCommand("fuse:stat?");

	string ret = m_transport->ReadReply();
	return atoi(ret.c_str()) ? true : false;
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	SelectChannel(chan);
	m_transport->SendCommand("fuse:trip?");

	string ret = m_transport->ReadReply();
	return atoi(ret.c_str()) ? true : false;
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerVoltage(int chan, double volts)
{
	SelectChannel(chan);

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "volt %.3f\n", volts);
	m_transport->SendCommand(cmd);
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerCurrent(int chan, double amps)
{
	SelectChannel(chan);

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "curr %.3f\n", amps);
	m_transport->SendCommand(cmd);
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	SelectChannel(chan);

	if(on)
		m_transport->SendCommand("outp on");
	else
		m_transport->SendCommand("outp off");
}

bool RohdeSchwarzHMC804xPowerSupply::GetMasterPowerEnable()
{
	//not uspported in single channel device, return "always on"
	if(m_channelCount == 1)
		return true;

	m_transport->SendCommand("outp:mast?");

	string ret = m_transport->ReadReply();
	return atoi(ret.c_str()) ? true : false;

	return false;
}

void RohdeSchwarzHMC804xPowerSupply::SetMasterPowerEnable(bool enable)
{
	//not supported in single channel device
	if(m_channelCount == 1)
		return;

	if(enable)
		m_transport->SendCommand("outp:mast on");
	else
		m_transport->SendCommand("outp:mast off");
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
	if(m_transport->SendCommand(cmd))
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
