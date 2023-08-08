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

#include "scopehal.h"
#include "SiglentPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentPowerSupply::SiglentPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_ch3on(false)
{
	//For now, all supported instruments have three channels
	m_channels.push_back(new PowerSupplyChannel("CH1", this, "#008000", 0));
	m_channels.push_back(new PowerSupplyChannel("CH2", this, "#ffff00", 1));
	m_channels.push_back(new PowerSupplyChannel("CH3", this, "#808080", 2));
}

SiglentPowerSupply::~SiglentPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string SiglentPowerSupply::GetDriverNameInternal()
{
	return "siglent_spd";
}

string SiglentPowerSupply::GetName()
{
	return m_model;
}

string SiglentPowerSupply::GetVendor()
{
	return m_vendor;
}

string SiglentPowerSupply::GetSerial()
{
	return m_serial;
}

uint32_t SiglentPowerSupply::GetInstrumentTypesForChannel(size_t /*i*/)
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool SiglentPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool SiglentPowerSupply::SupportsVoltageCurrentControl(int chan)
{
	//CH2 can be switched but has no other controls
	return (chan == 0) || (chan == 1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

/*
	Bit 0: CH1 CC mode
	Bit 1: CH2 CC mode
	Bit 4: CH1 on
	Bit 5: CH2 on
 */
unsigned int SiglentPowerSupply::GetStatusRegister()
{
	auto str = m_transport->SendCommandQueuedWithReply("syst:stat?");
	unsigned int ret;
	sscanf(str.c_str(), "0x%x", &ret);
	return ret;
}

bool SiglentPowerSupply::IsPowerConstantCurrent(int chan)
{
	if(chan == 0)
		return GetStatusRegister() & 1 ? true : false;
	else if(chan == 1)
		return GetStatusRegister() & 2 ? true : false;
	else
		return false;
}

double SiglentPowerSupply::GetPowerVoltageActual(int chan)
{
	if(chan >= 2)
		return 0;

	auto ret = m_transport->SendCommandQueuedWithReply(string("meas:volt? ") + m_channels[chan]->GetHwname());
	return atof(ret.c_str());
}

double SiglentPowerSupply::GetPowerVoltageNominal(int chan)
{
	if(chan >= 2)
		return 0;

	auto ret = m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":volt?");
	return atof(ret.c_str());
}

double SiglentPowerSupply::GetPowerCurrentActual(int chan)
{
	if(chan >= 2)
		return 0;

	auto ret = m_transport->SendCommandQueuedWithReply(string("meas:curr? ") + m_channels[chan]->GetHwname());
	return atof(ret.c_str());
}

double SiglentPowerSupply::GetPowerCurrentNominal(int chan)
{
	//hard wired 3.2A current limit
	if(chan >= 2)
		return 3.2;

	auto ret = m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":curr?");
	return atof(ret.c_str());
}

bool SiglentPowerSupply::GetPowerChannelActive(int chan)
{
	if(chan == 0)
		return GetStatusRegister() & 0x10 ? true : false;
	else if(chan == 1)
		return GetStatusRegister() & 0x20 ? true : false;

	//TODO: is there any way to query channel 3 enable status via scpi?
	//This does not currently appear possible
	else //if(chan == 2)
		return m_ch3on;
}

void SiglentPowerSupply::SetPowerVoltage(int chan, double volts)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":VOLT " + to_string(volts));
}

void SiglentPowerSupply::SetPowerCurrent(int chan, double amps)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":CURR " + to_string(amps));
}

void SiglentPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	if(on)
		m_transport->SendCommandQueued(string("OUTP ") + m_channels[chan]->GetHwname() + ",ON");
	else
		m_transport->SendCommandQueued(string("OUTP ") + m_channels[chan]->GetHwname() + ",OFF");
}
