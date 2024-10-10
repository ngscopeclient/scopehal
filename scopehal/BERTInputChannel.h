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
	@brief Declaration of BERTInputChannel
	@ingroup core
 */

#ifndef BERTInputChannel_h
#define BERTInputChannel_h

class BERT;
#include "EyeMask.h"

/**
	@brief A pattern checker channel of a BERT

	Derived from OscilloscopeChannel because we can output time domain bathtub curves etc

	@ingroup core
 */
class BERTInputChannel : public OscilloscopeChannel
{
public:

	BERTInputChannel(
		const std::string& hwname,
		std::weak_ptr<BERT> bert,
		const std::string& color = "#808080",
		size_t index = 0);

	virtual ~BERTInputChannel();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	///@brief Get the BERT we're part of
	std::weak_ptr<BERT> GetBERT() const
	{ return m_bert; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// All of these inline accessors don't null-check because the channel is contained by the parent BERT
	// (and thus it's impossible for us to outlive it)

	///@brief Get the polarity inversion state
	bool GetInvert()
	{ return m_bert.lock()->GetRxInvert(GetIndex()); }

	/**
		@brief Set the polarity inversion state

		@param invert	True for inverted polarity, false for normal
	 */
	void SetInvert(bool invert)
	{ m_bert.lock()->SetRxInvert(GetIndex(), invert); }

	///@brief Check if the CDR is currently locked
	bool GetCdrLockState()
	{ return m_bert.lock()->GetRxCdrLockState(GetIndex()); }

	/**
		@brief Set the pattern this channel is expecting to see

		@param pattern	Expected pattern
	 */
	void SetPattern(BERT::Pattern pattern)
	{ m_bert.lock()->SetRxPattern(GetIndex(), pattern); }

	///@brief Get the pattern this channel is expecting to see
	BERT::Pattern GetPattern()
	{ return m_bert.lock()->GetRxPattern(GetIndex()); }

	///@brief Get a list of all patterns this channel knows how to match
	std::vector<BERT::Pattern> GetAvailablePatterns()
	{ return m_bert.lock()->GetAvailableRxPatterns(GetIndex()); }

	/**
		@brief Gets the currently selected integration depth for eye / bathtub scans.

		@return Integration depth, in UIs per pixel or point
	 */
	int64_t GetScanDepth()
	{ return m_bert.lock()->GetScanDepth(GetIndex()); }

	///@brief Gets the available integration depths for eye / bathtub scans
	std::vector<int64_t> GetScanDepths()
	{ return m_bert.lock()->GetScanDepths(GetIndex()); }

	/**
		@brief Sets the currently selected integration depth for eye / bathtub scans.

		@param depth Integration depth, in UIs per pixel or point
	 */
	void SetScanDepth(int64_t depth)
	{ m_bert.lock()->SetScanDepth(GetIndex(), depth); }

	///@brief Well known indexes of output streams
	enum StreamIDs
	{
		///@brief Horizontal bathtub
		STREAM_HBATHTUB		= 0,

		///@param Eye pattern
		STREAM_EYE 			= 1,

		///@param Realtime bit error rate
		STREAM_BER 			= 2,

		///@param Mask hit rate
		STREAM_MASKHITRATE	= 3
	};

	///@brief Gets the stream for the horizontal bathtub output
	StreamDescriptor GetHBathtubStream()
	{ return StreamDescriptor(this, STREAM_HBATHTUB); }

	///@brief Gets the stream for the eye pattern output
	StreamDescriptor GetEyeStream()
	{ return StreamDescriptor(this, STREAM_EYE); }

	///@brief Gets the stream for the realtime BER output
	StreamDescriptor GetBERStream()
	{ return StreamDescriptor(this, STREAM_BER); }

	///@brief Returns true if the channel has a CTLE on the input
	bool HasCTLE()
	{ return m_bert.lock()->HasRxCTLE(); }

	///@brief Get the set of available gain steps for the CTLE, in dB
	std::vector<float> GetCTLEGainSteps()
	{ return m_bert.lock()->GetRxCTLEGainSteps(); }

	/**
		@brief Gets the currently selected CTLE gain step

		The actual gain value can be looked up in the table returned by GetCTLEGainSteps().
	 */
	size_t GetCTLEGainStep()
	{ return m_bert.lock()->GetRxCTLEGainStep(GetIndex()); }

	/**
		@brief Sets the gain for the CTLE

		The actual gain value can be looked up in the table returned by GetCTLEGainSteps().

		@param step		Step index
	 */
	void SetCTLEGainStep(size_t step)
	{ m_bert.lock()->SetRxCTLEGainStep(GetIndex(), step); }

	/**
		@brief Sets the sampling location for real time offset BER measurements

		@param dx	Horizontal offset of the sampling point from the center of the eye, in femtoseconds
		@param dy	Vertical offset of the sampling point from the center of the eye, in volts
	 */
	void SetBERSamplingPoint(int64_t dx, float dy)
	{ m_bert.lock()->SetBERSamplingPoint(GetIndex(), dx, dy); }

	/**
		@brief Gets the sampling location for real time offset BER measurements

		@param dx	Horizontal offset of the sampling point from the center of the eye, in femtoseconds
		@param dy	Vertical offset of the sampling point from the center of the eye, in volts
	 */
	void GetBERSamplingPoint(int64_t& dx, float& dy)
	{ m_bert.lock()->GetBERSamplingPoint(GetIndex(), dx, dy); }

	/**
		@brief Gets the data rate of this channel, in symbols per second
	 */
	int64_t GetDataRate()
	{ return m_bert.lock()->GetDataRate(GetIndex()); }

	/**
		@brief Sets the data rate of this channel, in symbols per second

		Depending on the clocking architecture of the instrument, this may affect other channels.

		For example, on MultiLane BERTs all channels share a single timebase. Antikernel Labs BERTs are
		based on Xilinx FPGAs and channels are divided into quads which each share some clocking resources.

		@param rate		Desired data rate
	 */
	void SetDataRate(int64_t rate)
	{ m_bert.lock()->SetDataRate(GetIndex(), rate); }

	///@brief Gets the path of the mask file for pass/fail testing (if any)
	std::string GetMaskFile()
	{ return m_maskFile; }

	/**
		@brief Sets the path of the mask file for pass/fail testing

		@param fname	Path of the YAML mask file
	 */
	void SetMaskFile(const std::string& fname);

	///@brief Gets the EyeMask being used for pass/fail testing (may be blank)
	EyeMask& GetMask()
	{ return m_mask; }

	virtual PhysicalConnector GetPhysicalConnector() override;

	/**
		@brief Estimate the time needed to capture a bathtub curve with the current settings

		This can be used for displaying progress bars, hints in the GUI, etc.

		@return	Expected capture time, in femtoseconds
	 */
	int64_t GetExpectedBathtubCaptureTime()
	{ return m_bert.lock()->GetExpectedBathtubCaptureTime(GetIndex()); }

	/**
		@brief Estimate the time needed to capture an eye pattern with the current settings

		This can be used for displaying progress bars, hints in the GUI, etc.

		@return	Expected capture time, in femtoseconds
	 */
	int64_t GetExpectedEyeCaptureTime()
	{ return m_bert.lock()->GetExpectedEyeCaptureTime(GetIndex()); }

	/**
		@brief Check if an eye scan is currently executing

		@return	 True if scan is in progress, false if not
	 */
	bool IsEyeScanInProgress()
	{ return m_bert.lock()->IsEyeScanInProgress(GetIndex()); }

	/**
		@brief Returns an estimate of the current scan progress (if an eye or bathtub scan is executing)

		@return Estimated progress in the range [0, 1]
	 */
	float GetScanProgress()
	{ return m_bert.lock()->GetScanProgress(GetIndex()); }

	/**
		@brief Check if a bathtub scan is currently executing

		@return	 True if scan is in progress, false if not
	 */
	bool IsHBathtubScanInProgress()
	{ return m_bert.lock()->IsHBathtubScanInProgress(GetIndex()); }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Vertical scaling and stream management

public:
	virtual void ClearStreams() override;
	virtual size_t AddStream(Unit yunit, const std::string& name, Stream::StreamType stype, uint8_t flags = 0) override;

	virtual float GetVoltageRange(size_t stream) override;
	virtual void SetVoltageRange(float range, size_t stream) override;

	virtual float GetOffset(size_t stream) override;
	virtual void SetOffset(float offset, size_t stream) override;

protected:

	///@brief Voltage range of each stream
	std::vector<float> m_ranges;

	///@brief Vertical offset of each stream
	std::vector<float> m_offsets;

	///@brief Path to the YAML file, if any, that m_mask was loaded from
	std::string m_maskFile;

	///@brief Eye mask used for pass/fail testing
	EyeMask m_mask;

protected:

	/**
		@brief Pointer to the parent instrument

		TODO: this needs to get refactored away so we can just use InstrumentChannel::m_instrument
	 */
	std::weak_ptr<BERT> m_bert;
};

#endif
