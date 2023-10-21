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
#include "DemoPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DemoPowerSupply::DemoPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
{
	m_model = "Power Supply Simulator";
	m_vendor = "Entropic Engineering";
	m_serial = "12345";

	for(int i=0; i<m_numChans; i++)
	{
		m_channels.push_back(
			new PowerSupplyChannel(string("CH_") + m_names[i], this, "#808080", i));

		m_voltages[i] = 3.0;
		m_currents[i] = 3.0;
		m_enabled[i] = i == 0;
		m_ocpState[i] = OCP_OFF;
	}

	m_masterEnabled = true;
}

DemoPowerSupply::~DemoPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string DemoPowerSupply::GetDriverNameInternal()
{
	return "demo";
}

uint32_t DemoPowerSupply::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool DemoPowerSupply::SupportsSoftStart()
{
	return false;
}

bool DemoPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool DemoPowerSupply::SupportsMasterOutputSwitching()
{
	return true;
}

bool DemoPowerSupply::SupportsOvercurrentShutdown()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool DemoPowerSupply::IsPowerConstantCurrent(int chan)
{
	double current = m_voltages[chan] / m_loads[chan];

	if (current > m_currents[chan])
		return true;
	else
		return false;
}

double DemoPowerSupply::GetPowerVoltageActual(int chan)
{
	return GetPowerCurrentActual(chan) * m_loads[chan];
}

double DemoPowerSupply::GetPowerVoltageNominal(int chan)
{
	return m_voltages[chan];
}

double DemoPowerSupply::GetPowerCurrentActual(int chan)
{
	double current = m_voltages[chan] / m_loads[chan];

	if (!m_masterEnabled || !m_enabled[chan] || m_ocpState[chan] == OCP_TRIPPED)
	{
		current = 0;
	}

	if (current > m_currents[chan])
	{
		current = m_currents[chan];

		if (m_ocpState[chan] == OCP_ENABLED)
		{
			m_ocpState[chan] = OCP_TRIPPED;
		}
	}

	// Add 0.1% noise
	float rand_0_to_1 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
	float fudge = 0.999 + (rand_0_to_1 / 500.);

	return fudge * current;
}

double DemoPowerSupply::GetPowerCurrentNominal(int chan)
{
	return m_currents[chan];
}

bool DemoPowerSupply::GetPowerChannelActive(int chan)
{
	return m_enabled[chan];
}

void DemoPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	m_ocpState[chan] = enable ? OCP_ENABLED : OCP_OFF;
}

bool DemoPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	return m_ocpState[chan] != OCP_OFF;
}

bool DemoPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	return m_ocpState[chan] == OCP_TRIPPED;
}

void DemoPowerSupply::SetPowerVoltage(int chan, double volts)
{
	m_voltages[chan] = max(min(volts, m_maxVoltage), 0.);
}

void DemoPowerSupply::SetPowerCurrent(int chan, double amps)
{
	m_currents[chan] = max(min(amps, m_maxAmperage), 0.);
}

void DemoPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	m_enabled[chan] = on;

	if (on && m_ocpState[chan] == OCP_TRIPPED)
	{
		m_ocpState[chan] = OCP_ENABLED;
	}
}

bool DemoPowerSupply::GetMasterPowerEnable()
{
	return m_masterEnabled;
}

void DemoPowerSupply::SetMasterPowerEnable(bool enable)
{
	m_masterEnabled = enable;
}
