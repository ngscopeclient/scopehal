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
	m_serializers.push_back(sigc::mem_fun(*this, &Load::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &Load::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &Load::DoPreLoadConfiguration));
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

string Load::GetNameOfLoadMode(LoadMode mode)
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

Load::LoadMode Load::GetLoadModeOfName(const string& name)
{
	if(name == "Constant current")
		return MODE_CONSTANT_CURRENT;
	else if(name == "Constant voltage")
		return MODE_CONSTANT_VOLTAGE;
	else if(name == "Constant resistance")
		return MODE_CONSTANT_RESISTANCE;
	else if(name == "Constant power")
		return MODE_CONSTANT_POWER;

	//invalid
	else
		return MODE_CONSTANT_CURRENT;
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

		channelNode["mode"] = GetNameOfLoadMode(GetLoadMode(i));
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

void Load::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& idmap)
{
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_LOAD))
			continue;

		auto chan = dynamic_cast<LoadChannel*>(GetChannel(i));
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];
		idmap.emplace(channelNode["loadid"].as<intptr_t>(), chan);

		SetLoadMode(i, GetLoadModeOfName(channelNode["mode"].as<string>()));
		SetLoadSetPoint(i, channelNode["setpoint"].as<float>());
		SetLoadCurrentRange(i, channelNode["irange"].as<size_t>());
		SetLoadVoltageRange(i, channelNode["vrange"].as<size_t>());

		SetLoadActive(i, channelNode["enabled"].as<bool>());
	}
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

	Unit volts(Unit::UNIT_VOLTS);
	Unit amps(Unit::UNIT_AMPS);
	Unit watts(Unit::UNIT_WATTS);
	Unit ohms(Unit::UNIT_OHMS);

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_LOAD))
			continue;

		auto chan = dynamic_cast<LoadChannel*>(GetChannel(i));
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		//Warn if turned on
		if(channelNode["enabled"].as<bool>() && !GetLoadActive(i))
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " enable", "Turning load on", "off", "on"));
		}

		//Complain if mode is changed
		auto newMode = channelNode["mode"].as<string>();
		auto curMode = GetLoadMode(i);
		auto mode = GetLoadModeOfName(newMode);
		if(mode != curMode)
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " mode",
				"Changing operating mode",
				GetNameOfLoadMode(curMode),
				newMode));
		}

		//Figure out current unit
		Unit vunit(Unit::UNIT_VOLTS);
		switch(mode)
		{
			case MODE_CONSTANT_CURRENT:
				vunit = amps;
				break;

			case MODE_CONSTANT_VOLTAGE:
				vunit = volts;
				break;

			case MODE_CONSTANT_POWER:
				vunit = watts;
				break;

			case MODE_CONSTANT_RESISTANCE:
				vunit = ohms;
				break;

			default:
				break;
		}

		//Complain if set point is increased
		auto newSet = channelNode["setpoint"].as<float>();
		auto oldSet = GetLoadSetPoint(i);
		if(newSet > oldSet)
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " set point",
				string("Increasing set point by ") + vunit.PrettyPrint(newSet - oldSet),
				vunit.PrettyPrint(oldSet),
				vunit.PrettyPrint(newSet)));
		}

		//Complain if range has changed
		//TODO: only if decreased?
		auto ranges = GetLoadCurrentRanges(i);
		auto newRange = channelNode["irange"].as<size_t>();
		auto curRange = GetLoadCurrentRange(i);
		if(newRange != curRange)
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " current range",
				"Changing full scale range",
				amps.PrettyPrint(ranges[curRange]),
				amps.PrettyPrint(ranges[newRange])));
		}

		ranges = GetLoadVoltageRanges(i);
		newRange = channelNode["vrange"].as<size_t>();
		curRange = GetLoadVoltageRange(i);
		if(newRange != curRange)
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " voltage range",
				"Changing full scale range",
				volts.PrettyPrint(ranges[curRange]),
				volts.PrettyPrint(ranges[newRange])));
		}
	}
}
