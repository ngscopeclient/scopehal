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
	@brief Declaration of WindowTrigger
	@ingroup triggers
 */
#ifndef WindowTrigger_h
#define WindowTrigger_h

#include "TwoLevelTrigger.h"

/**
	@brief Window trigger - detect when the signal leaves a specified range
	@ingroup triggers
 */
class WindowTrigger : public TwoLevelTrigger
{
public:
	WindowTrigger(Oscilloscope* scope);
	virtual ~WindowTrigger();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	/**
		@brief Type of level crossing to detect for "stay inside" and "stay outside" windows
	 */
	enum Crossing
	{
		///@brief Upper threshold
		CROSS_UPPER,

		///@brief Lower threshold
		CROSS_LOWER,

		///@brief Either threshold
		CROSS_EITHER,

		///@brief Nothing
		CROSS_NONE,
	};

	/**
		@brief How to trigger
	 */
	enum WindowType
	{
		///@brief Trigger immediately upon entry to the window
		WINDOW_ENTER,

		///@brief Trigger immediately upon exit from the window
		WINDOW_EXIT,

		///@brief Trigger upon exit from the window, if we were in it for at least X time
		WINDOW_EXIT_TIMED,

		///@brief Trigger upon entry to the window, if we were outside it for at least X time
		WINDOW_ENTER_TIMED
	};

	/// @brief Sets the crossing direction (only used for "stay inside" and "stay outside" window types)
	void SetCrossingDirection(Crossing dir)
	{ m_parameters[m_crossingName].SetIntVal(dir); }

	///@brief Gets the selected crossing direction
	Crossing GetCrossingDirection()
	{ return (Crossing)m_parameters[m_crossingName].GetIntVal(); }

	/// @brief Sets the type of window
	void SetWindowType(WindowType type)
	{ m_parameters[m_windowName].SetIntVal(type); }

	///@brief Gets the type of window
	WindowType GetWindowType()
	{ return (WindowType)m_parameters[m_windowName].GetIntVal(); }

	/**
		@brief Sets the time the signal needs to stay in/outside the window

		Only used for WINDOW_ENTER_TIMED and WINDOW_EXIT_TIMED
	 */
	void SetWidth(int64_t ps)
	{ m_parameters[m_widthName].SetIntVal(ps); }

	///@brief Gets the time the signal needs to stay in / outside the winodw
	int64_t GetWidth()
	{ return m_parameters[m_widthName].GetIntVal(); }

	static std::string GetTriggerName();
	TRIGGER_INITPROC(WindowTrigger);

protected:

	///@brief Name of the "width" parameter
	std::string m_widthName;

	///@brief Name of the "crossing type" parameter
	std::string m_crossingName;

	///@brief Name of the "window type" parameter
	std::string m_windowName;
};

#endif
