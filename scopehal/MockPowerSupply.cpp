/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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

/**
	@file
	@brief Implementation of MockPowerSupply

	@ingroup psudrivers
 */

#include "scopehal.h"
#include "MockPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPINullTransport object (this driver does not connect to real hardware)
 */
MockPowerSupply::MockPowerSupply(const string& name,
	const string& vendor,
	const string& serial,
	const std::string& transport,
	const std::string& driver,
	const std::string& args) : SCPIDevice(nullptr, false), SCPIInstrument(nullptr, false), MockInstrument(name, vendor, serial, transport, driver, args)
{
	//Need to run this loader prior to the main Oscilloscope loader
	m_preloaders.push_front(sigc::mem_fun(*this, &MockPowerSupply::DoPreLoadConfiguration));
}

MockPowerSupply::~MockPowerSupply()
{
	LogError("Destroying Mock Power Supply !\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void MockPowerSupply::DoPreLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& table, ConfigWarningList& /*warnings*/)
{
	LogError("Loading PSU configuration...\n");
	//Load the channels
	auto& chans = node["channels"];
	for(auto it : chans)
	{
		LogError("Loading PSU channel...\n");
		auto& cnode = it.second;

		//Allocate channel space if we didn't have it yet
		size_t index = cnode["index"].as<int>();
		if(m_channels.size() < (index+1))
			m_channels.resize(index+1);

		auto chan = new PowerSupplyChannel(
			cnode["name"].as<string>(),
			this,
			cnode["color"].as<string>(),
			index);
		m_channels[index] = chan;

		//Create the channel ID
		table.emplace(cnode["id"].as<int>(), chan);
		LogError("Added PSU channel with id %d\n",cnode["id"].as<int>());
	}

	//If any of our channels are null, we're missing configuration for them in the file
	//Create dummy channels so nothing segfaults
	for(size_t i=0; i<m_channels.size(); i++)
	{
		if(m_channels[i] != nullptr)
			continue;

		auto chan = new PowerSupplyChannel(
			"MISSINGNO.",
			this,
			"#808080",
			i);
		m_channels[i] = chan;
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool MockPowerSupply::SupportsSoftStart()
{
	return false;
}

bool MockPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool MockPowerSupply::SupportsMasterOutputSwitching()
{
	return true;
}

bool MockPowerSupply::SupportsOvercurrentShutdown()
{
	return true;
}

uint32_t MockPowerSupply::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool MockPowerSupply::IsPowerConstantCurrent(int chan)
{
	return m_constantCurrent[chan];
}

double MockPowerSupply::GetPowerVoltageActual(int chan)
{
	return m_voltagesActual[chan];
}

double MockPowerSupply::GetPowerVoltageNominal(int chan)
{
	return m_voltagesNominal[chan];
}

double MockPowerSupply::GetPowerCurrentActual(int chan)
{
	return m_currentsActual[chan];
}

double MockPowerSupply::GetPowerCurrentNominal(int chan)
{
	return m_currentsNominal[chan];
}

bool MockPowerSupply::GetPowerChannelActive(int chan)
{
	return m_enabled[chan];
}

void MockPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	m_overcurrentShutdown[chan] = enable;
}

bool MockPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	return m_overcurrentShutdown[chan];
}

bool MockPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	return m_overcurrentTripped[chan];
}

void MockPowerSupply::SetPowerVoltage(int chan, double volts)
{
	m_voltagesNominal[chan] = volts;
	m_voltageSetPoints[chan] = volts;
}

void MockPowerSupply::SetPowerCurrent(int chan, double amps)
{
	m_currentsNominal[chan] = amps;
	m_currentSetPoints[chan] = amps;
}

void MockPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	m_enabled[chan] = on;
}

bool MockPowerSupply::GetMasterPowerEnable()
{
	return m_masterEnabled;
}

void MockPowerSupply::SetMasterPowerEnable(bool enable)
{
	m_masterEnabled = enable;
}
