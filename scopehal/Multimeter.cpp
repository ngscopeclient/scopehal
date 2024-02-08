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
#include "Multimeter.h"

using namespace std;

Multimeter::Multimeter()
{
	m_serializers.push_back(sigc::mem_fun(*this, &Multimeter::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &Multimeter::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &Multimeter::DoPreLoadConfiguration));
}

Multimeter::~Multimeter()
{
}

Unit Multimeter::GetMeterUnit()
{
	switch(GetMeterMode())
	{
		case Multimeter::FREQUENCY:
			return Unit(Unit::UNIT_HZ);

		case Multimeter::TEMPERATURE:
			return Unit(Unit::UNIT_CELSIUS);

		case Multimeter::DC_CURRENT:
		case Multimeter::AC_CURRENT:
			return Unit(Unit::UNIT_AMPS);

		case Multimeter::DC_VOLTAGE:
		case Multimeter::DC_RMS_AMPLITUDE:
		case Multimeter::AC_RMS_AMPLITUDE:
		default:
			return Unit(Unit::UNIT_VOLTS);
	}
}

Unit Multimeter::GetSecondaryMeterUnit()
{
	switch(GetSecondaryMeterMode())
	{
		case Multimeter::FREQUENCY:
			return Unit(Unit::UNIT_HZ);

		case Multimeter::TEMPERATURE:
			return Unit(Unit::UNIT_CELSIUS);

		case Multimeter::DC_CURRENT:
		case Multimeter::AC_CURRENT:
			return Unit(Unit::UNIT_AMPS);

		case Multimeter::DC_VOLTAGE:
		case Multimeter::DC_RMS_AMPLITUDE:
		case Multimeter::AC_RMS_AMPLITUDE:
		default:
			return Unit(Unit::UNIT_VOLTS);
	}
}

/**
	@brief Converts a meter mode to human readable text
 */
string Multimeter::ModeToText(MeasurementTypes type)
{
	switch(type)
	{
		case Multimeter::FREQUENCY:
			return "Frequency";
		case Multimeter::TEMPERATURE:
			return "Temperature";
		case Multimeter::DC_CURRENT:
			return "DC Current";
		case Multimeter::AC_CURRENT:
			return "AC Current";
		case Multimeter::DC_VOLTAGE:
			return "DC Voltage";
		case Multimeter::DC_RMS_AMPLITUDE:
			return "DC RMS Amplitude";
		case Multimeter::AC_RMS_AMPLITUDE:
			return "AC RMS Amplitude";

		default:
			return "";
	}
}

/**
	@brief Converts a textual meter mode to a mode ID
 */
Multimeter::MeasurementTypes Multimeter::TextToMode(const string& mode)
{
	if(mode == "Frequency")
		return Multimeter::FREQUENCY;
	else if(mode == "Temperature")
		return Multimeter::TEMPERATURE;
	else if(mode == "DC Current")
		return Multimeter::DC_CURRENT;
	else if(mode == "AC Current")
		return Multimeter::AC_CURRENT;
	else if(mode == "DC Voltage")
		return Multimeter::DC_VOLTAGE;
	else if(mode == "DC RMS Amplitude")
		return Multimeter::DC_RMS_AMPLITUDE;
	else if(mode == "AC RMS Amplitude")
		return Multimeter::AC_RMS_AMPLITUDE;

	//invalid / unknown
	return Multimeter::DC_VOLTAGE;
}

/**
	@brief Gets a bitmask of secondary measurement types currently available.

	The return value may change depending on the current primary measurement type.
 */
unsigned int Multimeter::GetSecondaryMeasurementTypes()
{
	//default to no secondary measurements
	return NONE;
}

/**
	@brief Gets the active secondary mode
 */
Multimeter::MeasurementTypes Multimeter::GetSecondaryMeterMode()
{
	//default to no measurement
	return NONE;
}

/**
	@brief Sets the active secondary mode
 */
void Multimeter::SetSecondaryMeterMode(MeasurementTypes /*type*/)
{
	//nothing to do
}

double Multimeter::GetSecondaryMeterValue()
{
	return 0;
}

/**
	@brief Pull meter readings from hardware
 */
bool Multimeter::AcquireData()
{
	auto chan = dynamic_cast<MultimeterChannel*>(GetChannel(GetCurrentMeterChannel()));
	if(chan)
		chan->Update(this);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void Multimeter::DoSerializeConfiguration(YAML::Node& node, IDTable& table)
{
	//If we're derived from multimeter class but not a meter, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_DMM) == 0)
		return;

	node["measurementTypes"] = GetMeasurementTypes();
	node["secondaryMeasurementTypes"] = GetSecondaryMeasurementTypes();
	node["currentChannel"] = GetCurrentMeterChannel();
	node["meterMode"] = ModeToText(GetMeterMode());
	node["secondaryMode"] = ModeToText(GetSecondaryMeterMode());
	node["autoRange" ] = GetMeterAutoRange();
	node["unit"] = GetMeterUnit().ToString();
	node["secondaryUnit"] = GetSecondaryMeterUnit().ToString();
	node["value"] = GetMeterUnit().PrettyPrint(GetMeterValue());
	node["secondaryValue"] = GetSecondaryMeterUnit().PrettyPrint(GetSecondaryMeterValue());
	node["digits"] = GetMeterDigits();

	//TODO: ranges

	YAML::Node chnode = node["channels"];

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_DMM))
			continue;

		auto chan = GetChannel(i);
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		//Save basic info
		channelNode["meterid"] = table.emplace(chan);
		node["channels"][key] = channelNode;
	}
}

void Multimeter::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& /*idmap*/)
{
	//If we're derived from multimeter class but not a meter, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_DMM) == 0)
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
}

void Multimeter::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& node,
	IDTable& idmap,
	ConfigWarningList& list)
{
	//If we're derived from multimeter class but not a meter, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_DMM) == 0)
		return;

	//Complain if mode is changed
	auto mode = TextToMode(node["meterMode"].as<string>());
	if(mode != GetMeterMode())
	{
		list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
			"Operating mode",
			"Changing meter mode",
			ModeToText(GetMeterMode()),
			node["meterMode"].as<string>()));
	}

	//Set channel IDs
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_DMM))
			continue;

		auto chan = dynamic_cast<MultimeterChannel*>(GetChannel(i));

		//Save basic info
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		//Set our ID
		idmap.emplace(channelNode["meterid"].as<int>(), chan);
	}

}
