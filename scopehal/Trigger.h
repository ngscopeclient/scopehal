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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of Trigger
 */
#ifndef Trigger_h
#define Trigger_h

#include "FlowGraphNode.h"

/**
	@brief Abstract base class for oscilloscope / logic analyzer triggers
 */
class Trigger : public FlowGraphNode
{
public:
	Trigger(Oscilloscope* scope);
	virtual ~Trigger();

	float GetLevel()
	{ return m_parameters[m_levelname].GetFloatVal(); }

	void SetLevel(float level)
	{ m_parameters[m_levelname].SetFloatVal(level); }

	//Conditions for filters
	enum Condition
	{
		CONDITION_EQUAL,
		CONDITION_NOT_EQUAL,
		CONDITION_LESS,
		CONDITION_LESS_OR_EQUAL,
		CONDITION_GREATER,
		CONDITION_GREATER_OR_EQUAL,
		CONDITION_BETWEEN,
		CONDITION_NOT_BETWEEN,
		CONDITION_ANY
	};

protected:
	Oscilloscope* m_scope;
	std::string m_levelname;

public:
	virtual std::string GetTriggerDisplayName() =0;

	typedef Trigger* (*CreateProcType)(Oscilloscope*);
	static void DoAddTriggerClass(std::string name, CreateProcType proc);

	static void EnumTriggers(std::vector<std::string>& names);
	static Trigger* CreateTrigger(std::string name, Oscilloscope* scope);

	/**
		@brief Serializes this trigger's configuration to a YAML string.

		@return YAML block with this trigger's configuration
	 */
	virtual std::string SerializeConfiguration(IDTable& table);

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;
};

#define TRIGGER_INITPROC(T) \
	static Trigger* CreateInstance(Oscilloscope* scope) \
	{ return new T(scope); } \
	virtual std::string GetTriggerDisplayName() \
	{ return GetTriggerName(); }

#define AddTriggerClass(T) Trigger::DoAddTriggerClass(T::GetTriggerName(), T::CreateInstance)

#endif
