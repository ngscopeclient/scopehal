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
#include "BERT.h"

using namespace std;

BERT::BERT()
{
	m_serializers.push_back(sigc::mem_fun(*this, &BERT::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &BERT::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &BERT::DoPreLoadConfiguration));
}

BERT::~BERT()
{
}


unsigned int BERT::GetInstrumentTypes() const
{
	return INST_BERT;
}

string BERT::GetPatternName(Pattern pat)
{
	switch(pat)
	{
		case PATTERN_PRBS7:
			return "PRBS7";

		case PATTERN_PRBS9:
			return "PRBS9";

		case PATTERN_PRBS11:
			return "PRBS11";

		case PATTERN_PRBS15:
			return "PRBS15";

		case PATTERN_PRBS23:
			return "PRBS23";

		case PATTERN_PRBS31:
			return "PRBS31";

		case PATTERN_CUSTOM:
			return "Custom";

		case PATTERN_AUTO:
			return "Auto";

		default:
			return "invalid";
	}
}

BERT::Pattern BERT::GetPatternOfName(string name)
{
	if(name == "PRBS7")
		return PATTERN_PRBS7;
	else if(name == "PRBS9")
		return PATTERN_PRBS9;
	else if(name == "PRBS11")
		return PATTERN_PRBS11;
	else if(name == "PRBS15")
		return PATTERN_PRBS15;
	else if(name == "PRBS23")
		return PATTERN_PRBS23;
	else if(name == "PRBS31")
		return PATTERN_PRBS31;
	else if(name == "Custom")
		return PATTERN_CUSTOM;
	else if(name == "Auto")
		return PATTERN_AUTO;

	//invalid
	else
		return PATTERN_PRBS7;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void BERT::DoSerializeConfiguration(YAML::Node& node, IDTable& table)
{
	//If we're derived from bert class but not a bert, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_BERT) == 0)
		return;

	YAML::Node chnode = node["channels"];

	//Top level / global config

	YAML::Node customPattern;
	customPattern["isPerChannel"] = IsCustomPatternPerChannel();
	customPattern["length"] = GetCustomPatternLength();
	customPattern["globalPattern"] = GetGlobalCustomPattern();
	node["customPattern"] = customPattern;

	YAML::Node rxCTLE;
	rxCTLE["present"] = HasRxCTLE();
	YAML::Node rxCTLESteps;
	auto steps = GetRxCTLEGainSteps();
	for(auto s : steps)
		rxCTLESteps.push_back(s);
	rxCTLE["steps"] = steps;
	node["rxCTLE"] = rxCTLE;

	node["berIntegrationLength"] = GetBERIntegrationLength();

	YAML::Node refclkOut;
	refclkOut["muxsel"] = GetRefclkOutMux();
	refclkOut["freq"] = GetRefclkOutFrequency();
	auto names = GetRefclkOutMuxNames();
	YAML::Node muxnames;
	for(auto n : names)
		muxnames.push_back(n);
	refclkOut["names"] = muxnames;
	node["refclkOut"] = refclkOut;
	node["refclkInFreq"] = GetRefclkInFrequency();

	YAML::Node timebase;
	timebase["dataRate"] = GetDataRate();
	YAML::Node availableRates;
	auto rates = GetAvailableDataRates();
	for(auto r : rates)
		availableRates.push_back(r);
	timebase["availableRates"] = availableRates;
	timebase["useExtRefclk"] = GetUseExternalRefclk();
	node["timebase"] = timebase;

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_BERT))
			continue;

		auto chan = GetChannel(i);
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		auto ichan = dynamic_cast<BERTInputChannel*>(chan);
		auto ochan = dynamic_cast<BERTOutputChannel*>(chan);

		if(ichan)
		{
			channelNode["bertid"] = table.emplace(ichan);
			channelNode["direction"] = "in";

			channelNode["invert"] = GetRxInvert(i);
			channelNode["cdrlock"] = GetRxCdrLockState(i);
			channelNode["ctleStep"] = GetRxCTLEGainStep(i);
			channelNode["pattern"] = GetPatternName(GetRxPattern(i));

			YAML::Node avail;
			auto patterns = GetAvailableRxPatterns(i);
			for(auto p : patterns)
				avail.push_back(GetPatternName(p));
			channelNode["availablePatterns"] = avail;

			int64_t dx;
			float dy;
			GetBERSamplingPoint(i, dx, dy);
			YAML::Node sampler;
			sampler["dx"] = dx;
			sampler["dy"] = dy;
			sampler["ber"] = ichan->GetBERStream().GetScalarValue();
			channelNode["sampler"] = sampler;
		}
		else
		{
			channelNode["bertid"] = table.emplace(ochan);
			channelNode["direction"] = "out";

			channelNode["pattern"] = GetPatternName(GetTxPattern(i));

			YAML::Node avail;
			auto patterns = GetAvailableTxPatterns(i);
			for(auto p : patterns)
				avail.push_back(GetPatternName(p));
			channelNode["availablePatterns"] = avail;

			channelNode["invert"] = GetTxInvert(i);
			channelNode["drive"] = GetTxDriveStrength(i);

			YAML::Node adrives;
			auto drives = GetAvailableTxDriveStrengths(i);
			for(auto d : drives)
				adrives.push_back(d);
			channelNode["availableDrives"] = adrives;

			channelNode["enabled"] = GetTxEnable(i);
			channelNode["preCursor"] = GetTxPreCursor(i);
			channelNode["postCursor"] = GetTxPostCursor(i);
		}

		node["channels"][key] = channelNode;
	}
}

void BERT::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& idmap)
{
	SetGlobalCustomPattern(node["customPattern"]["globalPattern"].as<uint64_t>());
	SetBERIntegrationLength(node["berIntegrationLength"].as<int>());
	SetRefclkOutMux(node["berIntegrationLength"].as<int>());
	auto timebase = node["timebase"];
	SetUseExternalRefclk(timebase["useExtRefclk"].as<bool>());
	SetDataRate(timebase["dataRate"].as<int64_t>());

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_BERT))
			continue;

		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		auto ichan = dynamic_cast<BERTInputChannel*>(GetChannel(i));
		auto ochan = dynamic_cast<BERTOutputChannel*>(GetChannel(i));

		if(ichan)
		{
			idmap.emplace(channelNode["bertid"].as<intptr_t>(), ichan);

			SetRxInvert(i, channelNode["invert"].as<bool>());
			SetRxCTLEGainStep(i, channelNode["ctleStep"].as<size_t>());
			SetRxPattern(i, GetPatternOfName(channelNode["pattern"].as<string>()));

			auto sampler = channelNode["sampler"];
			SetBERSamplingPoint(i, sampler["dx"].as<int64_t>(), sampler["dy"].as<float>());
		}
		else if(ochan)
		{
			idmap.emplace(channelNode["bertid"].as<intptr_t>(), ochan);

			SetTxPattern(i, GetPatternOfName(channelNode["pattern"].as<string>()));
			SetTxInvert(i, channelNode["invert"].as<bool>());
			SetTxDriveStrength(i, channelNode["drive"].as<float>());
			SetTxEnable(i, channelNode["enabled"].as<bool>());
			SetTxPreCursor(i, channelNode["preCursor"].as<float>());
			SetTxPostCursor(i, channelNode["postCursor"].as<float>());
		}
	}
}

void BERT::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& node,
	IDTable& /*idmap*/,
	ConfigWarningList& list)
{
	//If we're derived from bert class but not a bert, do nothing
	//(we're probably a multi function instrument missing an option)
	if( (GetInstrumentTypes() & Instrument::INST_BERT) == 0)
		return;

	Unit volts(Unit::UNIT_VOLTS);

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_BERT))
			continue;

		auto ichan = dynamic_cast<BERTInputChannel*>(GetChannel(i));
		auto ochan = dynamic_cast<BERTOutputChannel*>(GetChannel(i));
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		if(ichan)
		{
			//nothing on input channel can break things, don't have to check anything
		}
		else if(ochan)
		{
			//complain if output turned on, or level increased

			if(channelNode["enabled"].as<bool>() && !GetTxEnable(i))
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					ochan->GetDisplayName() + " enable", "Turning output on", "off", "on"));
			}

			auto drive = GetTxDriveStrength(i);
			auto ndrive = channelNode["drive"].as<float>();
			if(ndrive > drive)
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					ochan->GetDisplayName() + " output swing",
					string("Increasing drive by ") + volts.PrettyPrint(ndrive - drive),
					volts.PrettyPrint(drive),
					volts.PrettyPrint(ndrive)));
			}
		}
	}
}

