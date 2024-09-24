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
	@brief Declaration of UartTrigger
	@ingroup triggers
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
#undef PARITY_MARK
#undef PARITY_SPACE
#endif

/**
	@brief Trigger when a UART sees a certain data pattern
	@ingroup triggers
 */
class UartTrigger : public SerialTrigger
{
public:
	UartTrigger(Oscilloscope* scope);
	virtual ~UartTrigger();

	///@brief Type of parity to use
	enum ParityType
	{
		///@brief No parity
		PARITY_NONE,

		///@brief Odd parity
		PARITY_ODD,

		///@brief Even parity
		PARITY_EVEN,

		///@brief Mark parity
		PARITY_MARK,

		///@brief Space parity
		PARITY_SPACE
	};

	/**
		@brief Set the parity for the trigger

		@param type		Selected parity mode
	 */
	void SetParityType(ParityType type)
	{ m_parameters[m_ptypename].SetIntVal(type); }

	///@brief Get the currently selected parity mode
	ParityType GetParityType()
	{ return (ParityType) m_parameters[m_ptypename].GetIntVal(); }

	///@brief What kind of pattern to match
	enum MatchType
	{
		///@brief Match on a data byte
		TYPE_DATA,

		///@brief Match on a parity error
		TYPE_PARITY_ERR,

		///@brief Match on a start bit
		TYPE_START,

		///@brief Match on a stop bit
		TYPE_STOP
	};

	/**
		@brief Sets the match mode for the trigger

		@param type		Type of pattern to look for
	 */
	void SetMatchType(MatchType type)
	{ m_parameters[m_typename].SetIntVal(type); }

	///@brief Returns the currently selected match mode
	MatchType GetMatchType()
	{ return (MatchType) m_parameters[m_typename].GetIntVal(); }

	///@brief Polarity of the port
	enum Polarity
	{
		///@brief Idle high, pull low to send a bit
		IDLE_HIGH,

		///@brief Idle low, pull high to send a bit
		IDLE_LOW
	};

	/**
		@brief Sets the UART polarity

		@param type		Desired polarity
	 */
	void SetPolarity(Polarity type)
	{ m_parameters[m_polarname].SetIntVal(type); }

	///@brief Get the current trigger polarity
	Polarity GetPolarity()
	{ return (Polarity) m_parameters[m_polarname].GetIntVal(); }

	///@brief Get the current baud rate
	int64_t GetBitRate()
	{ return m_parameters[m_baudname].GetIntVal(); }

	/**
		@brief Sets the baud rate

		@param t	Desired baud rate
	 */
	void SetBitRate(int64_t t)
	{ m_parameters[m_baudname].SetIntVal(t); }

	///@brief Get the length of the stop bit, in UI
	float GetStopBits()
	{ return m_parameters[m_stopname].GetFloatVal(); }

	/**
		@brief Set the length of the stop bit

		@param n	Length of the stop bit, in UI
	 */
	void SetStopBits(float n)
	{ m_parameters[m_stopname].SetFloatVal(n); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	static std::string GetTriggerName();
	TRIGGER_INITPROC(UartTrigger);

protected:

	///@brief Name of the "baud rate" parameter
	std::string m_baudname;

	///@brief Name of the "parity type" parameter
	std::string m_ptypename;

	///@brief Name of the "match type" parameter
	std::string m_typename;

	///@brief Name of the "stop bits" parameter
	std::string m_stopname;

	///@brief Name of the "polarity" parameter
	std::string m_polarname;
};

#endif
