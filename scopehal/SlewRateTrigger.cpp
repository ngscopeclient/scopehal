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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of SlewRateTrigger
	@ingroup triggers
 */

#include "scopehal.h"
#include "SlewRateTrigger.h"
#include "LeCroyOscilloscope.h"
#include "TektronixOscilloscope.h"
#include "SiglentSCPIOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the trigger

	@param scope	The scope this trigger will be used with
 */
SlewRateTrigger::SlewRateTrigger(Oscilloscope* scope)
	: TwoLevelTrigger(scope)
	, m_condition(m_parameters["Condition"])
	, m_lowerInterval(m_parameters["Lower Interval"])
	, m_upperInterval(m_parameters["Upper Interval"])
	, m_slope(m_parameters["Edge Slope"])
{
	CreateInput("in");

	m_condition = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_condition.AddEnumValue("Less than", CONDITION_LESS);
	m_condition.AddEnumValue("Greater than", CONDITION_GREATER);

	m_lowerInterval = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_upperInterval = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));

	m_slope = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_slope.AddEnumValue("Rising", EDGE_RISING);
	m_slope.AddEnumValue("Falling", EDGE_FALLING);

	//Make/model specific options
	if((dynamic_cast<LeCroyOscilloscope*>(scope) != nullptr) ||
		(dynamic_cast<SiglentSCPIOscilloscope*>(scope) != nullptr))
	{
		m_condition.AddEnumValue("Between", CONDITION_BETWEEN);
		m_condition.AddEnumValue("Not between", CONDITION_NOT_BETWEEN);
	}

	else if(dynamic_cast<TektronixOscilloscope*>(scope) != nullptr)
	{
		m_slope.AddEnumValue("Any", EDGE_ANY);

		m_condition.AddEnumValue("Equal", CONDITION_EQUAL);
		m_condition.AddEnumValue("Not equal", CONDITION_NOT_EQUAL);

		m_upperInterval.MarkHidden();
	}

	else
	{
		m_upperInterval.MarkHidden();
	}
}

SlewRateTrigger::~SlewRateTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant trigger name "Slew Rate"
string SlewRateTrigger::GetTriggerName()
{
	return "Slew Rate";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool SlewRateTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//We only can take one input
	if(i > 0)
		return false;

	//There has to be a signal to trigger on
	auto schan = dynamic_cast<OscilloscopeChannel*>(stream.m_channel);
	if(!schan)
		return false;

	//It has to be from the same instrument we're trying to trigger on
	if(schan->GetScope() != m_scope)
		return false;

	//It has to be analog or external trigger, digital inputs make no sense
	if( (stream.GetType() != Stream::STREAM_TYPE_ANALOG) && (stream.GetType() != Stream::STREAM_TYPE_TRIGGER) )
		return false;

	return true;
}
