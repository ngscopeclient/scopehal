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
	@author Mike Walters
	@brief Declaration of NthEdgeBurstTrigger
	@ingroup triggers
 */
#ifndef NthEdgeBurstTrigger_h
#define NthEdgeBurstTrigger_h

/**
	@brief Nth Edge Burst Trigger: triggers on a specific edge within a burst
	@ingroup triggers
 */
class NthEdgeBurstTrigger : public Trigger
{
public:
	NthEdgeBurstTrigger(Oscilloscope* scope);
	virtual ~NthEdgeBurstTrigger();

	///@brief Types of edges to trigger on
	enum EdgeType
	{
		///@brief Low to high transition
		EDGE_RISING,

		///@brief High to low transition
		EDGE_FALLING
	};

	/**
		@brief Set the type of the edge to trigger on

		@param type	Edge type
	 */
	void SetSlope(EdgeType type)
	{ m_edgetype.SetIntVal(type); }

	///@brief Gets the currently selected edge type
	EdgeType GetSlope()
	{ return (EdgeType) m_edgetype.GetIntVal(); }

	/**
		@brief Set the minimum idle time between bursts

		@param idle	Idle time, in femtoseconds
	 */
	void SetIdleTime(int64_t idle)
	{ m_idletime.SetIntVal(idle); }

	///@brief Gets the idle time between bursts, in femtoseconds
	int64_t GetIdleTime()
	{ return m_idletime.GetIntVal(); }

	/**
		@brief Set the index of the edge to trigger on

		@param edge	Target edge index
	 */
	void SetEdgeNumber(int64_t edge)
	{ m_edgenumber.SetIntVal(edge); }

	///@brief Get the index of the edge to trigger on
	int64_t GetEdgeNumber()
	{ return m_edgenumber.GetIntVal(); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	static std::string GetTriggerName();
	TRIGGER_INITPROC(NthEdgeBurstTrigger);

protected:

	///@brief Edge type
	FilterParameter& m_edgetype;

	///@brief Idle time before a burst is considered to have ended
	FilterParameter& m_idletime;

	///@brief Index of target edge within the burst
	FilterParameter& m_edgenumber;
};

#endif
