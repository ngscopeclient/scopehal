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

#ifndef AntikernelLabsTriggerCrossbar_h
#define AntikernelLabsTriggerCrossbar_h

/**
	@brief An AKL-TXB1 trigger crossbar
 */
class AntikernelLabsTriggerCrossbar
	: public virtual SCPIBERT
	, public virtual SCPIDevice
	, public virtual SwitchMatrix
	, public virtual SCPIOscilloscope
{
public:
	AntikernelLabsTriggerCrossbar(SCPITransport* transport);
	virtual ~AntikernelLabsTriggerCrossbar();

	virtual void PostCtorInit() override;

	virtual bool AcquireData() override;

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void SetChannelDisplayName(size_t i, std::string name) override;

	//TX pattern generator configuration
	virtual Pattern GetTxPattern(size_t i) override;
	virtual void SetTxPattern(size_t i, Pattern pattern) override;
	virtual std::vector<Pattern> GetAvailableTxPatterns(size_t i) override;
	virtual bool IsCustomPatternPerChannel() override;
	virtual size_t GetCustomPatternLength() override;
	virtual void SetGlobalCustomPattern(uint64_t pattern) override;
	virtual uint64_t GetGlobalCustomPattern() override;

	//TX driver configuration
	virtual bool GetTxInvert(size_t i) override;
	virtual void SetTxInvert(size_t i, bool invert) override;
	virtual std::vector<float> GetAvailableTxDriveStrengths(size_t i) override;
	virtual float GetTxDriveStrength(size_t i) override;
	virtual void SetTxDriveStrength(size_t i, float drive) override;
	virtual void SetTxEnable(size_t i, bool enable) override;
	virtual bool GetTxEnable(size_t i) override;
	virtual float GetTxPreCursor(size_t i) override;
	virtual void SetTxPreCursor(size_t i, float precursor) override;
	virtual float GetTxPostCursor(size_t i) override;
	virtual void SetTxPostCursor(size_t i, float postcursor) override;

	//RX input buffer configuration
	virtual bool GetRxInvert(size_t i) override;
	virtual void SetRxInvert(size_t i, bool invert) override;
	virtual bool HasRxCTLE() override;
	virtual std::vector<float> GetRxCTLEGainSteps() override;
	virtual size_t GetRxCTLEGainStep(size_t i) override;
	virtual void SetRxCTLEGainStep(size_t i, size_t step) override;

	//RX pattern checker configuration
	virtual Pattern GetRxPattern(size_t i) override;
	virtual void SetRxPattern(size_t i, Pattern pattern) override;
	virtual std::vector<Pattern> GetAvailableRxPatterns(size_t i) override;

	//RX data readout
	virtual bool GetRxCdrLockState(size_t i) override;
	virtual void MeasureHBathtub(size_t i) override;
	virtual void MeasureEye(size_t i) override;
	virtual void SetBERIntegrationLength(int64_t uis) override;
	virtual int64_t GetBERIntegrationLength() override;
	virtual void SetBERSamplingPoint(size_t i, int64_t dx, float dy) override;
	virtual void GetBERSamplingPoint(size_t i, int64_t& dx, float& dy) override;
	virtual bool HasConfigurableScanDepth() override;
	virtual std::vector<int64_t> GetScanDepths(size_t i) override;
	virtual int64_t GetScanDepth(size_t i) override;
	virtual void SetScanDepth(size_t i, int64_t depth) override;
	virtual int64_t GetExpectedBathtubCaptureTime(size_t i) override;
	virtual int64_t GetExpectedEyeCaptureTime(size_t i) override;
	virtual bool IsEyeScanInProgress(size_t i) override;
	virtual float GetScanProgress(size_t i) override;
	virtual bool IsHBathtubScanInProgress(size_t i) override;

	//Reference clock output
	virtual size_t GetRefclkOutMux() override;
	virtual void SetRefclkOutMux(size_t i) override;
	virtual std::vector<std::string> GetRefclkOutMuxNames() override;
	virtual int64_t GetRefclkOutFrequency() override;
	virtual int64_t GetRefclkInFrequency() override;

	//Timebase
	virtual bool IsDataRatePerChannel() override;
	virtual int64_t GetDataRate(size_t i) override;
	virtual void SetDataRate(size_t i, int64_t rate) override;
	virtual std::vector<int64_t> GetAvailableDataRates() override;
	virtual void SetUseExternalRefclk(bool external) override;
	virtual bool GetUseExternalRefclk() override;
	virtual bool HasRefclkIn() override;
	virtual bool HasRefclkOut() override;

	//Switch matrix
	virtual void SetMuxPath(size_t dstchan, size_t srcchan) override;
	virtual void SetMuxPathOpen(size_t dstchan) override;
	virtual bool MuxHasConfigurableDrive(size_t dstchan) override;
	virtual float GetMuxOutputDrive(size_t dstchan) override;
	virtual void SetMuxOutputDrive(size_t dstchan, float v) override;
	virtual bool MuxHasConfigurableThreshold(size_t dstchan) override;
	virtual float GetMuxInputThreshold(size_t dstchan) override;
	virtual void SetMuxInputThreshold(size_t dstchan, float v) override;

	//Oscilloscope/LA
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) override;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) override;
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual float GetChannelVoltageRange(size_t i, size_t stream) override;
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range) override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual bool IsTriggerArmed() override;
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual uint64_t GetSampleRate() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;
	virtual bool CanInterleave() override;
	virtual std::set< InterleaveConflict > GetInterleaveConflicts() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;

protected:

	size_t m_triggerInChannelBase;
	size_t m_triggerBidirChannelBase;
	size_t m_triggerOutChannelBase;
	size_t m_txChannelBase;
	size_t m_rxChannelBase;

	/**
		@brief Returns the number of points on either side of the center for a full eye/bathtub scan

		The full scan width is 2*halfwidth + 1.
	 */
	ssize_t GetScanHalfWidth(size_t i)
	{ return 16 << m_rxClkDiv[i - m_rxChannelBase]; }

	//Cached settings for trigger channels
	float m_trigDrive[12];
	float m_trigThreshold[12];

	//Cached settings for bert channels
	Pattern m_txPattern[2];
	//Pattern m_rxPattern[2];
	bool m_txInvert[2];
	bool m_rxInvert[2];
	float m_txDrive[2];
	bool m_txEnable[2];
	float m_txPreCursor[2];
	float m_txPostCursor[2];
	int64_t m_scanDepth[2];
	/*
	bool m_rxLock[4];
	uint64_t m_txCustomPattern;
	size_t m_rxCtleGainSteps[4];
	int64_t m_integrationLength;
	int64_t m_sampleX[4];
	float m_sampleY[4];
	*/
	uint64_t m_txDataRate[2];
	uint64_t m_rxDataRate[2];
	int64_t m_rxClkDiv[2];

	/**
		@brief True if in a constructor or similar initialization path (getting hardware state)

		This prevents filter graph changes from being pushed to hardware if we've just pulled the same path.
	 */
	bool m_loadInProgress;

	std::atomic<bool> m_bathtubScanInProgress;
	std::atomic<bool> m_eyeScanInProgress;
	std::atomic<size_t> m_activeScanChannel;
	std::atomic<float> m_activeScanProgress;

	//Logic analyzer config
	bool m_laChannelEnabled[2];
	uint64_t m_maxLogicDepth;
	bool m_triggerArmed;
	bool m_triggerOneShot;

public:
	static std::string GetDriverNameInternal();
	BERT_INITPROC(AntikernelLabsTriggerCrossbar)
};

#endif
