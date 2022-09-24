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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool RohdeSchwarzHMC804xPowerSupply::SupportsSoftStart()
{
	return true;
}

bool RohdeSchwarzHMC804xPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool RohdeSchwarzHMC804xPowerSupply::SupportsMasterOutputSwitching()
{
	return m_channelCount > 1;
}

bool RohdeSchwarzHMC804xPowerSupply::SupportsOvercurrentShutdown()
{
	return true;
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
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);

	//Get status register
	auto ret = m_transport->SendCommandQueuedWithReply("stat:ques:cond?");
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
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("meas:volt?");
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerVoltageNominal(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("volt?");
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerCurrentActual(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("meas:curr?");
	return atof(ret.c_str());
}

double RohdeSchwarzHMC804xPowerSupply::GetPowerCurrentNominal(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("curr?");
	return atof(ret.c_str());
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerChannelActive(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("outp?");
	return atoi(ret.c_str()) ? true : false;
}

bool RohdeSchwarzHMC804xPowerSupply::IsSoftStartEnabled(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("volt:ramp?");
	return atoi(ret.c_str()) ? true : false;
}

void RohdeSchwarzHMC804xPowerSupply::SetSoftStartEnabled(int chan, bool enable)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	if(enable)
		m_transport->SendCommandQueued("volt:ramp on");
	else
		m_transport->SendCommandQueued("volt:ramp off");
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);

	if(enable)
		m_transport->SendCommandQueued("fuse on");
	else
		m_transport->SendCommandQueued("fuse off");
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("fuse:stat?");
	return atoi(ret.c_str()) ? true : false;
}

bool RohdeSchwarzHMC804xPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	auto ret = m_transport->SendCommandQueuedWithReply("fuse:trip?");
	return atoi(ret.c_str()) ? true : false;
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerVoltage(int chan, double volts)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "volt %.3f\n", volts);
	m_transport->SendCommandQueued(cmd);
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerCurrent(int chan, double amps)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "curr %.3f\n", amps);
	m_transport->SendCommandQueued(cmd);
}

void RohdeSchwarzHMC804xPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	SelectChannel(chan);

	if(on)
		m_transport->SendCommandQueued("outp on");
	else
		m_transport->SendCommandQueued("outp off");
}

bool RohdeSchwarzHMC804xPowerSupply::GetMasterPowerEnable()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	//not supported in single channel device, return "always on"
	if(m_channelCount == 1)
		return true;

	auto ret = m_transport->SendCommandQueuedWithReply("outp:mast?");
	return atoi(ret.c_str()) ? true : false;
}

void RohdeSchwarzHMC804xPowerSupply::SetMasterPowerEnable(bool enable)
{
	//not supported in single channel device
	if(m_channelCount == 1)
		return;

	if(enable)
		m_transport->SendCommandQueued("outp:mast on");
	else
		m_transport->SendCommandQueued("outp:mast off");
}

void RohdeSchwarzHMC804xPowerSupply::SelectChannel(int chan)
{
	//per HMC804x SCPI manual page 26, this command is neither supported nor required
	//for the single channel device
	if(m_channelCount == 1)
		return;

	//Early-out if we're already on the requested channel
	if(m_activeChannel == chan)
		return;

	string cmd = "inst:nsel 1";
	cmd[cmd.length()-1] += chan;
	m_transport->SendCommandQueued(cmd);
	m_activeChannel = chan;
}
