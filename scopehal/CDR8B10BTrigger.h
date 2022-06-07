/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of CDR8B10BTrigger
 */
#ifndef CDR8B10BTrigger_h
#define CDR8B10BTrigger_h

#include "CDRTrigger.h"

/**
	@brief A hardware 8B/10B pattern trigger
 */
class CDR8B10BTrigger : public CDRTrigger
{
public:
	CDR8B10BTrigger(Oscilloscope* scope);
	virtual ~CDR8B10BTrigger();

	static std::string GetTriggerName();
	TRIGGER_INITPROC(CDR8B10BTrigger);

	enum PatternMode
	{
		PATTERN_SEQUENCE,
		PATTERN_LIST
	};

	enum MatchMode
	{
		MATCH_INCLUDE,
		MATCH_EXCLUDE
	};

	void SetMatchMode(MatchMode mode)
	{ m_parameters[m_matchModeName].SetIntVal(mode); }

	MatchMode GetMatchMode()
	{ return static_cast<MatchMode>(m_parameters[m_matchModeName].GetIntVal()); }

	void SetPatternMode(PatternMode mode)
	{ m_parameters[m_patternModeName].SetIntVal(mode); }

	PatternMode GetPatternMode()
	{ return static_cast<PatternMode>(m_parameters[m_patternModeName].GetIntVal()); }

	void SetSymbolCount(size_t i)
	{ m_parameters[m_patternLengthName].SetIntVal(i); }

	size_t GetSymbolCount()
	{ return m_parameters[m_patternLengthName].GetIntVal(); }

	std::vector<T8B10BSymbol> GetPattern()
	{ return m_parameters[m_patternName].Get8B10BPattern(); }

	void SetPattern(const std::vector<T8B10BSymbol>& pattern)
	{ return m_parameters[m_patternName].Set8B10BPattern(pattern); }

protected:
	void OnLengthChanged();
	void OnModeChanged();

	std::string m_patternModeName;
	std::string m_patternName;
	std::string m_patternLengthName;
	std::string m_matchModeName;
};

#endif
