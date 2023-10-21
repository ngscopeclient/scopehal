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

#ifndef MultiLaneBERT_h
#define MultiLaneBERT_h

/**
	@brief A MultiLANE BERT accessed via scopehal-mlbert-bridge
 */
class MultiLaneBERT
	: public virtual SCPIBERT
	, public virtual SCPIDevice
{
public:
	MultiLaneBERT(SCPITransport* transport);
	virtual ~MultiLaneBERT();

	virtual bool AcquireData() override;

	//Device information
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

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

	//Reference clock output
	virtual size_t GetRefclkOutMux() override;
	virtual void SetRefclkOutMux(size_t i) override;
	virtual std::vector<std::string> GetRefclkOutMuxNames() override;
	virtual int64_t GetRefclkOutFrequency() override;
	virtual int64_t GetRefclkInFrequency() override;

	//Timebase
	virtual int64_t GetDataRate() override;
	virtual void SetDataRate(int64_t rate) override;
	virtual std::vector<int64_t> GetAvailableDataRates() override;
	virtual void SetUseExternalRefclk(bool external) override;
	virtual bool GetUseExternalRefclk() override;

protected:

	int m_rxChannelBase;

	//Cached settings
	Pattern m_txPattern[4];
	Pattern m_rxPattern[4];
	bool m_txInvert[4];
	bool m_rxInvert[4];
	float m_txDrive[4];
	bool m_txEnable[4];
	float m_txPreCursor[4];
	float m_txPostCursor[4];
	bool m_rxLock[4];
	uint64_t m_txCustomPattern;
	size_t m_refclkOutMux;
	size_t m_rxCtleGainSteps[4];
	int64_t m_integrationLength;
	int64_t m_sampleX[4];
	float m_sampleY[4];
	bool m_useExternalRefclk;

	enum RefclkMuxSelectors
	{
		RX0_DIV8,
		RX0_DIV16,
		RX1_DIV8,
		RX1_DIV16,
		RX2_DIV8,
		RX2_DIV16,
		RX3_DIV8,
		RX3_DIV16,
		LO_DIV32_OR_80,	//div32 in low rate, div80 in high rate
		SERDES
	};

	uint64_t m_dataRate;

public:
	static std::string GetDriverNameInternal();
	BERT_INITPROC(MultiLaneBERT)
};

#endif
