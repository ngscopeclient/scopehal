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
	@brief Declaration of BERT
	@ingroup core
 */
#ifndef BERT_h
#define BERT_h

/**
	@brief Base class for bit error rate tester drivers

	@ingroup core
 */
class BERT : public virtual Instrument
{
public:
	BERT();
	virtual ~BERT();

	virtual unsigned int GetInstrumentTypes() const override;

	///@brief Set of patterns we can generate or accept
	enum Pattern
	{
		///@brief Standard PRBS-7 x^7 + x^6 + 1
		PATTERN_PRBS7,

		///@brief Standard PRBS-9 x^9 + x^5 + 1
		PATTERN_PRBS9,

		///@brief Standard PRBS-11 x^11 + x^9 + 1
		PATTERN_PRBS11,

		///@brief Standard PRBS-15 x^15 + x^14 + 1
		PATTERN_PRBS15,

		///@brief Standard PRBS-23 x^23 + x^18 + 1
		PATTERN_PRBS23,

		///@brief Standard PRBS-31 x^31 + x^28 + 1
		PATTERN_PRBS31,

		///@brief Custom output pattern (not valid for input)
		PATTERN_CUSTOM,

		///@brief Clock at half the symbol rate (1-0-1-0 pattern)
		PATTERN_CLOCK_DIV2,

		///@brief Clock at 1/32 the symbol rate (16 0s followed by 16 1s)
		PATTERN_CLOCK_DIV32,

		///@brief Autodetect input pattern (not valid for output)
		PATTERN_AUTO
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// TX pattern generator configuration

	static std::string GetPatternName(Pattern pat);
	static Pattern GetPatternOfName(std::string name);

	/**
		@brief Gets the currently selected transmit pattern for a channel

		@param i	Channel index
	 */
	virtual Pattern GetTxPattern(size_t i) =0;

	/**
		@brief Sets the transmit pattern for the selected channel

		@param i		Channel index
		@param pattern	The selected pattern
	 */
	virtual void SetTxPattern(size_t i, Pattern pattern) =0;

	/**
		@brief Gets the list of available transmit patterns for a channel

		@param i		Channel index
	 */
	virtual std::vector<Pattern> GetAvailableTxPatterns(size_t i) =0;

	/**
		@brief Determines whether custom patterns are settable per channel, or shared by the whole device

		@return True = per channel, false = global
	 */
	virtual bool IsCustomPatternPerChannel() =0;

	/**
		@brief Returns the number of bits in a custom pattern (may change with line rate)

		@return Length of the custom pattern
	 */
	virtual size_t GetCustomPatternLength() =0;

	/**
		@brief Sets the global custom pattern (only valid if IsCustomPatternPerChannel returns false)

		@param pattern	The bit sequence (up to 64 bits in length)
	 */
	virtual void SetGlobalCustomPattern(uint64_t pattern) =0;

	/**
		@brief Gets the global custom pattern (only valid if IsCustomPatternPerChannel returns false)

		@return The bit sequence
	 */
	virtual uint64_t GetGlobalCustomPattern() =0;


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// TX driver configuration

	/**
		@brief Gets the transmit invert flag for a channel

		@param i		Channel index
	 */
	virtual bool GetTxInvert(size_t i) =0;


	/**
		@brief Sets the transmit invert flag for a channel

		@param i		Channel index
		@param invert	True = inverted, false = normal
	 */
	virtual void SetTxInvert(size_t i, bool invert) =0;

	/**
		@brief Gets the list of available drive strengths (in volts) for a channel

		Drive strength is nominal p-p swing with full main cursor tap and pre/post cursors at zero.

		@param i		Channel index
	 */
	virtual std::vector<float> GetAvailableTxDriveStrengths(size_t i) =0;

	/**
		@brief Get the drive strength for a channel

		@param i		Channel index
	 */
	virtual float GetTxDriveStrength(size_t i) =0;

	/**
		@brief Set the drive strength for a channel

		@param i		Channel index
		@param drive	Nominal p-p swing
	 */
	virtual void SetTxDriveStrength(size_t i, float drive) =0;

	/**
		@brief Sets the transmit enable flag for a channel

		@param i		Channel index
		@param enable	True = enabled, false = disabled
	 */
	virtual void SetTxEnable(size_t i, bool enable) =0;

	/**
		@brief Gets the transmit enable flag for a channel

		@param i		Channel index
	 */
	virtual bool GetTxEnable(size_t i) =0;

	/**
		@brief Get the pre-cursor equalizer tap for a channel

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.

		@param i		Channel index
	 */
	virtual float GetTxPreCursor(size_t i) =0;

	/**
		@brief Set the pre-cursor equalizer tap for a channel

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.

		@param i			Channel index
		@param precursor	Pre-cursor tap
	 */
	virtual void SetTxPreCursor(size_t i, float precursor) =0;

	/**
		@brief Get the post-cursor equalizer tap for a channel

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.

		@param i			Channel index
	 */
	virtual float GetTxPostCursor(size_t i) =0;

	/**
		@brief Set the post-cursor equalizer tap for a channel

		Equalizer coefficients are are normalized so that 0 is the lowest possible tap value and 1 is the highest
		possible tap value. Some instruments allow negative values (which invert the sign of the tap).

		The exact mapping of normalized coefficients to FFE gain steps is instrument specific and may be nonlinear.

		@param i			Channel index
		@param postcursor	Post-cursor tap
	 */
	virtual void SetTxPostCursor(size_t i, float postcursor) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// RX input buffer configuration

	/**
		@brief Gets the transmit invert flag for a channel

		@param i			Channel index
	 */
	virtual bool GetRxInvert(size_t i) =0;


	/**
		@brief Sets the receive invert flag for a channel

		@param i			Channel index
		@param invert		True = inverted, false = inverted
	 */
	virtual void SetRxInvert(size_t i, bool invert) =0;

	/**
		@brief Gets the RX CDR lock state (true=lock, false=unlock)

		NOTE: For some instruments this is just CDR lock, for others it indicates both CDR and pattern checker lock

		@param i			Channel index
	 */
	virtual bool GetRxCdrLockState(size_t i) =0;

	/**
		@brief Determines whether the input buffer has a continuous-time linear equalizer
	 */
	virtual bool HasRxCTLE() =0;

	/**
		@brief Get the list of available RX CTLE gain values (in dB)
	 */
	virtual std::vector<float> GetRxCTLEGainSteps() =0;

	/**
		@brief Gets the currently selected RX CTLE gain index

		To get the actual gain in dB, use this value as the index into the table returned by GetRxCTLEGainSteps()

		@param i			Channel index
	 */
	virtual size_t GetRxCTLEGainStep(size_t i) =0;

	/**
		@brief Sets the RX CTLE gain index

		@param i			Channel index
		@param step			Index of the gain value within the GetRxCTLEGainSteps() table to use
	 */
	virtual void SetRxCTLEGainStep(size_t i, size_t step) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// RX pattern checker configuration

	/**
		@brief Gets the currently selected receive pattern for a channel

		@param i			Channel index
	 */
	virtual Pattern GetRxPattern(size_t i) =0;

	/**
		@brief Sets the receive pattern for the selected channel

		@param i			Channel index
		@param pattern		Desired input pattern
	 */
	virtual void SetRxPattern(size_t i, Pattern pattern) =0;

	/**
		@brief Gets the list of available receive patterns for a channel

		@param i			Channel index
	 */
	virtual std::vector<Pattern> GetAvailableRxPatterns(size_t i) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// RX data readout

	/**
		@brief Returns the expected time, in femtoseconds, to complete a bathtub scan with the current settings.

		The default implementation returns zero.

		@param i			Channel index
	 */
	virtual int64_t GetExpectedBathtubCaptureTime(size_t i);

	/**
		@brief Returns the expected time, in femtoseconds, to complete an eye scan with the current settings.

		The default implementation returns zero.

		@param i			Channel index
	 */
	virtual int64_t GetExpectedEyeCaptureTime(size_t i);

	/**
		@brief Acquires a bathtub curve

		@param i			Channel index
	 */
	virtual void MeasureHBathtub(size_t i) =0;

	/**
		@brief Acquires an eye pattern

		@param i			Channel index
	 */
	virtual void MeasureEye(size_t i) =0;

	/**
		@brief Set the integration period for BER measurements

		Longer values give better resolution for low BERs but update more slowly
	 */
	virtual void SetBERIntegrationLength(int64_t uis) =0;

	/**
		@brief Get the integration period for BER measurements
	 */
	virtual int64_t GetBERIntegrationLength() =0;

	/**
		@brief Set the sampling point for BER measurements

		@param i	Channel index
		@param dx	Horizontal offset of the sampling point from the center of the eye, in femtoseconds
		@param dy	Vertical offset of the sampling point from the center of the eye, in volts
	 */
	virtual void SetBERSamplingPoint(size_t i, int64_t dx, float dy) =0;

	/**
		@brief Get the sampling point for BER measurements

		@param i	Channel index
		@param dx	Horizontal offset of the sampling point from the center of the eye, in femtoseconds
		@param dy	Vertical offset of the sampling point from the center of the eye, in volts
	 */
	virtual void GetBERSamplingPoint(size_t i, int64_t& dx, float& dy) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// RX capture status

	/**
		@brief Returns true if a horizontal bathtub scan is in progress

		@param i	Channel index
	 */
	virtual bool IsHBathtubScanInProgress(size_t i);

	/**
		@brief Returns true if an eye scan is in progress

		@param i	Channel index
	 */
	virtual bool IsEyeScanInProgress(size_t i);

	/**
		@brief Gets the estimated completion status (0-1) of the current scan

		@param i	Channel index
	 */
	virtual float GetScanProgress(size_t i);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// RX integration depth control

	/**
		@brief Determines whether this instrument has configurable integration depth for eye / bathtub scans
	 */
	virtual bool HasConfigurableScanDepth();

	/**
		@brief Returns the list of integration depths for eye / bathtub scans.

		Higher depths will give better resolution at low BER values, but increase scan time.

		@param i	Channel index
	 */
	virtual std::vector<int64_t> GetScanDepths(size_t i);

	/**
		@brief Get the configured scan depth for a channel, in UIs per sample/pixel

		@param i	Channel index
	 */
	virtual int64_t GetScanDepth(size_t i);

	/**
		@brief Set the configured scan depth for a channel

		@param i		Channel index
		@param depth	Integration depth, in UIs
	 */
	virtual void SetScanDepth(size_t i, int64_t depth);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Reference clock control

	/**
		@brief Returns true if this instrument has a reference clock input port
	 */
	virtual bool HasRefclkIn() =0;

	/**
		@brief Returns true if this instrument has a reference clock output port
	 */
	virtual bool HasRefclkOut() =0;

	/**
		@brief Gets the currently selected reference clock output mux setting
	 */
	virtual size_t GetRefclkOutMux() =0;

	/**
		@brief Sets the reference clock output mux

		@param i	Index of the mux within the table returned by GetRefclkOutMuxNames()
	 */
	virtual void SetRefclkOutMux(size_t i) =0;

	/**
		@brief Gets the list of available reference clock mux settings

		Mux selector names are free-form text whose meaning is defined by the instrument.
	 */
	virtual std::vector<std::string> GetRefclkOutMuxNames() =0;

	/**
		@brief Gets the currently selected refclk out frequency, in Hz
	 */
	virtual int64_t GetRefclkOutFrequency() =0;

	/**
		@brief Gets the refclk in frequency, in Hz, required to generate the currently selected data rate
	 */
	virtual int64_t GetRefclkInFrequency() =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Timebase

	/**
		@brief Determines whether the TX/RX clock is a per-instrument or per-channel setting

		@return True if clock is a per-channel setting, false if global
	 */
	virtual bool IsDataRatePerChannel() =0;

	/**
		@brief Gets the currently selected line rate (in symbols/sec)

		@param i	Channel index
	 */
	virtual int64_t GetDataRate(size_t i) =0;

	/**
		@brief Sets the data rate (in symbols/sec)

		@param i	Channel index
		@param rate Baud rate of the channel
	 */
	virtual void SetDataRate(size_t i, int64_t rate) =0;

	/**
		@brief Gets the list of available data rates, in symbols/sec
	 */
	virtual std::vector<int64_t> GetAvailableDataRates() =0;

	/**
		@brief Sets the reference clock source (internal or external)

		@param external	True to select external clock, false to select internal
	 */
	virtual void SetUseExternalRefclk(bool external) =0;

	/**
		@brief Sets the reference clock source

		@return True if external clock is in use, false if internal
	 */
	virtual bool GetUseExternalRefclk() =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Configuration storage

protected:
	/**
		@brief Serializes this BERT's configuration to a YAML node.

		@param node		Output node
		@param table	Table of IDs
	 */
	void DoSerializeConfiguration(YAML::Node& node, IDTable& table);

	/**
		@brief Load instrument and channel configuration from a save file

		@param version	File format version
		@param node		Output node
		@param table	Table of IDs
	 */
	void DoLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap);

	/**
		@brief Validate instrument and channel configuration from a save file

		@param version	File format version
		@param node		Output node
		@param table	Table of IDs
		@param list		List of warnings generated by the pre-load process
	 */
	void DoPreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& list);
};

#include "BERTInputChannel.h"
#include "BERTOutputChannel.h"

#endif
