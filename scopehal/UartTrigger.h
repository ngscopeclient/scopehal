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
	@brief Declaration of UartTrigger
 */
#ifndef UartTrigger_h
#define UartTrigger_h

#include "SerialTrigger.h"

#ifdef _WIN32
//Our parity type definitions are also found in windows.h!
//Undefine those so we can use ours instead.
#undef PARITY_NONE
#undef PARITY_ODD
#undef PARITY_EVEN
#endif

/**
	@brief Trigger when a UART sees a certain data pattern
 */
class UartTrigger : public SerialTrigger
{
public:
	UartTrigger(Oscilloscope* scope);
	virtual ~UartTrigger();

	enum ParityType
	{
		PARITY_NONE,
		PARITY_ODD,
		PARITY_EVEN
	};

	void SetParityType(ParityType type)
	{ m_parameters[m_ptypename].SetIntVal(type); }

	ParityType GetParityType()
	{ return (ParityType) m_parameters[m_ptypename].GetIntVal(); }

	enum MatchType
	{
		TYPE_DATA,
		TYPE_PARITY_ERR
	};

	void SetMatchType(MatchType type)
	{ m_parameters[m_typename].SetIntVal(type); }

	MatchType GetMatchType()
	{ return (MatchType) m_parameters[m_typename].GetIntVal(); }

	enum Polarity
	{
		IDLE_HIGH,
		IDLE_LOW
	};

	void SetPolarity(Polarity type)
	{ m_parameters[m_polarname].SetIntVal(type); }

	Polarity GetPolarity()
	{ return (Polarity) m_parameters[m_polarname].GetIntVal(); }

	int64_t GetBitRate()
	{ return m_parameters[m_baudname].GetIntVal(); }

	void SetBitRate(int64_t t)
	{ m_parameters[m_baudname].SetIntVal(t); }

	float GetStopBits()
	{ return m_parameters[m_stopname].GetFloatVal(); }

	void SetStopBits(float n)
	{ m_parameters[m_stopname].SetFloatVal(n); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	static std::string GetTriggerName();
	TRIGGER_INITPROC(UartTrigger);

protected:
	std::string m_baudname;
	std::string m_ptypename;
	std::string m_typename;
	std::string m_stopname;
	std::string m_polarname;
};

#endif
