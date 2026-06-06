/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of CDRTrigger
	@ingroup triggers
 */
#include "scopehal.h"
#include "CDRTrigger.h"
#include "LeCroyOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Creates a new CDR trigger

	@param scope	The scope the trigger is to be created for
 */
CDRTrigger::CDRTrigger(Oscilloscope* scope)
	: Trigger(scope)
	, m_bitRate(m_parameters["Bit Rate"])
	, m_position(m_parameters["Trigger Point"])
	, m_lecroyEq(m_parameters["Equalization"])
	, m_polarity(m_parameters["Polarity"])
{
	CreateInput("in");

	m_bitRate = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_bitRate.SetIntVal(1250000000);

	m_position = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_position.AddEnumValue("Start of pattern", POSITION_START);
	m_position.AddEnumValue("End of pattern", POSITION_END);
	m_position.SetIntVal(POSITION_START);

	m_polarity = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_polarity.AddEnumValue("Normal", POLARITY_NORMAL);
	m_polarity.AddEnumValue("Inverted", POLARITY_INVERTED);
	m_polarity.SetIntVal(POLARITY_NORMAL);

	//Equalization (only for LeCroy scopes)
	//TODO: this is for SDA 8Zi, does the set of parameters change for other scopes?
	if(dynamic_cast<LeCroyOscilloscope*>(m_scope) != nullptr)
	{
		m_lecroyEq = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
		m_lecroyEq.AddEnumValue("None (0 dB)", LECROY_EQ_NONE);
		m_lecroyEq.AddEnumValue("Low (2 dB)", LECROY_EQ_LOW);
		m_lecroyEq.AddEnumValue("Medium (5 dB)", LECROY_EQ_MEDIUM);
		m_lecroyEq.AddEnumValue("High (9 dB)", LECROY_EQ_HIGH);
		m_lecroyEq.SetIntVal(LECROY_EQ_NONE);
	}
}

CDRTrigger::~CDRTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Autobaud

/**
	@brief Checks whether automatic bit rate calculation is available on this trigger.

	The current implementation assumes any LeCroyOscilloscope with a CDR trigger board has auto-baud capability,
	and nothing else does.
 */
bool CDRTrigger::IsAutomaticBitRateCalculationAvailable()
{
	if(dynamic_cast<LeCroyOscilloscope*>(m_scope) != nullptr)
		return true;

	return false;
}

bool CDRTrigger::ValidateChannel(size_t /*i*/, StreamDescriptor stream)
{
	//LeCroy scopes with CDR trigger only support it on channel 4 (if not interleaving) or 3 (if interleaving)
	if(dynamic_cast<LeCroyOscilloscope*>(m_scope) != nullptr)
	{
		int expectedChannel = 3;
		if(m_scope->IsInterleaving())
			expectedChannel = 2;

		return (stream.m_channel == m_scope->GetChannel(expectedChannel));
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PLL lock status

/**
	@brief Checks whether the hardware clock recovery PLL is locked
 */
bool CDRTrigger::IsCDRLocked()
{
	auto lscope = dynamic_cast<LeCroyOscilloscope*>(m_scope);
	if(lscope != nullptr)
		return lscope->IsCDRLocked();

	//default: assume it's locked
	return true;
}
