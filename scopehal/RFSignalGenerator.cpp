/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RFSignalGenerator::RFSignalGenerator()
{
	m_serializers.push_back(sigc::mem_fun(*this, &RFSignalGenerator::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &RFSignalGenerator::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &RFSignalGenerator::DoPreLoadConfiguration));
}

RFSignalGenerator::~RFSignalGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Placeholder implementations for optional features not all instruments may have

float RFSignalGenerator::GetSweepStartFrequency(int /*chan*/)
{
	return 0;
}

float RFSignalGenerator::GetSweepStopFrequency(int /*chan*/)
{
	return 0;
}

void RFSignalGenerator::SetSweepStartFrequency(int /*chan*/, float /*freq*/)
{
	//no-op in base class
}

void RFSignalGenerator::SetSweepStopFrequency(int /*chan*/, float /*freq*/)
{
	//no-op in base class
}

float RFSignalGenerator::GetSweepStartLevel(int /*chan*/)
{
	return 0;
}

float RFSignalGenerator::GetSweepStopLevel(int /*chan*/)
{
	return 0;
}

void RFSignalGenerator::SetSweepStartLevel(int /*chan*/, float /*level*/)
{
	//no-op in base class
}

void RFSignalGenerator::SetSweepStopLevel(int /*chan*/, float /*level*/)
{
	//no-op in base class
}

float RFSignalGenerator::GetSweepDwellTime(int /*chan*/)
{
	return 0;
}

void RFSignalGenerator::SetSweepDwellTime(int /*chan*/, float /*fs*/)
{
	//no-op in base class
}

void RFSignalGenerator::SetSweepPoints(int /*chan*/, int /*npoints*/)
{
}

int RFSignalGenerator::GetSweepPoints(int /*chan*/)
{
	return 0;
}

RFSignalGenerator::SweepShape RFSignalGenerator::GetSweepShape(int /*chan*/)
{
	return SWEEP_SHAPE_TRIANGLE;
}

void RFSignalGenerator::SetSweepShape(int /*chan*/, SweepShape /*shape*/)
{
	//no-op in base class
}

RFSignalGenerator::SweepSpacing RFSignalGenerator::GetSweepSpacing(int /*chan*/)
{
	return SWEEP_SPACING_LINEAR;
}

void RFSignalGenerator::SetSweepSpacing(int /*chan*/, SweepSpacing /*shape*/)
{
	//no-op in base class
}

RFSignalGenerator::SweepDirection RFSignalGenerator::GetSweepDirection(int /*chan*/)
{
	return SWEEP_DIR_FWD;
}

void RFSignalGenerator::SetSweepDirection(int /*chan*/, SweepDirection /*dir*/)
{
	//no-op in base class
}

RFSignalGenerator::SweepType RFSignalGenerator::GetSweepType(int /*chan*/)
{
	return SWEEP_TYPE_NONE;
}

void RFSignalGenerator::SetSweepType(int /*chan*/, SweepType /*type*/)
{
	//no-op in base class
}

bool RFSignalGenerator::AcquireData()
{
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto pchan = dynamic_cast<RFSignalGeneratorChannel*>(m_channels[i]);
		if(!pchan)
			continue;

		pchan->SetScalarValue(RFSignalGeneratorChannel::STREAM_FREQUENCY, GetChannelCenterFrequency(i));
		pchan->SetScalarValue(RFSignalGeneratorChannel::STREAM_LEVEL, GetChannelOutputPower(i));
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string RFSignalGenerator::GetNameOfSweepType(SweepType type)
{
	switch(type)
	{
		case SWEEP_TYPE_NONE:
			return "None";

		case SWEEP_TYPE_FREQ:
			return "Frequency";

		case SWEEP_TYPE_LEVEL:
			return "Level";

		case SWEEP_TYPE_FREQ_LEVEL:
			return "Frequency + level";

		default:
			return "invalid";
	}
}

RFSignalGenerator::SweepType RFSignalGenerator::GetSweepTypeOfName(const string& name)
{
	if(name == "None")
		return SWEEP_TYPE_NONE;
	else if(name == "Frequency")
		return SWEEP_TYPE_FREQ;
	else if(name == "Level")
		return SWEEP_TYPE_LEVEL;
	else if(name == "Frequency + level")
		return SWEEP_TYPE_FREQ_LEVEL;

	else
		return SWEEP_TYPE_NONE;
}

string RFSignalGenerator::GetNameOfSweepShape(SweepShape shape)
{
	switch(shape)
	{
		case SWEEP_SHAPE_TRIANGLE:
			return "Triangle";

		case SWEEP_SHAPE_SAWTOOTH:
			return "Sawtooth";

		default:
			return "invalid";
	}
}

RFSignalGenerator::SweepShape RFSignalGenerator::GetSweepShapeOfName(const string& name)
{
	if(name == "Triangle")
		return SWEEP_SHAPE_TRIANGLE;
	else if(name == "Sawtooth")
		return SWEEP_SHAPE_SAWTOOTH;

	//invalid
	else
		return SWEEP_SHAPE_TRIANGLE;
}

string RFSignalGenerator::GetNameOfSweepSpacing(SweepSpacing spacing)
{
	switch(spacing)
	{
		case SWEEP_SPACING_LINEAR:
			return "Linear";

		case SWEEP_SPACING_LOG:
			return "Log";

		default:
			return "invalid";
	}
}

RFSignalGenerator::SweepSpacing RFSignalGenerator::GetSweepSpacingOfName(const string& name)
{
	if(name == "Linear")
		return SWEEP_SPACING_LINEAR;
	else if(name == "Log")
		return SWEEP_SPACING_LOG;

	//invalid
	else
		return SWEEP_SPACING_LINEAR;
}

string RFSignalGenerator::GetNameOfSweepDirection(SweepDirection dir)
{
	switch(dir)
	{
		case SWEEP_DIR_FWD:
			return "Forward";

		case SWEEP_DIR_REV:
			return "Reverse";

		default:
			return "invalid";
	}
}

RFSignalGenerator::SweepDirection RFSignalGenerator::GetSweepDirectionOfName(const string& name)
{
	if(name == "Forward")
		return SWEEP_DIR_FWD;
	else if(name == "Reverse")
		return SWEEP_DIR_REV;

	//invalid
	else
		return SWEEP_DIR_FWD;
}

void RFSignalGenerator::DoSerializeConfiguration(YAML::Node& node, IDTable& table)
{
	//Global capabilities
	//For now, just wave shapes for analog FM
	auto waveshapes = GetAnalogFMWaveShapes();
	YAML::Node wshapes;
	for(auto shape : waveshapes)
		wshapes.push_back(FunctionGenerator::GetNameOfShape(shape));
	node["analogfmwaveshapes"] = wshapes;

	//All other capabilities etc are per channel

	//Channel configuration
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_RF_GEN))
			continue;

		auto chan = dynamic_cast<RFSignalGeneratorChannel*>(GetChannel(i));

		//Save basic info
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];
		channelNode["rfgenid"] = table.emplace(chan);

		//Common config every RF gen channel has
		channelNode["enabled"] = GetChannelOutputEnable(i);
		channelNode["power"] = GetChannelOutputPower(i);
		channelNode["centerfreq"] = GetChannelCenterFrequency(i);

		if(IsAnalogModulationAvailable(i))
		{
			YAML::Node anode;

			anode["enabled"] = GetAnalogModulationEnable(i);

			YAML::Node fmnode;
			fmnode["enabled"] = GetAnalogFMEnable(i);
			fmnode["shape"] = FunctionGenerator::GetNameOfShape(GetAnalogFMWaveShape(i));
			fmnode["deviation"] = GetAnalogFMDeviation(i);
			fmnode["frequency"] = GetAnalogFMFrequency(i);

			anode["fm"] = fmnode;

			channelNode["analogMod"] = anode;
		}

		if(IsVectorModulationAvailable(i))
		{
			YAML::Node vnode;

			channelNode["vectorMod"] = vnode;
		}

		if(IsSweepAvailable(i))
		{
			YAML::Node snode;

			snode["type"] = GetNameOfSweepType(GetSweepType(i));
			snode["startfreq"] = GetSweepStartFrequency(i);
			snode["stopfreq"] = GetSweepStopFrequency(i);
			snode["startlevel"] = GetSweepStartLevel(i);
			snode["stoplevel"] = GetSweepStopLevel(i);
			snode["dwell"] = GetSweepDwellTime(i);
			snode["points"] = GetSweepPoints(i);
			snode["shape"] = GetNameOfSweepShape(GetSweepShape(i));
			snode["spacing"] = GetNameOfSweepSpacing(GetSweepSpacing(i));
			snode["direction"] = GetNameOfSweepDirection(GetSweepDirection(i));

			channelNode["sweep"] = snode;
		}

		node["channels"][key] = channelNode;
	}
}

void RFSignalGenerator::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& node,
	IDTable& /*idmap*/,
	ConfigWarningList& list)
{
	//Ignore analogfmwaveshapes, that's only important for offline

	Unit db(Unit::UNIT_DB);
	Unit dbm(Unit::UNIT_DBM);

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_RF_GEN))
			continue;

		auto chan = dynamic_cast<RFSignalGeneratorChannel*>(GetChannel(i));
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		//Warn if turned on
		if(channelNode["enabled"] && !GetChannelOutputEnable(i))
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName(), "Turning RF power on", "off", "on"));
		}

		//Complain if power level is increased
		float pact = GetChannelOutputPower(i);
		float pnom = channelNode["power"].as<float>();
		if(pnom > pact)
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " output power",
				string("Increasing output level by ") + db.PrettyPrint(pnom - pact),
				dbm.PrettyPrint(pact),
				dbm.PrettyPrint(pnom)));
		}

		//If we have sweep capability, complain if sweep power is increased
		auto snode = channelNode["sweep"];
		if(IsSweepAvailable(i) && snode)
		{
			//Complain if enabling power sweep
			auto sweepType = GetSweepType(i);
			if( (sweepType == SWEEP_TYPE_LEVEL) || (sweepType == SWEEP_TYPE_FREQ_LEVEL) )
			{
				//already doing power sweep
			}
			else if(snode["type"].as<string>().find("evel") != string::npos)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName() + " sweep mode",
					"Enabling level sweep",
					GetNameOfSweepType(GetSweepType(i)),
					snode["type"].as<string>()));
			}

			//Check if increasing sweep power levels
			float bact = GetSweepStartLevel(i);
			float bnom = snode["startlevel"].as<float>();

			float eact = GetSweepStopLevel(i);
			float enom = snode["stoplevel"].as<float>();

			if(bnom > bact)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName() + " power sweep start",
					string("Increasing sweep start level by ") + db.PrettyPrint(bnom - bact),
					dbm.PrettyPrint(bact),
					dbm.PrettyPrint(bnom)));
			}

			if(enom > eact)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName() + " power sweep stop",
					string("Increasing sweep stop level by ") + db.PrettyPrint(enom - eact),
					dbm.PrettyPrint(eact),
					dbm.PrettyPrint(enom)));
			}
		}
	}
}

void RFSignalGenerator::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& idmap)
{
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_RF_GEN))
			continue;

		auto chan = dynamic_cast<RFSignalGeneratorChannel*>(GetChannel(i));
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];
		idmap.emplace(channelNode["rfgenid"].as<intptr_t>(), chan);

		SetChannelOutputPower(i, channelNode["power"].as<float>());
		SetChannelCenterFrequency(i, channelNode["centerfreq"].as<double>());
		SetChannelOutputEnable(i, channelNode["enabled"].as<bool>());

		YAML::Node anode = channelNode["analogMod"];
		if(IsAnalogModulationAvailable(i) && anode)
		{
			SetAnalogModulationEnable(i, anode["enabled"].as<bool>());

			YAML::Node fmnode = anode["fm"];
			if(fmnode)
			{
				SetAnalogFMEnable(i, fmnode["enabled"].as<bool>());

				SetAnalogFMDeviation(i, fmnode["deviation"].as<float>());
				SetAnalogFMFrequency(i, fmnode["frequency"].as<int64_t>());
				SetAnalogFMWaveShape(i, FunctionGenerator::GetShapeOfName(fmnode["shape"].as<string>()));
			}
		}

		YAML::Node vnode = channelNode["vectorMod"];
		if(IsVectorModulationAvailable(i) && vnode)
		{
			//TODO
		}

		YAML::Node snode = channelNode["sweep"];
		if(IsSweepAvailable(i) && snode)
		{
			SetSweepType(i, GetSweepTypeOfName(snode["type"].as<string>()));
			SetSweepStartFrequency(i, snode["startfreq"].as<float>());
			SetSweepStopFrequency(i, snode["stopfreq"].as<float>());
			SetSweepStartLevel(i, snode["startlevel"].as<float>());
			SetSweepStopLevel(i, snode["stoplevel"].as<float>());
			SetSweepDwellTime(i, snode["dwell"].as<float>());
			SetSweepPoints(i, snode["points"].as<int>());
			SetSweepShape(i, GetSweepShapeOfName(snode["shape"].as<string>()));
			SetSweepSpacing(i, GetSweepSpacingOfName(snode["spacing"].as<string>()));
			SetSweepDirection(i, GetSweepDirectionOfName(snode["direction"].as<string>()));
		}

	}
}
