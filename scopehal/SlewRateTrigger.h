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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of SlewRateTrigger
 */
#ifndef SlewRateTrigger_h
#define SlewRateTrigger_h

#include "TwoLevelTrigger.h"

/**
	@brief Slew rate trigger - trigger when an edge rate meets the specified conditions
 */
class SlewRateTrigger : public TwoLevelTrigger
{
public:
	SlewRateTrigger(Oscilloscope* scope);
	virtual ~SlewRateTrigger();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	static std::string GetTriggerName();
	TRIGGER_INITPROC(SlewRateTrigger);

	//Upper interval
	int64_t GetUpperInterval()
	{ return m_parameters[m_upperintname].GetIntVal(); }

	void SetUpperInterval(int64_t interval)
	{ m_parameters[m_upperintname].SetIntVal(interval); }

	//Lower interval
	int64_t GetLowerInterval()
	{ return m_parameters[m_lowerintname].GetIntVal(); }

	void SetLowerInterval(int64_t interval)
	{ m_parameters[m_lowerintname].SetIntVal(interval); }

	//Condition
	void SetCondition(Condition type)
	{ m_parameters[m_conditionname].SetIntVal(type); }

	Condition GetCondition()
	{ return (Condition) m_parameters[m_conditionname].GetIntVal(); }

	//Types
	enum EdgeType
	{
		EDGE_RISING,
		EDGE_FALLING
	};

	void SetSlope(EdgeType type)
	{ m_parameters[m_slopename].SetIntVal(type); }

	EdgeType GetSlope()
	{ return (EdgeType) m_parameters[m_slopename].GetIntVal(); }

protected:
	std::string m_conditionname;
	std::string m_lowerintname;
	std::string m_upperintname;
	std::string m_slopename;
};

#endif
