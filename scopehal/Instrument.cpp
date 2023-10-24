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
#include "Instrument.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Instrument::~Instrument()
{
	for(auto c : m_channels)
		delete c;
	m_channels.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel enumeration and identification

/**
	@brief Gets a channel given the display name
 */
InstrumentChannel* Instrument::GetChannelByDisplayName(const string& name)
{
	for(auto c : m_channels)
	{
		if(c->GetDisplayName() == name)
			return c;
	}
	return NULL;
}

/**
	@brief Gets a channel given the hardware name
 */
InstrumentChannel* Instrument::GetChannelByHwName(const string& name)
{
	for(auto c : m_channels)
	{
		if(c->GetHwname() == name)
			return c;
	}
	return NULL;
}

void Instrument::SetChannelDisplayName(size_t /*i*/, string /*name*/)
{
}

string Instrument::GetChannelDisplayName(size_t i)
{
	return m_channels[i]->GetHwname();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

YAML::Node Instrument::SerializeConfiguration(IDTable& table) const
{
	YAML::Node node;

	//Serialize instrument-wide stuff
	node["nick"] = m_nickname;
	node["name"] = GetName();
	node["vendor"] = GetVendor();
	node["serial"] = GetSerial();

	//give us an ID just in case, but i'm not sure how much that gets used
	node["id"] = table.emplace(const_cast<Instrument*>(this));

	//type bitmask, only used for offline loading so we know what the mock instrument should support
	YAML::Node types;
	auto typemask = GetInstrumentTypes();
	if(typemask & INST_OSCILLOSCOPE)
		types.push_back("oscilloscope");
	if(typemask & INST_DMM)
		types.push_back("multimeter");
	if(typemask & INST_PSU)
		types.push_back("psu");
	if(typemask & INST_FUNCTION)
		types.push_back("funcgen");
	if(typemask & INST_RF_GEN)
		types.push_back("rfgen");
	if(typemask & INST_LOAD)
		types.push_back("load");
	if(typemask & INST_BERT)
		types.push_back("bert");
	node["types"] = types;

	//Serialize base channel configuration
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		auto chan = GetChannel(i);
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		//Save basic info
		channelNode["id"] = table.emplace(chan);
		channelNode["index"] = i;
		channelNode["color"] = chan->m_displaycolor;
		channelNode["nick"] = chan->GetDisplayName();
		channelNode["name"] = chan->GetHwname();

		//type bitmask, only used for offline loading so we know what the mock instrument should support
		YAML::Node chtypes;
		typemask = GetInstrumentTypesForChannel(i);
		if(typemask & INST_OSCILLOSCOPE)
			chtypes.push_back("oscilloscope");
		if(typemask & INST_DMM)
			chtypes.push_back("multimeter");
		if(typemask & INST_PSU)
			chtypes.push_back("psu");
		if(typemask & INST_FUNCTION)
			chtypes.push_back("funcgen");
		if(typemask & INST_RF_GEN)
			chtypes.push_back("rfgen");
		if(typemask & INST_LOAD)
			chtypes.push_back("load");
		if(typemask & INST_BERT)
			chtypes.push_back("bert");
		channelNode["types"] = chtypes;

		//Save inputs for the channel as well (may not be fully serialized yet so add to the table if needed)
		//FlowGraphNode::SerializeConfiguration() expects to be the first thing called so we have to tweak a bit
		auto tnode = chan->SerializeConfiguration(table);
		channelNode["inputs"] = tnode["inputs"];
		//no parameters for channels, for now

		node["channels"][key] = channelNode;
	}

	//Call each derived class
	for(auto& s : m_serializers)
		s(node, table);

	return node;
}

void Instrument::LoadConfiguration(int version, const YAML::Node& node, IDTable& idmap)
{
	for(auto& load : m_loaders)
		load(version, node, idmap);
}

void Instrument::PreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& warnings)
{
	//Load channel nickname now to make messages easier to understand
	m_nickname = node["nick"].as<string>();

	for(auto& preload : m_preloaders)
		preload(version, node, idmap, warnings);
}
