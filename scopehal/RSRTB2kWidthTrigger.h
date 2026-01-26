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
#ifndef RSRTB2kWidthTrigger_h
#define RSRTB2kWidthTrigger_h

/**
	@brief Trigger on a pulse meeting certain width criteria
 */
class RSRTB2kWidthTrigger : public Trigger
{
public:
	RSRTB2kWidthTrigger(Oscilloscope* scope);
	virtual ~RSRTB2kWidthTrigger();

	enum EdgeType
	{
		EDGE_RISING,
		EDGE_FALLING
	};

	void SetType(EdgeType type)
	{ m_edgetype.SetIntVal(type); }

	EdgeType GetType()
	{ return (EdgeType) m_edgetype.GetIntVal(); }

	void SetCondition(Condition type)
	{ m_conditiontype.SetIntVal(type); }

	Condition GetCondition()
	{ return (Condition) m_conditiontype.GetIntVal(); }

	int64_t GetWidthTime()
	{ return m_widthTime.GetIntVal(); }

	void SetWidthTime(int64_t bound)
	{ m_widthTime.SetIntVal(bound); }

	int64_t GetWidthVariation()
	{ return m_widthVariation.GetIntVal(); }

	void SetWidthVariation(int64_t bound)
	{ m_widthVariation.SetIntVal(bound); }

	enum HysteresisType
	{
		HYSTERESIS_SMALL,
		///@brief Values correspond to the vertical scale
		HYSTERESIS_MEDIUM,
		HYSTERESIS_LARGE
	};

	void SetHysteresisType(HysteresisType type)
	{ m_hysteresistype.SetIntVal(type); }

	HysteresisType GetHysteresisType()
	{ return (HysteresisType) m_hysteresistype.GetIntVal(); }

	void SetHoldoffTimeState(bool state)
	{ m_holdofftimestate.SetBoolVal(state); }

	bool GetHoldoffTimeState()
	{ return m_holdofftimestate.GetBoolVal(); }

	void SetHoldoffTime(uint64_t bound)
	{ m_holdofftime.SetIntVal(bound); }

	uint64_t GetHoldoffTime()
	{ return m_holdofftime.GetIntVal(); }


	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	static std::string GetTriggerName();
	TRIGGER_INITPROC(RSRTB2kWidthTrigger);

protected:
	FilterParameter& m_edgetype;
	FilterParameter& m_conditiontype;
	FilterParameter& m_widthTime;
	FilterParameter& m_widthVariation;
	FilterParameter& m_holdofftimestate;
	FilterParameter& m_holdofftime;
	FilterParameter& m_hysteresistype;

};

#endif
