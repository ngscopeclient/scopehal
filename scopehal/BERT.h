/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef BERT_h
#define BERT_h

/**
	@brief Base class for bit error rate testers
 */
class BERT : public virtual Instrument
{
public:
	BERT();
	virtual ~BERT();

	virtual unsigned int GetInstrumentTypes();

	enum Pattern
	{
		//Standard PRBS polynomials
		PATTERN_PRBS7,
		PATTERN_PRBS9,
		PATTERN_PRBS11,
		PATTERN_PRBS15,
		PATTERN_PRBS23,
		PATTERN_PRBS31,

		//Custom output pattern
		PATTERN_CUSTOM,

		//Autodetect input pattern
		PATTERN_AUTO
	};

	std::string GetPatternName(Pattern pat);

	/**
		@brief Gets the currently selected transmit pattern for a channel
	 */
	virtual Pattern GetTxPattern(size_t i) =0;

	/**
		@brief Gets the transmit invert flag for a channel
	 */
	virtual bool GetTxInvert(size_t i) =0;

	/**
		@brief Gets the transmit invert flag for a channel
	 */
	virtual bool GetRxInvert(size_t i) =0;

	/**
		@brief Sets the transmit invert flag for a channel
	 */
	virtual void SetTxInvert(size_t i, bool invert) =0;

	/**
		@brief Sets the receive invert flag for a channel
	 */
	virtual void SetRxInvert(size_t i, bool invert) =0;

	/**
		@brief Sets the transmit pattern for the selected channel
	 */
	virtual void SetTxPattern(size_t i, Pattern pattern) =0;

	/**
		@brief Gets the currently selected receive pattern for a channel
	 */
	virtual Pattern GetRxPattern(size_t i) =0;

	/**
		@brief Sets the receive pattern for the selected channel
	 */
	virtual void SetRxPattern(size_t i, Pattern pattern) =0;

	/**
		@brief Gets the list of available transmit patterns for a channel
	 */
	virtual std::vector<Pattern> GetAvailableTxPatterns(size_t i) =0;

	/**
		@brief Gets the list of available receive patterns for a channel
	 */
	virtual std::vector<Pattern> GetAvailableRxPatterns(size_t i) =0;

	/**
		@brief Gets the list of available drive strengths (in volts) for a channel

		Drive strength is nominal p-p swing with full main cursor tap and pre/post cursors at zero.
	 */
	virtual std::vector<float> GetAvailableTxDriveStrengths(size_t i) =0;

	/**
		@brief Get the drive strength for a channel
	 */
	virtual float GetTxDriveStrength(size_t i) =0;

	/**
		@brief Set the drive strength for a channel
	 */
	virtual void SetTxDriveStrength(size_t i, float drive) =0;

	/**
		@brief Sets the transmit enable flag for a channel
	 */
	virtual void SetTxEnable(size_t i, bool enable) =0;

	/**
		@brief Gets the transmit enable flag for a channel
	 */
	virtual bool GetTxEnable(size_t i) =0;

	/**
		@brief Get the pre-cursor equalizer tap for a channel

		Tap values are normalized to 0-1
	 */
	virtual float GetTxPreCursor(size_t i) =0;

	/**
		@brief Set the pre-cursor equalizer tap for a channel
	 */
	virtual void SetTxPreCursor(size_t i, float precursor) =0;

	/**
		@brief Get the post-cursor equalizer tap for a channel

		Tap values are normalized to 0-1
	 */
	virtual float GetTxPostCursor(size_t i) =0;

	/**
		@brief Set the post-cursor equalizer tap for a channel
	 */
	virtual void SetTxPostCursor(size_t i, float postcursor) =0;

	/**
		@brief Gets the RX CDR lock state (true=lock, false=unlock)
	 */
	virtual bool GetRxCdrLockState(size_t i) =0;

	/**
		@brief Acquires a bathtub curve
	 */
	virtual void MeasureHBathtub(size_t i) =0;
};

#include "BERTInputChannel.h"
#include "BERTOutputChannel.h"

#endif
