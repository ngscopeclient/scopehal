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
	@brief Declaration of DropoutTrigger
 */
#ifndef DropoutTrigger_h
#define DropoutTrigger_h

/**
	@brief Trigger when a signal stops toggling for some amount of time
 */
class DropoutTrigger : public Trigger
{
public:
	DropoutTrigger(Oscilloscope* scope);
	virtual ~DropoutTrigger();

	enum EdgeType
	{
		EDGE_RISING,
		EDGE_FALLING,
		EDGE_ANY
	};

	void SetType(EdgeType type)
	{ m_parameters[m_typename].SetIntVal(type); }

	EdgeType GetType()
	{ return (EdgeType) m_parameters[m_typename].GetIntVal(); }

	enum ResetType
	{
		RESET_OPPOSITE,	//
		RESET_NONE
	};

	void SetResetType(ResetType type)
	{ m_parameters[m_resetname].SetIntVal(type); }

	ResetType GetResetType()
	{ return (ResetType) m_parameters[m_resetname].GetIntVal(); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	int64_t GetDropoutTime()
	{ return m_parameters[m_timename].GetIntVal(); }

	void SetDropoutTime(int64_t t)
	{ m_parameters[m_timename].SetIntVal(t); }

	static std::string GetTriggerName();
	TRIGGER_INITPROC(DropoutTrigger);

protected:
	std::string m_typename;
	std::string m_timename;
	std::string m_resetname;
};

#endif
