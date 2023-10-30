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
#include "Load.h"
#include "LoadChannel.h"

using namespace std;

Load::Load()
{
	m_serializers.push_back(sigc::mem_fun(this, &Load::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(this, &Load::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(this, &Load::DoPreLoadConfiguration));
}

Load::~Load()
{
}

unsigned int Load::GetInstrumentTypes() const
{
	return INST_LOAD;
}

/**
	@brief Pulls data from hardware and updates our measurements
 */
bool Load::AcquireData()
{
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto lchan = dynamic_cast<LoadChannel*>(m_channels[i]);
		if(!lchan)
			continue;

		lchan->SetScalarValue(LoadChannel::STREAM_VOLTAGE_MEASURED, GetLoadVoltageActual(i));
		lchan->SetScalarValue(LoadChannel::STREAM_SET_POINT, GetLoadSetPoint(i));
		lchan->SetScalarValue(LoadChannel::STREAM_CURRENT_MEASURED, GetLoadCurrentActual(i));
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string Load::LoadModeToString(LoadMode mode)
{
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			return "Constant current";

		case MODE_CONSTANT_VOLTAGE:
			return "Constant voltage";

		case MODE_CONSTANT_RESISTANCE:
			return "Constant resistance";

		case MODE_CONSTANT_POWER:
			return "Constant power";

		default:
			return "Invalid";
	}
}

void Load::DoSerializeConfiguration(YAML::Node& node, IDTable& table)
{
	//If we're derived from load class but not a load, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_LOAD) == 0)
		return;

	YAML::Node chnode = node["channels"];

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_LOAD))
			continue;

		auto chan = GetChannel(i);
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		//Save basic info
		channelNode["loadid"] = table.emplace(chan);

		channelNode["mode"] = LoadModeToString(GetLoadMode(i));
		channelNode["enabled"] = GetLoadActive(i);
		channelNode["setpoint"] = GetLoadSetPoint(i);
		channelNode["voltageActual"] = GetLoadVoltageActual(i);
		channelNode["currentActual"] = GetLoadCurrentActual(i);

		//Current ranges
		YAML::Node iranges;
		auto ranges = GetLoadCurrentRanges(i);
		for(auto r : ranges)
			iranges.push_back(r);
		channelNode["irange"] = GetLoadCurrentRange(i);
		channelNode["iranges"] = iranges;

		//Voltage ranges
		YAML::Node vranges;
		ranges = GetLoadCurrentRanges(i);
		for(auto r : ranges)
			vranges.push_back(r);
		channelNode["vrange"] = GetLoadVoltageRange(i);
		channelNode["vranges"] = vranges;

		node["channels"][key] = channelNode;
	}
}

void Load::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& /*idmap*/)
{
	/*
	//If we're derived from load class but not a load, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_LOAD) == 0)
		return;

	if(node["currentChannel"])
		SetCurrentMeterChannel(node["currentChannel"].as<int>());
	if(node["meterMode"])
		SetMeterMode(TextToMode(node["meterMode"].as<string>()));

	//TODO: ranges

	if(node["secondaryMode"])
		SetSecondaryMeterMode(TextToMode(node["secondaryMode"].as<string>()));
	if(node["autoRange"])
		SetMeterAutoRange(node["autoRange"].as<bool>());
	*/
}

void Load::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& node,
	IDTable& /*idmap*/,
	ConfigWarningList& list)
{
	//If we're derived from load class but not a load, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_LOAD) == 0)
		return;

	//Complain if mode is changed
	/*
	auto mode = TextToMode(node["meterMode"].as<string>());
	if(mode != GetMeterMode())
	{
		list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
			"Operating mode",
			"Changing meter mode",
			ModeToText(GetMeterMode()),
			node["meterMode"].as<string>()));
	}*/
}
