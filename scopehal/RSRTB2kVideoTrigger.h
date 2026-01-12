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
#ifndef RSRTB2kVideoTrigger_h
#define RSRTB2kVideoTrigger_h

/**
	@brief Video or TV trigger is used to analyze analog baseband video signals
	@ingroup triggers
 */
class RSRTB2kVideoTrigger : public Trigger
{
public:
	RSRTB2kVideoTrigger(Oscilloscope* scope);
	virtual ~RSRTB2kVideoTrigger();

	enum EdgeType
	{
		EDGE_RISING,
		EDGE_FALLING
	};

	void SetType(EdgeType type)
	{ m_edgetype.SetIntVal(type); }

	EdgeType GetType()
	{ return (EdgeType) m_edgetype.GetIntVal(); }

	enum StandardType
	{
		STANDARD_PAL,
		STANDARD_NTSC,
		STANDARD_SEC,
		STANDARD_PALM,
		STANDARD_I576,
		STANDARD_P720,
		STANDARD_P1080,
		STANDARD_I1080
	};

	void SetStandardType(StandardType type)
	{ m_standardtype.SetIntVal(type); }

	StandardType GetStandardType()
	{ return (StandardType) m_standardtype.GetIntVal(); }

	enum ModeType
	{
		MODE_ALL,
		MODE_ODD,
		MODE_EVEN,
		MODE_ALIN,
		MODE_LINE
	};

	void SetModeType(ModeType type)
	{ m_modetype.SetIntVal(type); }

	ModeType GetModeType()
	{ return (ModeType) m_modetype.GetIntVal(); }

	uint64_t GetLineNumber()
	{ return m_linenumber.GetIntVal(); }

	void SetLineNumber(uint64_t interval)
	{ m_linenumber.SetIntVal(interval); }

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
	TRIGGER_INITPROC(RSRTB2kVideoTrigger);

protected:
	FilterParameter& m_edgetype;
	FilterParameter& m_standardtype;
	FilterParameter& m_modetype;
	FilterParameter& m_linenumber;
	FilterParameter& m_holdofftimestate;
	FilterParameter& m_holdofftime;
};

#endif
