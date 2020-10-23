/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
#include "RuntTrigger.h"
#include "LeCroyOscilloscope.h"
#include "TektronixOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RuntTrigger::RuntTrigger(Oscilloscope* scope)
	: TwoLevelTrigger(scope)
	, m_conditionname("Condition")
	, m_lowerintname("Lower Interval")
	, m_upperintname("Upper Interval")
{
	CreateInput("din");

	//These conditions are supported by all known scopes with a runt trigger
	m_parameters[m_conditionname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_conditionname].AddEnumValue("Less than", CONDITION_LESS);
	m_parameters[m_conditionname].AddEnumValue("Greater than", CONDITION_GREATER);

	//Standard edge slopes everyone supports
	m_slopename = "Edge Slope";
	m_parameters[m_slopename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_slopename].AddEnumValue("Rising", EDGE_RISING);
	m_parameters[m_slopename].AddEnumValue("Falling", EDGE_FALLING);

	//LeCroy scopes support both min and max limits, so we can specify range operators
	if(dynamic_cast<LeCroyOscilloscope*>(scope))
	{
		m_parameters[m_upperintname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_PS));

		m_parameters[m_conditionname].AddEnumValue("Between", CONDITION_BETWEEN);
		m_parameters[m_conditionname].AddEnumValue("Not between", CONDITION_NOT_BETWEEN);
	}

	//Tek scopes support equal with unspecified tolerance, either-edge pulses, and "ignore width",
	//but only a single pulse width target.
	if(dynamic_cast<TektronixOscilloscope*>(scope))
	{
		m_lowerintname = "Pulse Width";

		m_parameters[m_conditionname].AddEnumValue("Equal", CONDITION_EQUAL);
		m_parameters[m_conditionname].AddEnumValue("Not equal", CONDITION_NOT_EQUAL);
		m_parameters[m_conditionname].AddEnumValue("Occurs", CONDITION_ANY);

		m_parameters[m_slopename].AddEnumValue("Any", EDGE_ANY);
	}

	m_parameters[m_lowerintname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_PS));
}

RuntTrigger::~RuntTrigger()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string RuntTrigger::GetTriggerName()
{
	return "Runt";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool RuntTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//We only can take one input
	if(i > 0)
		return false;

	//There has to be a signal to trigger on
	if(stream.m_channel == NULL)
		return false;

	//It has to be from the same instrument we're trying to trigger on
	if(stream.m_channel->GetScope() != m_scope)
		return false;

	//It has to be analog or external trigger, digital inputs make no sense
	if( (stream.m_channel->GetType() != OscilloscopeChannel::CHANNEL_TYPE_ANALOG) &&
		(stream.m_channel->GetType() != OscilloscopeChannel::CHANNEL_TYPE_TRIGGER) )
	{
		return false;
	}

	return true;
}
