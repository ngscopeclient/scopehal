/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of PulseWidthTrigger
 */
#ifndef PulseWidthTrigger_h
#define PulseWidthTrigger_h

#include "EdgeTrigger.h"

/**
	@brief Trigger on a pulse meeting certain width criteria
 */
class PulseWidthTrigger : public EdgeTrigger
{
public:
	PulseWidthTrigger(Oscilloscope* scope);
	virtual ~PulseWidthTrigger();

	static std::string GetTriggerName();
	TRIGGER_INITPROC(PulseWidthTrigger);

	void SetCondition(Condition type)
	{ m_parameters[m_conditionname].SetIntVal(type); }

	Condition GetCondition()
	{ return (Condition) m_parameters[m_conditionname].GetIntVal(); }

	int64_t GetLowerBound()
	{ return m_parameters[m_lowername].GetIntVal(); }

	void SetLowerBound(int64_t bound)
	{ m_parameters[m_lowername].SetIntVal(bound); }

	int64_t GetUpperBound()
	{ return m_parameters[m_uppername].GetIntVal(); }

	void SetUpperBound(int64_t bound)
	{ m_parameters[m_uppername].SetIntVal(bound); }

protected:
	std::string m_conditionname;
	std::string m_lowername;
	std::string m_uppername;
};

#endif
