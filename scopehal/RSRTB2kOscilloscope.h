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

#ifndef RSRTB2kOscilloscope_h
#define RSRTB2kOscilloscope_h

/**
	@brief R&S RTB2000/RTB2 Oscilloscope

 */

class RSRTB2kEdgeTrigger;
class RSRTB2kLineTrigger;
class RSRTB2kRiseTimeTrigger;
class RSRTB2kRuntTrigger;
class RSRTB2kTimeoutTrigger;
class RSRTB2kVideoTrigger;
class RSRTB2kWidthTrigger;

#define MAX_ANALOG 4
#define MAX_DIGITAL 16
#define MAX_DIGITAL_POD 2
#define LOGICPOD1 51
#define LOGICPOD2 52

#define c_digiChannelsPerBus 8

class RSRTB2kOscilloscope
	: public virtual SCPIOscilloscope
	, public virtual SCPIFunctionGenerator
{
public:
	RSRTB2kOscilloscope(SCPITransport* transport);
	virtual ~RSRTB2kOscilloscope();

	//not copyable or assignable
	RSRTB2kOscilloscope(const RSRTB2kOscilloscope& rhs) = delete;
	RSRTB2kOscilloscope& operator=(const RSRTB2kOscilloscope& rhs) = delete;

private:
	std::string converse(const char* fmt, ...);
	void sendOnly(const char* fmt, ...);
	bool sendWithAck(const char* fmt, ...);
	void flush();
	void protocolError(bool flush, const char* fmt, va_list ap);
	void protocolError(const char* fmt, ...);
	void protocolErrorWithFlush(const char* fmt, ...);

protected:
	void IdentifyHardware();
	void DetectOptions();
	virtual void AddAnalogChannels();
	void AddDigitalChannels();
	void AddExternalTriggerChannel();
	void AddLineTriggerChannel();
	void AddAwgChannel();

public:
	// Specific 16 bit conversion methods for uint16_t
	static void Convert16BitSamples(float* pout, const uint16_t* pin, float gain, float offset, size_t count);
	static void Convert16BitSamplesGeneric(float* pout, const uint16_t* pin, float gain, float offset, size_t count);
	// Specific 8 bit conversion methods for uint8_t
	static void Convert8BitSamples(float* pout, const uint8_t* pin, float gain, float offset, size_t count);
	static void Convert8BitSamplesGeneric(float* pout, const uint8_t* pin, float gain, float offset, size_t count);

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void FlushConfigCache() override;

	void ForceHDMode(bool mode);

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual bool CanEnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) override;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) override;
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual float GetChannelVoltageRange(size_t i, size_t stream) override;
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;
	virtual std::string GetChannelDisplayName(size_t i) override;
	virtual void SetChannelDisplayName(size_t i, std::string name) override;
	virtual std::vector<unsigned int> GetChannelBandwidthLimiters(size_t i) override;
	virtual bool CanInvert(size_t i) override;
	virtual void Invert(size_t i, bool invert) override;
	virtual bool IsInverted(size_t i) override;

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
	virtual void EnableTriggerOutput() override;
	virtual std::vector<std::string> GetTriggerTypes() override;

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
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;

	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Function generator

	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) override;

	//Configuration
	virtual bool GetFunctionChannelActive(int chan) override;
	virtual void SetFunctionChannelActive(int chan, bool on) override;

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

	virtual float GetFunctionChannelRiseTime(int) override;

	virtual void SetFunctionChannelRiseTime(int, float) override;

	virtual void SetFunctionChannelFallTime(int, float) override;

	virtual float GetFunctionChannelFallTime(int chan) override;


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Logic analyzer configuration

	virtual std::vector<DigitalBank> GetDigitalBanks() override;
	virtual DigitalBank GetDigitalBank(size_t channel) override;
	virtual bool IsDigitalHysteresisConfigurable() override;
	virtual bool IsDigitalThresholdConfigurable() override;
	virtual float GetDigitalHysteresis(size_t channel) override;
	virtual float GetDigitalThreshold(size_t channel) override;
	virtual void SetDigitalHysteresis(size_t channel, float level) override;
	virtual void SetDigitalThreshold(size_t channel, float level) override;

protected:
    struct Metadata {
        uint32_t sampleCount;
        uint32_t bytesPerSample;
        float verticalStart;
        float verticalStep;
        float interval;
    };

    struct Logicpod {
		bool enabled;
		unsigned int progressChannel;
	};

	std::string PullTriggerSourceNumber(bool noDigital);

	void PullTriggerSource(Trigger* trig, std::string triggerModeName, bool isUart);
	void PullEdgeTrigger();
	void PullLineTrigger();
	void PullRiseTimeTrigger();
	void PullRuntTrigger();
	void PullTimeoutTrigger();
	void PullVideoTrigger();
	void PullWidthTrigger();

	void GetTriggerHysteresis(Trigger* trig, std::string reply);
	void GetTriggerSlope(Trigger* trig, std::string reply);
	void GetTriggerCoupling(Trigger* trig, std::string reply);
	Trigger::Condition GetCondition(std::string reply);

	void PushCondition(const std::string& path, Trigger::Condition cond);
	void PushFloat(std::string path, float f);
	void PushEdgeTrigger(RSRTB2kEdgeTrigger* trig, const std::string trigType);
	void PushLineTrigger(RSRTB2kLineTrigger* trig);
	void PushRiseTimeTrigger(RSRTB2kRiseTimeTrigger* trig);
	void PushRuntTrigger(RSRTB2kRuntTrigger* trig);
	void PushTimeoutTrigger(RSRTB2kTimeoutTrigger* trig);
	void PushVideoTrigger(RSRTB2kVideoTrigger* trig);
	void PushWidthTrigger(RSRTB2kWidthTrigger* trig);

	void BulkCheckChannelEnableState();
	void PrepareAcquisition();
	void SetupForAcquisition();

	std::string GetDigitalChannelBankName(size_t channel);
	std::string GetChannelName(size_t channel);
	bool GetActiveChannels(bool* pod1, bool* pod2, bool* chan1, bool* chan2, bool* chan3, bool* chan4);

	size_t ReadWaveformBlock(std::vector<uint8_t>* data, Metadata* metadata, std::function<void(float)> progress = nullptr);

	std::vector<WaveformBase*> ProcessAnalogWaveform(
		const std::vector<uint8_t>& data,
		size_t dataLen,
		time_t ttime,
		uint32_t sampleCount,
		uint32_t bytePerSample,
		float verticalStep,
		float verticalStart,
		float interval,
		int i);

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelCount;
	unsigned int m_analogAndDigitalChannelCount;
	size_t m_digitalChannelBase;

	//set of SW/HW options we have
	bool m_hasLA;
	bool m_hasDVM;
	bool m_hasFunctionGen;
	bool m_hasI2cTrigger;
	bool m_hasSpiTrigger;
	bool m_hasUartTrigger;
	bool m_hasCanTrigger;
	bool m_hasLinTrigger;

	//Maximum bandwidth we support, in MHz
	unsigned int m_maxBandwidth;

	bool m_triggerArmed;
	bool m_triggerOneShot;
	bool m_triggerForced;

	//Cached configuration
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, float> m_channelOffsets;
	std::map<std::string, float> m_channelDigitalHysteresis;
	std::map<std::string, float> m_channelDigitalThresholds;
	std::map<int, bool> m_channelsEnabled;
	bool m_sampleRateValid;
	int64_t m_sampleRate;
	bool m_memoryDepthValid;
	int64_t m_memoryDepth;
	bool m_memoryDepthAuto;
	bool m_triggerOffsetValid;
	int64_t m_triggerOffset;
	int64_t m_triggerReference;
	//~ std::map<size_t, int64_t> m_channelDeskew;
	//~ Multimeter::MeasurementTypes m_meterMode;
	//~ bool m_meterModeValid;
	std::map<size_t, bool> m_awgEnabled;
	std::map<size_t, float> m_awgDutyCycle;
	std::map<size_t, float> m_awgRange;
	std::map<size_t, float> m_awgOffset;
	std::map<size_t, float> m_awgFrequency;
	std::map<size_t, float> m_awgRiseTime;
	std::map<size_t, float> m_awgFallTime;
	std::map<size_t, FunctionGenerator::WaveShape> m_awgShape;
	std::map<size_t, FunctionGenerator::OutputImpedance> m_awgImpedance;

	//True if we have >8 bit capture depth
	bool m_highDefinition;

	//Other channels
	OscilloscopeChannel* m_extTrigChannel;
	OscilloscopeChannel* m_lineTrigChannel;
	FunctionGeneratorChannel* m_awgChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(RSRTB2kOscilloscope)
};
#endif
