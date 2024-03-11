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
#include "SwitchMatrix.h"

using namespace std;

SwitchMatrix::SwitchMatrix()
{
	m_serializers.push_back(sigc::mem_fun(*this, &SwitchMatrix::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &SwitchMatrix::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &SwitchMatrix::DoPreLoadConfiguration));
}

SwitchMatrix::~SwitchMatrix()
{
}


unsigned int SwitchMatrix::GetInstrumentTypes() const
{
	return INST_SWITCH_MATRIX;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void SwitchMatrix::DoSerializeConfiguration(YAML::Node& node, IDTable& table)
{
	/*
	//Global capabilities/status
	//(This info is only used if we're loading offline)
	YAML::Node caps;
	caps["softstart"] = SupportsSoftStart();
	caps["individualSwitching"] = SupportsIndividualOutputSwitching();
	caps["globalSwitch"] = SupportsMasterOutputSwitching();
	caps["overcurrentShutdown"] = SupportsOvercurrentShutdown();
	node["capabilities"] = caps;

	//Master enable (if present)
	if(SupportsMasterOutputSwitching())
		node["globalSwitch"] = GetMasterPowerEnable();

	//Channel configuration
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_PSU))
			continue;

		auto chan = dynamic_cast<SwitchMatrixChannel*>(GetChannel(i));

		//Save basic info
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];
		channelNode["psuid"] = table.emplace(chan);

		//Save PSU-specific settings (including actual sensor readings for use during offline load)
		if(SupportsVoltageCurrentControl(i))
		{
			channelNode["voltageActual"] = GetPowerVoltageActual(i);
			channelNode["voltageNominal"] = GetPowerVoltageNominal(i);
			channelNode["currentActual"] = GetPowerCurrentActual(i);
			channelNode["currentNominal"] = GetPowerCurrentNominal(i);
			channelNode["constantCurrent"] = IsPowerConstantCurrent(i);
		}
		if(SupportsOvercurrentShutdown())
		{
			channelNode["overcurrentShutdown"] = GetPowerOvercurrentShutdownEnabled(i);
			channelNode["overcurrentTripped"] = GetPowerOvercurrentShutdownTripped(i);
		}
		if(SupportsSoftStart())
		{
			YAML::Node ss;
			ss["enable"] = IsSoftStartEnabled(i);
			ss["ramptime"] = GetSoftStartRampTime(i);
			channelNode["softStart"] = ss;
		}
		channelNode["enabled"] = GetPowerChannelActive(i);

		node["channels"][key] = channelNode;
	}
	*/
}

void SwitchMatrix::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& node,
	IDTable& idmap,
	ConfigWarningList& list)
{
	/*
	Unit volts(Unit::UNIT_VOLTS);
	Unit amps(Unit::UNIT_AMPS);

	if(SupportsMasterOutputSwitching())
	{
		if(node["globalSwitch"].as<bool>() && !GetMasterPowerEnable())
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				"Master enable", "Turning global power switch on", "off", "on"));
		}
	}

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_PSU))
			continue;

		auto chan = dynamic_cast<SwitchMatrixChannel*>(GetChannel(i));

		//Save basic info
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		//Set our ID
		idmap.emplace(channelNode["psuid"].as<int>(), chan);

		//Compare settings to what's on the instrument now and warn if increasing limits,
		//or disabling overcurrent shutdown or soft start
		if(channelNode["voltageNominal"])
		{
			float vnom = channelNode["voltageNominal"].as<float>();
			float inom = channelNode["currentNominal"].as<float>();

			float vact = GetPowerVoltageNominal(i);
			float iact = GetPowerCurrentNominal(i);

			if(vnom > vact)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName(),
					string("Increasing output voltage by ") + volts.PrettyPrint(vnom - vact),
					volts.PrettyPrint(vact),
					volts.PrettyPrint(vnom)));
			}

			if(inom > iact)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName(),
					string("Increasing output current limit by ") + amps.PrettyPrint(inom - iact),
					amps.PrettyPrint(iact),
					amps.PrettyPrint(inom)));
			}
		}
		if(channelNode["overcurrentShutdown"])
		{
			bool ocp = channelNode["overcurrentShutdown"].as<bool>();
			if(GetPowerOvercurrentShutdownEnabled(i) && !ocp)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName() + " OCP", "Disabling overcurrent protection", "on", "off"));
			}
		}
		if(channelNode["softStart"])
		{
			bool ss = channelNode["softStart"]["enable"].as<bool>();
			if(IsSoftStartEnabled(i) && !ss)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName() + " SS", "Disabling soft start", "on", "off"));
			}
		}

		//Warn if turning output on that's currently off
		bool en = channelNode["enabled"].as<bool>();
		if(en && !GetPowerChannelActive(i))
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName(), "Turning power on", "off", "on"));
		}
	}
	*/
}

void SwitchMatrix::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& /*idmap*/)
{
	/*
	//Master enable (if present)
	if(SupportsMasterOutputSwitching())
		SetMasterPowerEnable(node["globalSwitch"].as<bool>());

	//Channel configuration
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_PSU))
			continue;

		//auto chan = dynamic_cast<SwitchMatrixChannel*>(GetChannel(i));

		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		if(SupportsVoltageCurrentControl(i))
		{
			SetPowerVoltage(i, channelNode["voltageNominal"].as<float>());
			SetPowerCurrent(i, channelNode["currentNominal"].as<float>());
		}
		if(SupportsOvercurrentShutdown())
			SetPowerOvercurrentShutdownEnabled(i, channelNode["overcurrentShutdown"].as<bool>());
		if(SupportsSoftStart())
		{
			YAML::Node ss = channelNode["softStart"];

			if(ss["ramptime"])
			{
				//Do not change ramp time if not strictly necessary to avoid output interruption
				//R&S HMC804x will shut down the output when changing ramp time if the output is currently on!
				auto ramptime = ss["ramptime"].as<int64_t>();
				if(ramptime != GetSoftStartRampTime(i))
					SetSoftStartRampTime(i, ramptime);
			}

			//Do not change ramp enable if not strictly necessary to avoid output interruption
			//R&S HMC804x will shut down the output when changing ramp time if the output is currently on!
			auto rampen = ss["enable"].as<bool>();
			if(IsSoftStartEnabled(i) != rampen)
				SetSoftStartEnabled(i, rampen);
		}

		auto en = channelNode["enabled"].as<bool>();
		if(en != GetPowerChannelActive(i))
			SetPowerChannelActive(i, en);
	}
	*/
}
