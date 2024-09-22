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
	@brief Declaration of BERTOutputChannel
	@ingroup core
 */
#ifndef BERTOutputChannel_h
#define BERTOutputChannel_h

#include "BERT.h"

/**
	@brief A pattern generator channel of a BERT
	@ingroup core
 */
class BERTOutputChannel : public InstrumentChannel
{
public:

	BERTOutputChannel(
		const std::string& hwname,
		BERT* bert,
		const std::string& color = "#808080",
		size_t index = 0);

	virtual ~BERTOutputChannel();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	///@brief Get the BERT this channel is part of
	BERT* GetBERT() const
	{ return dynamic_cast<BERT*>(m_instrument); }

	/**
		@brief Set the pattern this channel is generating

		@param pattern	The new pattern
	 */
	void SetPattern(BERT::Pattern pattern)
	{ GetBERT()->SetTxPattern(GetIndex(), pattern); }

	///@brief Get the pattern this channel is currently generating
	BERT::Pattern GetPattern()
	{ return GetBERT()->GetTxPattern(GetIndex()); }

	///@brief Get the set of patterns this channel is capable of generating
	std::vector<BERT::Pattern> GetAvailablePatterns()
	{ return GetBERT()->GetAvailableTxPatterns(GetIndex()); }

	///@brief Get the polarity inversion state of this channel
	bool GetInvert()
	{ return GetBERT()->GetTxInvert(GetIndex()); }

	/**
		@brief Set the polarity inversion state of this channel

		@param invert	New inversion state (true = invert, false = normal polarity)
	 */
	void SetInvert(bool invert)
	{ GetBERT()->SetTxInvert(GetIndex(), invert); }

	/**
		@brief Get the set of amplitudes this channel is capable of outputting

		Amplitudes are in nominal volts P-P with all TX equalizer taps set to zero.
	 */
	std::vector<float> GetAvailableDriveStrengths()
	{ return GetBERT()->GetAvailableTxDriveStrengths(GetIndex()); }

	/**
		@brief Get the current nominal amplitude of this channel

		Amplitudes are in nominal volts P-P with all TX equalizer taps set to zero.
	 */
	float GetDriveStrength()
	{ return GetBERT()->GetTxDriveStrength(GetIndex()); }

	/**
		@brief Set the current nominal amplitude of this channel

		@param drive	Desired amplitude; nominal volts P-P with all TX equalizer taps set to zero.
	 */
	void SetDriveStrength(float drive)
	{ GetBERT()->SetTxDriveStrength(GetIndex(), drive); }

	///@brief Gets the enable status of this channel
	bool GetEnable()
	{ return GetBERT()->GetTxEnable(GetIndex()); }

	/**
		@brief Set the enable state of this channel

		@param b	True to enable the channel, false to disable
	 */
	void Enable(bool b)
	{ GetBERT()->SetTxEnable(GetIndex(), b); }

	/**
		@brief Gets the TX FFE pre-cursor coefficient

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.
	 */
	float GetPreCursor()
	{ return GetBERT()->GetTxPreCursor(GetIndex()); }

	/**
		@brief Sets the TX FFE pre-cursor coefficient

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.

		@param f	Pre-cursor coefficient
	 */
	void SetPreCursor(float f)
	{ GetBERT()->SetTxPreCursor(GetIndex(), f); }

	/**
		@brief Gets the TX FFE post-cursor coefficient

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.
	 */
	float GetPostCursor()
	{ return GetBERT()->GetTxPostCursor(GetIndex()); }

	/**
		@brief Sets the TX FFE post-cursor coefficient

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.

		@param f	Post-cursor coefficient
	 */
	void SetPostCursor(float f)
	{ GetBERT()->SetTxPostCursor(GetIndex(), f); }

	/**
		@brief Gets the data rate of this channel, in symbols per second
	 */
	int64_t GetDataRate()
	{ return GetBERT()->GetDataRate(GetIndex()); }

	/**
		@brief Sets the data rate of this channel, in symbols per second

		Depending on the clocking architecture of the instrument, this may affect other channels.

		For example, on MultiLane BERTs all channels share a single timebase. Antikernel Labs BERTs are
		based on Xilinx FPGAs and channels are divided into quads which each share some clocking resources.

		@param rate		Desired data rate
	 */
	void SetDataRate(int64_t rate)
	{ GetBERT()->SetDataRate(GetIndex(), rate); }

	virtual PhysicalConnector GetPhysicalConnector() override;
};

#endif
