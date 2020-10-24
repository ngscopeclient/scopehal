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
#include "WindowTrigger.h"
#include "TektronixOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WindowTrigger::WindowTrigger(Oscilloscope* scope)
	: TwoLevelTrigger(scope)
	, m_widthName("Time Limit")
	, m_crossingName("Edge")
	, m_windowName("Condition")
{
	CreateInput("din");

	if(dynamic_cast<TektronixOscilloscope*>(scope))
	{
		m_parameters[m_widthName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_PS));

		m_parameters[m_crossingName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
		m_parameters[m_crossingName].AddEnumValue("Upper", CROSS_UPPER);
		m_parameters[m_crossingName].AddEnumValue("Lower", CROSS_LOWER);
		m_parameters[m_crossingName].AddEnumValue("Either", CROSS_EITHER);
		m_parameters[m_crossingName].AddEnumValue("None", CROSS_NONE);

		m_parameters[m_windowName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
		m_parameters[m_windowName].AddEnumValue("Enter", WINDOW_ENTER);
		m_parameters[m_windowName].AddEnumValue("Exit", WINDOW_EXIT);
		m_parameters[m_windowName].AddEnumValue("Exit (timed)", WINDOW_EXIT_TIMED);
		m_parameters[m_windowName].AddEnumValue("Enter (timed)", WINDOW_ENTER_TIMED);
	}
}

WindowTrigger::~WindowTrigger()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string WindowTrigger::GetTriggerName()
{
	return "Window";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool WindowTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
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
