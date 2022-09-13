/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef RFSignalGenerator_h
#define RFSignalGenerator_h

/**
	@brief An RF waveform generator which creates a carrier and optionally modulates it
 */
class RFSignalGenerator : public virtual Instrument
{
public:
	RFSignalGenerator();
	virtual ~RFSignalGenerator();

	/**
		@brief Returns the number of output channels on the generator
	 */
	virtual int GetChannelCount() =0;

	/**
		@brief Returns the name of a given output channel

		@param chan	Zero-based channel index
	 */
	virtual std::string GetChannelName(int chan) =0;

	/**
		@brief Check if a channel is currently enabled

		@param chan	Zero-based channel index

		@return True if output is enabled, false if disabled
	 */
	virtual bool GetChannelOutputEnable(int chan) =0;

	/**
		@brief Enable or disable a channel output

		@param chan	Zero-based channel index
		@param on	True to enable the output, false to disable
	 */
	virtual void SetChannelOutputEnable(int chan, bool on) =0;

	/**
		@brief Gets the power level of a channel

		@param chan	Zero-based channel index

		@return Power level (in dBm)
	 */
	virtual float GetChannelOutputPower(int chan) =0;

	/**
		@brief Sets the power level of a channel

		@param chan		Zero-based channel index
		@param power	Power level (in dBm)
	 */
	virtual void SetChannelOutputPower(int chan, float power) =0;

	/**
		@brief Gets the center frequency of a channel

		@param chan	Zero-based channel index

		@return Center frequency, in Hz
	 */
	virtual float GetChannelCenterFrequency(int chan) =0;

	/**
		@brief Sets the power level of a channel

		@param chan		Zero-based channel index
		@param freq		Center frequency, in Hz
	 */
	virtual void SetChannelCenterFrequency(int chan, float freq) =0;

	/**
		@brief Checks if an instrument is vector modulation capable

		@param chan		Zero-based channel index
	 */
	virtual bool IsVectorModulationAvailable(int chan) =0;

	//TODO: Modulation
};

#endif
