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
	@brief Declaration of CDR8B10BTrigger
	@ingroup triggers
 */
#ifndef CDR8B10BTrigger_h
#define CDR8B10BTrigger_h

#include "CDRTrigger.h"

/**
	@brief A hardware 8B/10B pattern trigger
	@ingroup triggers
 */
class CDR8B10BTrigger : public CDRTrigger
{
public:
	CDR8B10BTrigger(Oscilloscope* scope);
	virtual ~CDR8B10BTrigger();

	static std::string GetTriggerName();
	TRIGGER_INITPROC(CDR8B10BTrigger);

	///@brief Type of pattern to look for
	enum PatternMode
	{
		///@brief Match a sequence of consecutive symbols
		PATTERN_SEQUENCE,

		///@brief Match any of several symbols in a list
		PATTERN_LIST
	};

	///@brief Trigger on matched or unmatched data
	enum MatchMode
	{
		///@brief Trigger on a match
		MATCH_INCLUDE,

		///@brief Trigger if no match found
		MATCH_EXCLUDE
	};

	/**
		@brief Sets whether to trigger on pattern match or pattern not found

		@param mode	Match mode
	 */
	void SetMatchMode(MatchMode mode)
	{ m_parameters[m_matchModeName].SetIntVal(mode); }

	///@brief Get the match mode
	MatchMode GetMatchMode()
	{ return static_cast<MatchMode>(m_parameters[m_matchModeName].GetIntVal()); }

	/**
		@brief Sets the type of pattern to look for

		@param mode	Pattern mode
	 */
	void SetPatternMode(PatternMode mode)
	{ m_parameters[m_patternModeName].SetIntVal(mode); }

	///@brief Gets the type of pattern being searched for
	PatternMode GetPatternMode()
	{ return static_cast<PatternMode>(m_parameters[m_patternModeName].GetIntVal()); }

	/**
		@brief	Sets the length of the serial pattern, or size of the set of symbols to match

		@param count	Length of the pattern
	 */
	void SetSymbolCount(size_t count)
	{ m_parameters[m_patternLengthName].SetIntVal(count); }

	///@brief Gets the length of the serial pattern or size of the symbol set
	size_t GetSymbolCount()
	{ return m_parameters[m_patternLengthName].GetIntVal(); }

	///@brief Gets the pattern or list of symbols to match
	std::vector<T8B10BSymbol> GetPattern()
	{ return m_parameters[m_patternName].Get8B10BPattern(); }

	/**
		@brief Sets the pattern or list of symbols to match

		@param pattern	Vector of 8B/10B symbols to use as the pattern
	 */
	void SetPattern(const std::vector<T8B10BSymbol>& pattern)
	{ return m_parameters[m_patternName].Set8B10BPattern(pattern); }

protected:
	void OnLengthChanged();
	void OnModeChanged();

	///@brief Name of the "pattern mode" parameter
	std::string m_patternModeName;

	///@brief Name of the "pattern" parameter
	std::string m_patternName;

	///@brief Name of the "pattern length" parameter
	std::string m_patternLengthName;

	///@brief Name of the "mode" parameter
	std::string m_matchModeName;
};

#endif
