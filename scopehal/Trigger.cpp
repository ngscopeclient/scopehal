/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@author Andrew D. Zonenberg
	@brief Implementation of Trigger
	@ingroup core
 */

#include "scopehal.h"

using namespace std;

Trigger::CreateMapType Trigger::m_createprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize a new trigger

	@param scope	The scope this trigger is attached to
 */
Trigger::Trigger(Oscilloscope* scope)
	: m_scope(scope)
	, m_level(m_parameters["Lower Level"])
{
	m_level = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
}

Trigger::~Trigger()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

/**
	@brief Register a new trigger class for dynamic creation

	Do not call this function directly, use the AddTriggerClass macro

	@param name		Name of the trigger class
	@param proc		Factory method
 */
void Trigger::DoAddTriggerClass(string name, CreateProcType proc)
{
	m_createprocs[name] = proc;
}

/**
	@brief Gets a list of all registered trigger types

	@param[out] names	List of known triggers
 */
void Trigger::EnumTriggers(vector<string>& names)
{
	for(CreateMapType::iterator it=m_createprocs.begin(); it != m_createprocs.end(); ++it)
		names.push_back(it->first);
}

/**
	@brief	Creates a new trigger for an oscilloscope

	@param name		Name of the desired trigger
	@param scope	The scope to create the trigger for

	@return The newly created trigger, or nullptr on failure
 */
Trigger* Trigger::CreateTrigger(string name, Oscilloscope* scope)
{
	if(m_createprocs.find(name) != m_createprocs.end())
		return m_createprocs[name](scope);

	LogError("Invalid trigger name: %s\n", name.c_str());
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

YAML::Node Trigger::SerializeConfiguration(IDTable& table)
{
	int id = table.emplace(this);
	YAML::Node node = FlowGraphNode::SerializeConfiguration(table);
	node["id"] = id;
	node["type"] = GetTriggerDisplayName();
	return node;
}
