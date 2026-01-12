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
	@brief Implementation of Line trigger
	@ingroup triggers
 */

#include "scopehal.h"
#include "RSRTB2kLineTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the trigger

	@param scope	The scope this trigger will be used with
 */
RSRTB2kLineTrigger::RSRTB2kLineTrigger(Oscilloscope* scope)
	: Trigger(scope)
	, m_holdofftimestate(m_parameters["Hold Off"])
	, m_holdofftime(m_parameters["Hold Off Time"])
{
	CreateInput("din");

	//Trigger level
	m_level.MarkHidden();
	m_triggerLevel.MarkHidden();
	m_upperLevel.MarkHidden();

	//Hold off time
	m_holdofftimestate = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_holdofftime = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
}

RSRTB2kLineTrigger::~RSRTB2kLineTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Returns the constant trigger name "Dropout"
string RSRTB2kLineTrigger::GetTriggerName()
{
	return "Line";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool RSRTB2kLineTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
{
	return true;
}


