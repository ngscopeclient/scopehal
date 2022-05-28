/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of CDRTrigger
 */
#ifndef CDRTrigger_h
#define CDRTrigger_h

/**
	@brief A trigger involving hardware clock/data recovery
 */
class CDRTrigger : public Trigger
{
public:
	CDRTrigger(Oscilloscope* scope);
	virtual ~CDRTrigger();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	void SetBitRate(int64_t rate)
	{ m_parameters[m_bitRateName].SetIntVal(rate); }

	int64_t GetBitRate()
	{ return m_parameters[m_bitRateName].GetIntVal(); }

	/**
		@brief Automatically calculates the bit rate of the incoming signal
	 */
	void CalculateBitRate()
	{ m_calculateBitRateSignal.emit(); }

	/**
		@brief Signal emitted every time autobaud is requested
	 */
	sigc::signal<void> signal_calculateBitRate()
	{ return m_calculateBitRateSignal; }

	/**
		@brief Checks if automatic bit rate calculation is available
	 */
	bool IsAutomaticBitRateCalculationAvailable();

	const std::string GetBitRateName()
	{ return m_bitRateName; }

protected:

	std::string m_bitRateName;

	sigc::signal<void> m_calculateBitRateSignal;
};

#endif
