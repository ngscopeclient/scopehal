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
	@brief Implementation of MockInstrument

	@ingroup psudrivers
 */

#include "scopehal.h"
#include "MockInstrument.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver
 */
MockInstrument::MockInstrument(const string& name,
	const string& vendor,
	const string& serial,
	const std::string& transport,
	const std::string& driver,
	const std::string& args)
	: SCPIInstrument(nullptr, false)
	, m_name(name)
	, m_vendor(vendor)
	, m_serial(serial)
	, m_transportName(transport)
	, m_driver(driver)
	, m_args(args)
{
	// Use a null transport
	m_transport = new SCPINullTransport(args);

	//Clear warnings after preload for mock instruments
	m_preloaders.push_back(sigc::mem_fun(*this, &MockInstrument::ClearWarnings));

	m_serializers.push_back(sigc::mem_fun(*this, &MockInstrument::DoSerializeConfiguration));
}

MockInstrument::~MockInstrument()
{

}

void MockInstrument::ClearWarnings(int /*version*/, const YAML::Node& node, IDTable& table, ConfigWarningList& warnings)
{	// No warnings necessary for Mock instruments
	warnings.m_warnings.erase(this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

bool MockInstrument::IsOffline()
{
	return true;
}

string MockInstrument::GetTransportName()
{
	return m_transportName;
}

string MockInstrument::GetTransportConnectionString()
{
	return m_args;
}

void MockInstrument::SetTransportConnectionString(const string& args)
{
	m_args = args;
}

string MockInstrument::GetName() const
{
	return m_name;
}

string MockInstrument::GetVendor() const
{
	return m_vendor;
}

string MockInstrument::GetSerial() const
{
	return m_serial;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void MockInstrument::DoSerializeConfiguration(YAML::Node& node, IDTable& /*table*/)
{
	node["transport"] = GetTransportName();
	node["args"] = GetTransportConnectionString();
	node["driver"] = GetDriverName();
}
