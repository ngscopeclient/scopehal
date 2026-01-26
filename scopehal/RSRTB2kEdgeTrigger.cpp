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
	@brief Implementation of EdgeTrigger
	@ingroup triggers
 */

#include "scopehal.h"
#include "RSRTB2kEdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize a new edge trigger

	@param scope	The scope this trigger will be used on
 */
RSRTB2kEdgeTrigger::RSRTB2kEdgeTrigger(Oscilloscope* scope)
	: Trigger(scope)
	, m_edgetype(m_parameters["Slope"])
	, m_couplingtype(m_parameters["Coupling"])
	, m_hfrejectstate(m_parameters["Reject HF"])
	, m_noiserejectstate(m_parameters["Reject Noise"])
	, m_holdofftimestate(m_parameters["Hold Off"])
	, m_holdofftime(m_parameters["Hold Off Time"])
{
	CreateInput("din");

	//Trigger level
	m_level.MarkHidden();
	m_triggerLevel.MarkHidden(false);
	m_upperLevel.MarkHidden();

	//Slope
	m_edgetype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_edgetype.AddEnumValue("Rising", EDGE_RISING);
	m_edgetype.AddEnumValue("Falling", EDGE_FALLING);
	m_edgetype.AddEnumValue("Any", EDGE_ANY);

	//Trigger coupling
	m_couplingtype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_couplingtype.AddEnumValue("DC", COUPLING_DC);
	m_couplingtype.AddEnumValue("AC", COUPLING_AC);
	m_couplingtype.AddEnumValue("LF Reject", COUPLING_LFREJECT);

	//HF and noise reject
	m_hfrejectstate = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_noiserejectstate = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));

	//Hold off time
	m_holdofftimestate = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_holdofftime = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
}

RSRTB2kEdgeTrigger::~RSRTB2kEdgeTrigger()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant trigger name "edge"
string RSRTB2kEdgeTrigger::GetTriggerName()
{
	return "Edge";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool RSRTB2kEdgeTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//We only can take one input
	if(i > 0)
		return false;

	//Has to be non null
	if(!stream.m_channel)
		return false;

	//Has to be a scope or digital input / IO channel
	auto schan = dynamic_cast<OscilloscopeChannel*>(stream.m_channel);
	auto di = dynamic_cast<DigitalInputChannel*>(stream.m_channel);
	auto dio = dynamic_cast<DigitalIOChannel*>(stream.m_channel);
	if(!schan && !di && !dio)
		return false;

	//It has to be from the same instrument we're trying to trigger on
	if(stream.m_channel->GetInstrument() != m_scope)
		return false;

	return true;
}
