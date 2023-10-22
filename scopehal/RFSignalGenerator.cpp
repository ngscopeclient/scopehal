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
	m_serializers.push_back(sigc::mem_fun(this, &RFSignalGenerator::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(this, &RFSignalGenerator::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(this, &RFSignalGenerator::DoPreLoadConfiguration));
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

void RFSignalGenerator::DoLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap)
{
}

void RFSignalGenerator::DoPreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& list)
{
}
