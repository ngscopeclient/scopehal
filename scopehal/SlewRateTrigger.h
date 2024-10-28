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
	@brief Declaration of SlewRateTrigger
	@ingroup triggers
 */
#ifndef SlewRateTrigger_h
#define SlewRateTrigger_h

#include "TwoLevelTrigger.h"

/**
	@brief Slew rate trigger - trigger when an edge rate meets the specified conditions
	@ingroup triggers
 */
class SlewRateTrigger : public TwoLevelTrigger
{
public:
	SlewRateTrigger(Oscilloscope* scope);
	virtual ~SlewRateTrigger();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	static std::string GetTriggerName();
	TRIGGER_INITPROC(SlewRateTrigger);

	///@brief Get the upper limit on edge duration
	int64_t GetUpperInterval()
	{ return m_upperInterval.GetIntVal(); }

	/**
		@brief Sets the upper limit on edge duration

		@param interval	Time from crossing first to second level, in fs
	 */
	void SetUpperInterval(int64_t interval)
	{ m_upperInterval.SetIntVal(interval); }

	///@brief Get the lower limit on edge duration
	int64_t GetLowerInterval()
	{ return m_lowerInterval.GetIntVal(); }

	/**
		@brief Sets the lower limit on edge duration

		@param interval	Time from crossing first to second level, in fs
	 */
	void SetLowerInterval(int64_t interval)
	{ m_lowerInterval.SetIntVal(interval); }

	/**
		@brief Set the logical condition for the trigger

		@param type	Trigger condition
	 */
	void SetCondition(Condition type)
	{ m_condition.SetIntVal(type); }

	///@brief Get the logical condition for the trigger
	Condition GetCondition()
	{ return (Condition) m_condition.GetIntVal(); }

	///@brief Edge directions
	enum EdgeType
	{
		///@brief Rising edge
		EDGE_RISING,

		///@brief Falling edge
		EDGE_FALLING,

		///@brief Either rising or falling edge
		EDGE_ANY
	};

	/**
		@brief Set the edge direction to trigger on

		@param type	Type of edge to trigger on
	 */
	void SetSlope(EdgeType type)
	{ m_slope.SetIntVal(type); }

	///@brief Get the edge direction
	EdgeType GetSlope()
	{ return (EdgeType) m_slope.GetIntVal(); }

protected:

	///@brief Target condition to search for
	FilterParameter& m_condition;

	///@brief Lower interval
	FilterParameter& m_lowerInterval;

	///@brief Upper interval
	FilterParameter& m_upperInterval;

	///@brief Slope
	FilterParameter& m_slope;
};

#endif
