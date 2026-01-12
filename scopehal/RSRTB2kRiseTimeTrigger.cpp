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
#include "RSRTB2kRiseTimeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the trigger

	@param scope	The scope this trigger will be used with
 */
RSRTB2kRiseTimeTrigger::RSRTB2kRiseTimeTrigger(Oscilloscope* scope)
	: Trigger(scope)
	, m_edgetype(m_parameters["Polarity"])
	, m_conditiontype(m_parameters["Condition"])
	, m_risetime(m_parameters["Time"])
	, m_risetimevariation(m_parameters["Time Variation"])
	, m_lowerlevel(m_parameters["Level Lower"])
	, m_holdofftimestate(m_parameters["Hold Off"])
	, m_holdofftime(m_parameters["Hold Off Time"])
	, m_hysteresistype(m_parameters["Hysteresis"])
{
	CreateInput("din");

	//Trigger level
	m_level.MarkHidden();
	m_triggerLevel.MarkHidden();
	m_upperLevel.MarkHidden(false);

	//Polarity
	m_edgetype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_edgetype.AddEnumValue("Positive", EDGE_RISING);
	m_edgetype.AddEnumValue("Negative", EDGE_FALLING);
	//The Either parameter are not implemented in firmware v3.000.
	//There is no response when queried.
	//~ m_edgetype.AddEnumValue("Either", EDGE_ANY);

	//Comparsion
	m_conditiontype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_conditiontype.AddEnumValue("Less than", CONDITION_LESS);
	m_conditiontype.AddEnumValue("Greater than", CONDITION_GREATER);
	m_conditiontype.AddEnumValue("Equal", CONDITION_EQUAL);
	m_conditiontype.AddEnumValue("Not equal", CONDITION_NOT_EQUAL);

	//Time and Variation
	m_risetime = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_risetimevariation = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));

	//Upper (Trigger Level) and Lower Level
	m_lowerlevel = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));

	//Hold off time
	m_holdofftimestate = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_holdofftime = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));

	//Hysteresis
	m_hysteresistype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_hysteresistype.AddEnumValue("Small", HYSTERESIS_SMALL);
	m_hysteresistype.AddEnumValue("Medium", HYSTERESIS_MEDIUM);
	m_hysteresistype.AddEnumValue("Large", HYSTERESIS_LARGE);
}

RSRTB2kRiseTimeTrigger::~RSRTB2kRiseTimeTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant trigger name "Risetime"
string RSRTB2kRiseTimeTrigger::GetTriggerName()
{
	return "Risetime";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool RSRTB2kRiseTimeTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
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
