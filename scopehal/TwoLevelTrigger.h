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
	@brief Declaration of TwoLevelTrigger
	@ingroup triggers
 */
#ifndef TwoLevelTrigger_h
#define TwoLevelTrigger_h

/**
	@brief Base class for all triggers that have two thresholds rather than one

	@ingroup triggers
 */
class TwoLevelTrigger : public Trigger
{
public:
	TwoLevelTrigger(Oscilloscope* scope);
	virtual ~TwoLevelTrigger();

	/**
		@brief Gets the upper of the two trigger levels

		This is the base class "trigger level.
	 */
	float GetUpperBound()
	{ return GetLevel(); }

	/**
		@brief Sets the upper trigger level

		This is the base class "trigger level.

		@param level	Trigger level
	 */
	void SetUpperBound(float level)
	{ SetLevel(level); }

	///@brief Gets the lower of the two trigger levels
	float GetLowerBound()
	{ return m_lowerLevel.GetFloatVal(); }

	/**
		@brief Sets the lower trigger level

		@param level	Trigger level
	 */
	void SetLowerBound(float level)
	{ m_lowerLevel.SetFloatVal(level); }

protected:

	///@brief Lower voltage threshold
	FilterParameter& m_lowerLevel;
};

#endif
