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
	@brief Implementation of UartTrigger
	@ingroup triggers
 */

#include "scopehal.h"
#include "UartTrigger.h"
#include "SiglentSCPIOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Creates a new UART trigger

	@param scope	Scope to create the trigger for
 */
UartTrigger::UartTrigger(Oscilloscope* scope)
	: SerialTrigger(scope)
{
	CreateInput("din");

	m_baudname = "Bit Rate";
	m_parameters[m_baudname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));

	m_ptypename = "Parity Mode";
	m_parameters[m_ptypename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_ptypename].AddEnumValue("None", PARITY_NONE);
	m_parameters[m_ptypename].AddEnumValue("Even", PARITY_EVEN);
	m_parameters[m_ptypename].AddEnumValue("Odd", PARITY_ODD);

	//Constant 0/1 parity bits are not supported by some scopes as they're pretty rare
	if(dynamic_cast<SiglentSCPIOscilloscope*>(scope) != NULL)
	{
		m_parameters[m_ptypename].AddEnumValue("Mark", PARITY_MARK);
		m_parameters[m_ptypename].AddEnumValue("Space", PARITY_SPACE);
	}

	m_typename = "Trigger Type";
	m_parameters[m_typename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_typename].AddEnumValue("Data", TYPE_DATA);
	m_parameters[m_typename].AddEnumValue("Parity error", TYPE_PARITY_ERR);

	m_stopname = "Stop Bits";
	m_parameters[m_stopname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_UI));

	m_polarname = "Polarity";
	m_parameters[m_polarname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_polarname].AddEnumValue("Idle High", IDLE_HIGH);
	m_parameters[m_polarname].AddEnumValue("Idle Low", IDLE_LOW);
}

UartTrigger::~UartTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Returns the trigger name "UART"
string UartTrigger::GetTriggerName()
{
	return "UART";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool UartTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
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

	return true;
}
