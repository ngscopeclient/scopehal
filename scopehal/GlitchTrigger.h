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
	@brief Declaration of GlitchTrigger
	@ingroup triggers
 */
#ifndef GlitchTrigger_h
#define GlitchTrigger_h

#include "EdgeTrigger.h"

/**
	@brief Trigger on a glitch meeting certain width criteria
	@ingroup triggers
 */
class GlitchTrigger : public EdgeTrigger
{
public:
	GlitchTrigger(Oscilloscope* scope);
	virtual ~GlitchTrigger();

	static std::string GetTriggerName();
	TRIGGER_INITPROC(GlitchTrigger);

	/**
		@brief Set the condition for the glitch

		@param type		Search condition
						May be CONDITION_LESS to only trigger on glitches shorter than the upper bound, or
						CONDITION_BETWEEN to trigger on glitches between upper and lower bounds in length
	 */
	void SetCondition(Condition type)
	{ m_condition.SetIntVal(type); }

	///@brief Get the desired glitch condition
	Condition GetCondition()
	{ return (Condition) m_condition.GetIntVal(); }

	///@brief Get the lower bound, in fs, for a pulse to be considered a glitch
	int64_t GetLowerBound()
	{ return m_lowerBound.GetIntVal(); }

	/**
		@brief Set the duration of the shortest pulse that will be considered a glitch

		@param bound	Lower bound, in fs
	 */
	void SetLowerBound(int64_t bound)
	{ m_lowerBound.SetIntVal(bound); }

	///@brief Get the upper bound, in fs, for a pulse to be considered a glitch
	int64_t GetUpperBound()
	{ return m_upperBound.GetIntVal(); }

	/**
		@brief Set the duration of the longest pulse that will be considered a glitch

		@param bound	Upper bound, in fs
	 */
	void SetUpperBound(int64_t bound)
	{ m_upperBound.SetIntVal(bound); }

protected:

	///@brief Condition to look for
	FilterParameter m_condition;

	///@brief Lower voltage level for glitch detector
	FilterParameter m_lowerBound;

	///@brief Upper voltage level for glitch detector
	FilterParameter m_upperBound;
};

#endif
