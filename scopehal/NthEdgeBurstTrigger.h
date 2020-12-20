/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2020 Mike Walters                                                                                      * 
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
	@author Mike Walters
	@brief Declaration of NthEdgeBurstTrigger
 */
#ifndef NthEdgeBurstTrigger_h
#define NthEdgeBurstTrigger_h

/**
	@brief Nth Edge Burst Trigger
 */
class NthEdgeBurstTrigger : public Trigger
{
public:
	NthEdgeBurstTrigger(Oscilloscope* scope);
	virtual ~NthEdgeBurstTrigger();

	enum EdgeType
	{
		EDGE_RISING,
		EDGE_FALLING,
	};

	void SetSlope(EdgeType type)
	{ m_parameters[m_slopename].SetIntVal(type); }

	EdgeType GetSlope()
	{ return (EdgeType) m_parameters[m_slopename].GetIntVal(); }

	void SetIdleTime(int64_t idle)
	{ m_parameters[m_idletimename].SetIntVal(idle); }

	int64_t GetIdleTime()
	{ return m_parameters[m_idletimename].GetIntVal(); }

	void SetEdgeNumber(int64_t edge)
	{ m_parameters[m_edgenumbername].SetIntVal(edge); }

	int64_t GetEdgeNumber()
	{ return m_parameters[m_edgenumbername].GetIntVal(); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	static std::string GetTriggerName();
	TRIGGER_INITPROC(NthEdgeBurstTrigger);

protected:
	std::string m_slopename;
	std::string m_idletimename;
	std::string m_edgenumbername;
};

#endif
