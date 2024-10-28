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
	@brief Declaration of Trigger
	@ingroup core
 */
#ifndef Trigger_h
#define Trigger_h

#include "FlowGraphNode.h"

/**
	@brief Abstract base class for oscilloscope / logic analyzer trigger inputs
	@ingroup core
 */
class Trigger : public FlowGraphNode
{
public:
	Trigger(Oscilloscope* scope);
	virtual ~Trigger();

	///@brief Get the trigger level
	float GetLevel()
	{ return m_level.GetFloatVal(); }

	/**
		@brief Sets the trigger level

		@param level	Trigger level
	 */
	void SetLevel(float level)
	{ m_level.SetFloatVal(level); }

	///@brief Gets the scope this trigger is attached to
	Oscilloscope* GetScope()
	{ return m_scope; }

	///@brief Conditions for triggers that perform logical comparisons of values
	enum Condition
	{
		///@brief Match when value is equal to target
		CONDITION_EQUAL,

		///@brief Match when value is not equal to target
		CONDITION_NOT_EQUAL,

		///@brief Match when value is less than target
		CONDITION_LESS,

		///@brief Match when value is less than or equal to target
		CONDITION_LESS_OR_EQUAL,

		///@brief Match when value is greater than target
		CONDITION_GREATER,

		///@brief Match when value is greater than or equal to target
		CONDITION_GREATER_OR_EQUAL,

		///@brief Match when value is greater than one target but less than another
		CONDITION_BETWEEN,

		///@brief Match when value is not between two targets
		CONDITION_NOT_BETWEEN,

		///@brief Always match
		CONDITION_ANY
	};

protected:

	///@brief The scope this trigger is part of
	Oscilloscope* m_scope;

	///@brief "Trigger level" parameter
	FilterParameter& m_level;

public:
	virtual std::string GetTriggerDisplayName() =0;

	typedef Trigger* (*CreateProcType)(Oscilloscope*);
	static void DoAddTriggerClass(std::string name, CreateProcType proc);

	static void EnumTriggers(std::vector<std::string>& names);
	static Trigger* CreateTrigger(std::string name, Oscilloscope* scope);

	virtual YAML::Node SerializeConfiguration(IDTable& table) override;

protected:
	///@brief Helper typedef for m_createprocs
	typedef std::map< std::string, CreateProcType > CreateMapType;

	///@brief Map of trigger type names to factory methods
	static CreateMapType m_createprocs;
};

#define TRIGGER_INITPROC(T) \
	static Trigger* CreateInstance(Oscilloscope* scope) \
	{ return new T(scope); } \
	virtual std::string GetTriggerDisplayName() override \
	{ return GetTriggerName(); }

#define AddTriggerClass(T) Trigger::DoAddTriggerClass(T::GetTriggerName(), T::CreateInstance)

#endif
