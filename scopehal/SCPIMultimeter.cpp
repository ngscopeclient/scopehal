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

using namespace std;

SCPIMultimeter::MeterCreateMapType SCPIMultimeter::m_metercreateprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIMultimeter::SCPIMultimeter()
{
	m_serializers.push_back(sigc::mem_fun(this, &SCPIMultimeter::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(this, &SCPIMultimeter::DoLoadConfiguration));
}

SCPIMultimeter::~SCPIMultimeter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void SCPIMultimeter::DoAddDriverClass(string name, MeterCreateProcType proc)
{
	m_metercreateprocs[name] = proc;
}

void SCPIMultimeter::EnumDrivers(vector<string>& names)
{
	for(auto it=m_metercreateprocs.begin(); it != m_metercreateprocs.end(); ++it)
		names.push_back(it->first);
}

SCPIMultimeter* SCPIMultimeter::CreateMultimeter(string driver, SCPITransport* transport)
{
	if(m_metercreateprocs.find(driver) != m_metercreateprocs.end())
		return m_metercreateprocs[driver](transport);

	LogError("Invalid multimeter driver name \"%s\"\n", driver.c_str());
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void SCPIMultimeter::DoSerializeConfiguration(YAML::Node& node, IDTable& table)
{
	node["nick"] = m_nickname;
	node["name"] = GetName();
	node["vendor"] = GetVendor();
	node["serial"] = GetSerial();
	node["transport"] = GetTransportName();
	node["args"] = GetTransportConnectionString();
	node["driver"] = GetDriverName();
}

void SCPIMultimeter::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& idmap)
{
	m_nickname = node["nick"].as<string>();
}
