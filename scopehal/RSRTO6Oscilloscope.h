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

#ifndef RSRTO6Oscilloscope_h
#define RSRTO6Oscilloscope_h

class EdgeTrigger;

class RSRTO6Oscilloscope
	: public virtual SCPIOscilloscope
	, public virtual SCPIFunctionGenerator
{
public:
	RSRTO6Oscilloscope(SCPITransport* transport);
	virtual ~RSRTO6Oscilloscope();

	//not copyable or assignable
	RSRTO6Oscilloscope(const RSRTO6Oscilloscope& rhs) =delete;
	RSRTO6Oscilloscope& operator=(const RSRTO6Oscilloscope& rhs) =delete;

public:
	//Device information
	virtual unsigned int GetInstrumentTypes() const override;

	virtual void FlushConfigCache() override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;

	//Channel configuration
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;
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
	virtual std::vector<unsigned int> GetChannelBandwidthLimiters(size_t i) override;
	virtual float GetChannelVoltageRange(size_t i, size_t stream) override;
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range) override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;
	virtual std::string GetProbeName(size_t i) override;

	//Digital channel configuration
	virtual std::vector<DigitalBank> GetDigitalBanks() override;
	virtual DigitalBank GetDigitalBank(size_t channel) override;
	virtual bool IsDigitalHysteresisConfigurable() override;
	virtual bool IsDigitalThresholdConfigurable() override;
	// virtual float GetDigitalHysteresis(size_t channel) override;
	virtual float GetDigitalThreshold(size_t channel) override;
	// virtual void SetDigitalHysteresis(size_t channel, float level) override;
	virtual void SetDigitalThreshold(size_t channel, float level) override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;

	//Timebase
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual std::set<InterleaveConflict> GetInterleaveConflicts() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual uint64_t GetSampleRate() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Function generator

	//Channel info
	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) override;

	//Configuration
	virtual bool GetFunctionChannelActive(int chan) override;
	virtual void SetFunctionChannelActive(int chan, bool on) override;

	virtual bool HasFunctionDutyCycleControls(int chan) override;
	virtual float GetFunctionChannelDutyCycle(int chan) override;
	virtual void SetFunctionChannelDutyCycle(int chan, float duty) override;

	virtual float GetFunctionChannelAmplitude(int chan) override;
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude) override;

	virtual float GetFunctionChannelOffset(int chan) override;
	virtual void SetFunctionChannelOffset(int chan, float offset) override;

	virtual float GetFunctionChannelFrequency(int chan) override;
	virtual void SetFunctionChannelFrequency(int chan, float hz) override;

	virtual WaveShape GetFunctionChannelShape(int chan) override;
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) override;

	virtual bool HasFunctionRiseFallTimeControls(int chan) override;

	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan) override;
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z) override;

protected:
	OscilloscopeChannel* m_extTrigChannel;

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelBase;
	unsigned int m_digitalChannelCount;
	bool m_hasAFG;
	unsigned int m_firstAFGIndex;

	const static std::map<const std::string, const WaveShape> m_waveShapeNames;

	bool IsAnalog(size_t index)
	{ return index < m_analogChannelCount; }

	int HWDigitalNumber(size_t index)
	{ return index - m_digitalChannelBase; }

	template <typename T> size_t AcquireHeader(T* cap, std::string chname);

	//config cache
	std::map<size_t, float> m_channelOffsets;
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<int, bool> m_channelsEnabled;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCouplings;
	std::map<size_t, int> m_channelBandwidthLimits;
	std::map<size_t, double> m_channelAttenuations;

	bool m_triggerArmed;
	bool m_triggerOneShot;

	bool m_sampleRateValid;
	uint64_t m_sampleRate;
	bool m_sampleDepthValid;
	uint64_t m_sampleDepth;
	bool m_triggerOffsetValid;
	uint64_t m_triggerOffset;

	void PullEdgeTrigger();
	void PushEdgeTrigger(EdgeTrigger* trig);

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(RSRTO6Oscilloscope)
};

#endif
