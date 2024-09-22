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
	@brief Implementation of CDRNRZPatternTrigger
	@ingroup triggers
 */

#include "scopehal.h"
#include "CDRNRZPatternTrigger.h"
#include "LeCroyOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CDRNRZPatternTrigger::CDRNRZPatternTrigger(Oscilloscope* scope)
	: CDRTrigger(scope)
	/*, m_patternModeName("Mode")
	, m_patternName("Pattern")
	, m_patternLengthName("Length")
	, m_matchModeName("Match")*/
{
	/*
	m_parameters[m_patternModeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_patternModeName].AddEnumValue("Sequence", PATTERN_SEQUENCE);
	m_parameters[m_patternModeName].AddEnumValue("List", PATTERN_LIST);
	m_parameters[m_patternModeName].SetIntVal(PATTERN_SEQUENCE);
	m_parameters[m_patternModeName].signal_changed().connect(
		sigc::mem_fun(*this, &CDRNRZPatternTrigger::OnModeChanged));

	m_parameters[m_matchModeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_matchModeName].AddEnumValue("Include", MATCH_INCLUDE);
	m_parameters[m_matchModeName].AddEnumValue("Exclude", MATCH_EXCLUDE);
	m_parameters[m_matchModeName].SetIntVal(MATCH_INCLUDE);

	//Pattern length can be up to 8 in sequence mode, or 6 in match mode
	//Default to 8 at startup, we can cut down to 6 at run time as needed if mode changes
	if(dynamic_cast<LeCroyOscilloscope*>(m_scope) != nullptr)
	{
		m_parameters[m_patternLengthName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
		for(int i=1; i<=8; i++)
			m_parameters[m_patternLengthName].AddEnumValue(to_string(i), i);
		m_parameters[m_patternLengthName].SetIntVal(1);

		m_parameters[m_patternLengthName].signal_changed().connect(
			sigc::mem_fun(*this, &CDRNRZPatternTrigger::OnLengthChanged));
	}

	m_parameters[m_patternName] = FilterParameter(FilterParameter::TYPE_NRZPattern_PATTERN, Unit(Unit::UNIT_COUNTS));
	*/
}

CDRNRZPatternTrigger::~CDRNRZPatternTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CDRNRZPatternTrigger::GetTriggerName()
{
	return "CDR (NRZ Pattern)";
}

/*
void CDRNRZPatternTrigger::OnLengthChanged()
{
	auto pat = m_parameters[m_patternName].GetNRZPatternPattern();
	pat.resize(m_parameters[m_patternLengthName].GetIntVal());
	m_parameters[m_patternName].SetNRZPatternPattern(pat);
}

void CDRNRZPatternTrigger::OnModeChanged()
{
	//Pattern length can be up to 8 in sequence mode, or 6 in match mode
	if(dynamic_cast<LeCroyOscilloscope*>(m_scope) != nullptr)
	{
		m_parameters[m_patternLengthName].ClearEnumValues();

		int nmax = 8;
		if(m_parameters[m_patternModeName].GetIntVal() == PATTERN_LIST)
			nmax = 6;

		for(int i=1; i<=nmax; i++)
			m_parameters[m_patternLengthName].AddEnumValue(to_string(i), i);

	}
}
*/
