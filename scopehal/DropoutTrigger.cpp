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
#include "DropoutTrigger.h"
#include "LeCroyOscilloscope.h"
#include "TektronixOscilloscope.h"
#include "SiglentSCPIOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DropoutTrigger::DropoutTrigger(Oscilloscope* scope) : Trigger(scope)
{
	CreateInput("din");

	m_typename = "Edge";
	m_parameters[m_typename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_typename].AddEnumValue("Rising", EDGE_RISING);
	m_parameters[m_typename].AddEnumValue("Falling", EDGE_FALLING);
	if(dynamic_cast<TektronixOscilloscope*>(scope))
		m_parameters[m_typename].AddEnumValue("Any", EDGE_ANY);

	m_timename = "Dropout Time";
	m_parameters[m_timename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));

	m_resetname = "Reset Mode";
	if((dynamic_cast<LeCroyOscilloscope*>(scope)) || (dynamic_cast<SiglentSCPIOscilloscope*>(scope)))
	{
		m_parameters[m_resetname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
		m_parameters[m_resetname].AddEnumValue("Opposite Edge", RESET_OPPOSITE);
		m_parameters[m_resetname].AddEnumValue("None", RESET_NONE);
	}
}

DropoutTrigger::~DropoutTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DropoutTrigger::GetTriggerName()
{
	return "Dropout";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool DropoutTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
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
