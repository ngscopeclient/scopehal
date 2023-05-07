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
	for(auto& s : m_serializers)
		s(node, table);
	return node;
}

void Instrument::LoadConfiguration(int version, const YAML::Node& node, IDTable& idmap)
{
	for(auto& load : m_loaders)
		load(version, node, idmap);
}
