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

#include "scopehal.h"
#include "HP662xAPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

HP662xAPowerSupply::HP662xAPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
	, m_activeChannel(-1)
{
	// This instrument predates SCPI, use alternate commands to identify
	bool succeeded = false;
	for (int retry = 0; retry < 3; retry++)
	{
		m_transport->FlushRXBuffer();
		m_transport->SendCommand("ID?");
		string reply = Trim(m_transport->ReadReply());

		if (reply.compare(0, 5, "HP662") != 0)
		{
			LogError("Invalid model number: '%s'\n", reply.c_str());
			m_transport->FlushRXBuffer();
			continue; // retry
		}

		succeeded = true;
		m_model = reply;
		m_transport->FlushRXBuffer();
		break; // success
	}
	if (!succeeded)
	{
		LogError("Persistent bad ID response, giving up\n");
		return;
	}
	m_vendor = "HP";
	m_serial = "N/A";
	m_fwVersion = "N/A";

	//Figure out how many channels we have
	int nchans = 2;
	if(m_model == "HP6623A")
	{
		nchans = 3;
	}
	else if(m_model == "HP6624A" || m_model == "HP6627A")
	{
		nchans = 4;

	}
	for(int i=0; i<nchans; i++)
	{
		m_channels.push_back(
			new PowerSupplyChannel(to_string(i+1), this, "#808080", i));
	}
}

HP662xAPowerSupply::~HP662xAPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string HP662xAPowerSupply::GetDriverNameInternal()
{
	return "hp_66xxa";
}

uint32_t HP662xAPowerSupply::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool HP662xAPowerSupply::SupportsSoftStart()
{
	return false;
}

bool HP662xAPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool HP662xAPowerSupply::SupportsMasterOutputSwitching()
{
	return false;
}

bool HP662xAPowerSupply::SupportsOvercurrentShutdown()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing


void HP662xAPowerSupply::ChannelCommand(const char* command, int chan)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s %i", command, chan+1);
	m_transport->SendCommandQueued(cmd);
}

void HP662xAPowerSupply::ChannelCommand(const char* command, int chan, int arg)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s %i %i", command, chan+1, arg);
	m_transport->SendCommandQueued(cmd);
}

void HP662xAPowerSupply::ChannelCommand(const char* command, int chan, double arg)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s %i %f", command, chan+1, arg);
	m_transport->SendCommandQueued(cmd);
}

std::string HP662xAPowerSupply::ChannelQuery(const char* query, int chan)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s %i", query, chan+1);
	return m_transport->SendCommandQueuedWithReply(cmd);
}

int HP662xAPowerSupply::GetStatusRegister(int chan)
{
	auto ret = ChannelQuery("STS?", chan);
	return stoi(ret);
}

bool HP662xAPowerSupply::IsPowerConstantCurrent(int chan)
{
	int reg = GetStatusRegister(chan);
	return (reg & 6);	//CC+ or CC- bit
}

double HP662xAPowerSupply::GetPowerVoltageActual(int chan)
{
	auto ret = ChannelQuery("VOUT?", chan);
	return stof(ret);
}

double HP662xAPowerSupply::GetPowerVoltageNominal(int chan)
{
	auto ret = ChannelQuery("VSET?", chan);
	return stof(ret);
}

double HP662xAPowerSupply::GetPowerCurrentActual(int chan)
{
	auto ret = ChannelQuery("IOUT?", chan);
	return stof(ret);
}

double HP662xAPowerSupply::GetPowerCurrentNominal(int chan)
{
	auto ret = ChannelQuery("ISET?", chan);
	return stof(ret);
}

bool HP662xAPowerSupply::GetPowerChannelActive(int chan)
{
	auto ret = ChannelQuery("OUT?", chan);
	return stoi(ret);
}

void HP662xAPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	ChannelCommand("OCP", chan, enable ? 1 : 0);
}

bool HP662xAPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	auto ret = ChannelQuery("OCP?", chan);
	return stoi(ret);
}

bool HP662xAPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	int reg = GetStatusRegister(chan);
	return (reg & 64);	//OC bit
}

void HP662xAPowerSupply::SetPowerVoltage(int chan, double volts)
{
	ChannelCommand("VSET", chan, volts);
}

void HP662xAPowerSupply::SetPowerCurrent(int chan, double amps)
{
	ChannelCommand("ISET", chan, amps);
}

void HP662xAPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	ChannelCommand("OUT", chan, on ? 1 : 0);
	//FIXME: Automatically resetting the OCP to match expected behavior
	if(!on)
	{
		ChannelCommand("OCRST", chan);
	}
}
