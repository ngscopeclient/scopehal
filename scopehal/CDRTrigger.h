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
	@brief Declaration of CDRTrigger
	@ingroup triggers
 */
#ifndef CDRTrigger_h
#define CDRTrigger_h

/**
	@brief Base class for triggers involving hardware clock/data recovery pattern matching
	@ingroup triggers
 */
class CDRTrigger : public Trigger
{
public:
	CDRTrigger(Oscilloscope* scope);
	virtual ~CDRTrigger();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	/**
		@brief Sets the nominal baud rate the PLL should attempt to lock to

		@param rate	The baud rate, in Hz
	 */
	void SetBitRate(int64_t rate)
	{ m_parameters[m_bitRateName].SetIntVal(rate); }

	///@brief Returns the nominal CDR PLL data rate
	int64_t GetBitRate()
	{ return m_parameters[m_bitRateName].GetIntVal(); }

	///@brief Automatically calculates the bit rate of the incoming signal, if possible
	void CalculateBitRate()
	{ m_calculateBitRateSignal.emit(); }

	///@brief Signal emitted every time autobaud is requested
	sigc::signal<void()> signal_calculateBitRate()
	{ return m_calculateBitRateSignal; }

	///@brief Checks if automatic bit rate calculation is available
	bool IsAutomaticBitRateCalculationAvailable();

	///@brief Queries hardware PLL lock status
	bool IsCDRLocked();

	///@brief Gets the name of the bit rate parameter
	const std::string GetBitRateName()
	{ return m_bitRateName; }

	/**
		@brief RX equalizer settings for LeCroy SDA 8Zi GTX trigger board

		TODO: we should refactor this to be more generic
	 */
	enum LeCroyEqualizerMode
	{
		///@brief No equalization
		LECROY_EQ_NONE,

		///@brief 2 dB boost
		LECROY_EQ_LOW,

		///@brief 5 dB boost
		LECROY_EQ_MEDIUM,

		///@brief 9 dB boost
		LECROY_EQ_HIGH
	};

	///@brief Where to position the reported trigger point, relative to the serial bit pattern
	enum TriggerPosition
	{
		///@brief Trigger is reported at the end of the pattern
		POSITION_END,

		///@brief Trigger is reported at the start of the pattern
		POSITION_START
	};

	///@brief Polarity inversion for the input
	enum Polarity
	{
		///@param Input signal is positive polarity
		POLARITY_NORMAL,

		///@param Input signal is negated
		POLARITY_INVERTED
	};

	///@brief Gets the position of the trigger, relative to the serial bit pattern
	TriggerPosition GetTriggerPosition()
	{ return static_cast<TriggerPosition>(m_parameters[m_positionName].GetIntVal()); }

	/**
		@brief Sets the position of the trigger, relative to the serial bit pattern

		@param p	Desired trigger position (start or end)
	 */
	void SetTriggerPosition(TriggerPosition p)
	{ m_parameters[m_positionName].SetIntVal(p); }

	/**
		@brief Gets the RX equalizer mode

		TODO: we should refactor this to be more generic
	 */
	LeCroyEqualizerMode GetEqualizerMode()
	{ return static_cast<LeCroyEqualizerMode>(m_parameters[m_lecroyEqName].GetIntVal()); }

	/**
		@brief Sets the RX equalizer mode

		TODO: we should refactor this to be more generic
	 */
	void SetEqualizerMode(LeCroyEqualizerMode mode)
	{ m_parameters[m_lecroyEqName].SetIntVal(mode); }

	///@brief Gets the polarity inversion
	Polarity GetPolarity()
	{ return static_cast<Polarity>(m_parameters[m_polarityName].GetIntVal()); }

	/**
		@brief Gets the polarity inversion

		@param mode	Polarity inversion mode
	 */
	void SetPolarity(Polarity mode)
	{ m_parameters[m_polarityName].SetIntVal(mode); }

protected:

	///@brief Name of the bit rate parameter
	std::string m_bitRateName;

	///@brief Name of the trigger position parameter
	std::string m_positionName;

	///@brief Name of the equalizer mode parameter
	std::string m_lecroyEqName;

	///@brief Name of the polarity inversion parameter
	std::string m_polarityName;

	///@brief Signal requesting an auto-baud calculation
	sigc::signal<void()> m_calculateBitRateSignal;
};

#endif

