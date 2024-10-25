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
	@brief Declaration of DropoutTrigger
	@ingroup triggers
 */
#ifndef DropoutTrigger_h
#define DropoutTrigger_h

/**
	@brief Trigger when a signal stops toggling for some amount of time
	@ingroup triggers
 */
class DropoutTrigger : public Trigger
{
public:
	DropoutTrigger(Oscilloscope* scope);
	virtual ~DropoutTrigger();

	///@brief Types of edges to trigger on
	enum EdgeType
	{
		///@brief Low to high transition
		EDGE_RISING,

		///@brief High to low transition
		EDGE_FALLING,

		///@brief Either rising or falling transition
		EDGE_ANY
	};

	/**
		@brief Set the type of the edge to look for

		@param type	Edge type
	 */
	void SetType(EdgeType type)
	{ m_type.SetIntVal(type); }

	///@brief Get the currently selected edge type
	EdgeType GetType()
	{ return (EdgeType) m_type.GetIntVal(); }

	///@brief Type of edge to reset on
	enum ResetType
	{
		///@brief Reset on the opposite kind of edge
		RESET_OPPOSITE,

		///@brief Normal behavior
		RESET_NONE
	};

	/**
		@brief Set the edge to reset on

		TODO: document what this means more

		@param type	Reset type
	 */
	void SetResetType(ResetType type)
	{ m_reset.SetIntVal(type); }

	///@brief Get the currently selected reset type
	ResetType GetResetType()
	{ return (ResetType) m_reset.GetIntVal(); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	///@brief Get the timeout for a quiet period on the bus to be considered a dropout
	int64_t GetDropoutTime()
	{ return m_time.GetIntVal(); }

	/**
		@brief Set the timeout for a quiet period on the bus to be considered a dropout

		@param t	Dropout time, in fs
	 */
	void SetDropoutTime(int64_t t)
	{ m_time.SetIntVal(t); }

	static std::string GetTriggerName();
	TRIGGER_INITPROC(DropoutTrigger);

protected:

	///@brief Target edge type
	FilterParameter& m_type;

	///@brief Dropout time
	FilterParameter& m_time;

	///@brief Reset mode
	FilterParameter& m_reset;
};

#endif
