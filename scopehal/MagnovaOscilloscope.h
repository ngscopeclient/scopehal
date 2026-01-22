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

#ifndef MagnovaOscilloscope_h
#define MagnovaOscilloscope_h

#include <mutex>
#include <chrono>
#include "NthEdgeBurstTrigger.h"

class DropoutTrigger;
class EdgeTrigger;
class GlitchTrigger;
class PulseWidthTrigger;
class RuntTrigger;
class SlewRateTrigger;
class UartTrigger;
class WindowTrigger;

/**
	@brief Batronix's Magnova Oscilloscope

 */

#define MAX_ANALOG 4
#define MAX_DIGITAL 16

#define c_digiChannelsPerBus 8

class MagnovaOscilloscope 	: public virtual SCPIOscilloscope, public virtual SCPIFunctionGenerator
{
public:
	MagnovaOscilloscope(SCPITransport* transport);
	virtual ~MagnovaOscilloscope();

	//not copyable or assignable
	MagnovaOscilloscope(const MagnovaOscilloscope& rhs) = delete;
	MagnovaOscilloscope& operator=(const MagnovaOscilloscope& rhs) = delete;

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
	void DetectBandwidth();
	void SharedCtorInit();
	virtual void DetectAnalogChannels();
	void AddDigitalChannels(unsigned int count);
	void DetectOptions();
	void ParseFirmwareVersion();

public:
	// Specific 16 bit conversion methods for uint16_t
	static void Convert16BitSamples(float* pout, const uint16_t* pin, float gain, float offset, size_t count);
	static void Convert16BitSamplesGeneric(float* pout, const uint16_t* pin, float gain, float offset, size_t count);

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual unsigned int GetMeasurementTypes();
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void FlushConfigCache() override;

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

	//Scope models.
	//We only distinguish down to the series of scope, exact SKU is mostly irrelevant.
	enum Model
	{
		MODEL_MAGNOVA_BMO,
		MODEL_UNKNOWN
	};

	// Memory depth mode
	enum MemoryDepthMode
	{
		MEMORY_DEPTH_AUTO_FAST,
		MEMORY_DEPTH_AUTO_MAX,
		MEMORY_DEPTH_FIXED
	};

	// Memory depth mode
	enum CaptureMode
	{
		CAPTURE_MODE_NORMAL,
		CAPTURE_MODE_EXTENDED
	};


	Model GetModelID() { return m_modelid; }

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
	virtual void SetUseExternalRefclk(bool external) override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;

	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;
	virtual void SetDeskewForChannel(size_t channel, int64_t skew) override;
	virtual int64_t GetDeskewForChannel(size_t channel) override;

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

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ADC bit depth configuration

	//All currently supported Sig2 scopes have only one analog bank (same ADC config for all channels)
	//so no need to override those

	enum ADCMode
	{
		ADC_MODE_8BIT	= 0,
		ADC_MODE_10BIT	= 1
	};

	virtual bool IsADCModeConfigurable() override;
	virtual std::vector<std::string> GetADCModeNames(size_t channel) override;
	virtual size_t GetADCMode(size_t channel) override;
	virtual void SetADCMode(size_t channel, size_t mode) override;

protected:
    struct Metadata {
        float timeDelta;
        float startTime;
        float endTime;
        uint32_t sampleCount;
        uint32_t sampleStart;
        uint32_t sampleLength;
        float verticalStart;
        float verticalStep;
    };
    /**
     * @brief Parse metadata from the oscilloscope data
     * @param data Raw data from oscilloscope
     * @param data_transfer_type Data transfer type
     * @return Parsed metadata
     */
    std::optional<Metadata> parseMetadata(const std::vector<uint8_t>& data);


	void PullDropoutTrigger();
	void PullEdgeTrigger();
	void PullPulseWidthTrigger();
	void PullRuntTrigger();
	void PullSlewRateTrigger();
	void PullUartTrigger();
	void PullWindowTrigger();
	void PullGlitchTrigger();
	void PullNthEdgeBurstTrigger();
	void PullTriggerSource(Trigger* trig, std::string triggerModeName, bool isUart);

	void GetTriggerSlope(Trigger* trig, std::string reply);
	Trigger::Condition GetCondition(std::string reply);

	void PushDropoutTrigger(DropoutTrigger* trig);
	void PushEdgeTrigger(EdgeTrigger* trig, const std::string trigType);
	void PushGlitchTrigger(GlitchTrigger* trig);
	void PushCondition(const std::string& path, Trigger::Condition cond);
	void PushPatternCondition(const std::string& path, Trigger::Condition cond);
	void PushFloat(std::string path, float f);
	void PushPulseWidthTrigger(PulseWidthTrigger* trig);
	void PushRuntTrigger(RuntTrigger* trig);
	void PushSlewRateTrigger(SlewRateTrigger* trig);
	void PushUartTrigger(UartTrigger* trig);
	void PushWindowTrigger(WindowTrigger* trig);
	void PushNthEdgeBurstTrigger(NthEdgeBurstTrigger* trig);

	void BulkCheckChannelEnableState();

	unsigned int GetActiveChannelsCount();

	double GetTimebaseScale();

	bool IsReducedSampleRate();

	uint64_t GetMemoryDepthForSrate(uint64_t srate);

	uint64_t GetMaxAutoMemoryDepth(uint64_t original);

	CaptureMode GetCaptureMode();

	static double GetCaptureScreenDivisions(MemoryDepthMode memoryMode, CaptureMode captureMode, uint64_t srate);

	void PrepareAcquisition();

	std::string GetDigitalChannelBankName(size_t channel);

	std::string GetChannelName(size_t channel);

	std::string GetPossiblyEmptyString(const std::string& property);

	size_t ReadWaveformBlock(std::vector<uint8_t>* data, std::function<void(float)> progress = nullptr);

	time_t ExtractTimestamp(const std::string& time, double& basetime);

	std::vector<WaveformBase*> ProcessAnalogWaveform(
		const std::vector<uint8_t>& data,
		size_t datalen,
		uint32_t num_sequences,
		time_t ttime,
		double basetime,
		double* wavetime,
		int i);
	
	std::vector<SparseDigitalWaveform*> ProcessDigitalWaveform(
		const std::vector<uint8_t>& data,
		size_t datalen,
		uint32_t num_sequences,
		time_t ttime,
		double basetime,
		double* wavetime,
		int i);
	
	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelCount;
	unsigned int m_analogAndDigitalChannelCount;
	size_t m_digitalChannelBase;

	Model m_modelid;

	// Firmware version
	int m_fwMajorVersion;
	int m_fwMinorVersion;
	int m_fwPatchVersion;

	//set of SW/HW options we have
	bool m_hasLA;
	bool m_hasDVM;
	bool m_hasFunctionGen;
	bool m_hasI2cTrigger;
	bool m_hasSpiTrigger;

	///Maximum bandwidth we support, in MHz
	unsigned int m_maxBandwidth;

	bool m_triggerArmed;
	bool m_triggerOneShot;
	bool m_triggerForced;

	//Cached configuration
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, float> m_channelOffsets;
	std::map<std::string, float> m_channelDigitalThresholds;
	std::map<int, bool> m_channelsEnabled;
	bool m_sampleRateValid;
	int64_t m_sampleRate;
	bool m_memoryDepthValid;
	int64_t m_memoryDepth;
	bool m_captureModeValid;
	CaptureMode m_captudeMode;
	bool m_timebaseScaleValid;
	double m_timebaseScale;
	MemoryDepthMode m_memoryDepthMode;
	bool m_triggerOffsetValid;
	int64_t m_triggerOffset;
	std::map<size_t, int64_t> m_channelDeskew;
	Multimeter::MeasurementTypes m_meterMode;
	bool m_meterModeValid;
	std::map<size_t, bool> m_probeIsActive;
	std::map<size_t, bool> m_awgEnabled;
	std::map<size_t, float> m_awgDutyCycle;
	std::map<size_t, float> m_awgRange;
	std::map<size_t, float> m_awgOffset;
	std::map<size_t, float> m_awgFrequency;
	std::map<size_t, float> m_awgRiseTime;
	std::map<size_t, float> m_awgFallTime;
	std::map<size_t, FunctionGenerator::WaveShape> m_awgShape;
	std::map<size_t, FunctionGenerator::OutputImpedance> m_awgImpedance;
	ADCMode m_adcMode;
	bool m_adcModeValid;

	int64_t m_timeDiv;

	//Other channels
	OscilloscopeChannel* m_extTrigChannel;
	FunctionGeneratorChannel* m_awgChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;

	// Maps for srate to memory depth for auto memory mode
private:
    const std::map<uint64_t, uint64_t> memoryDepthFastMap {
        {2, 12000000},
        {5, 12000000},
        {10, 12000000},
        {40, 19200000},
        {50, 12000000},
        {100, 12000000},
        {400, 19200000},
        {500, 12000000},
        {1000, 12000000},
        {4000, 19200000},
        {5000, 12000000},
        {10000, 12000000},
        {40000, 19200000},
        {50000, 12000000},
        {100000, 12000000},
        {400000, 19200000},
        {500000, 12000000},
        {1000000, 12000000},
        {2500000, 12000000},
        {4000000, 19200000},
        {5000000, 12000000},
        {10000000, 12000000},
        {25000000, 12000000},
        {40000000, 19200000},
        {50000000, 12000000},
        {100000000, 12000000},
        {125000000, 15000000},
        {250000000, 12000000},
        {400000000, 19200000},
        {500000000, 12000000},
        {800000000, 19200000},
        {1000000000, 12000000},
        {1600000000, 19200000}
    };
    const std::map<uint64_t, uint64_t> memoryDepthMaxLowSrateMap {
        {25, 120000000},
        {50, 120000000},
        {100, 120000000},
        {250, 120000000},
        {500, 120000000},
        {1000, 120000000},
        {2500, 120000000},
        {5000, 120000000},
        {10000, 120000000},
        {25000, 120000000},
        {50000, 120000000},
        {100000, 120000000},
        {250000, 120000000},
        {500000, 120000000},
        {1000000, 120000000},
        {2500000, 120000000},
        {5000000, 120000000},
        {10000000, 120000000},
        {25000000, 120000000},
        {50000000, 120000000},
        {125000000, 150000000},
        {250000000, 120000000},
        {500000000, 120000000},
        {1000000000, 120000000}
    };
    const std::map<uint64_t, uint64_t> memoryDepthMaxHighSrateMap {
        {50, 240000000},
        {100, 240000000},
        {250, 300000000},
        {500, 240000000},
        {1000, 240000000},
        {2500, 300000000},
        {5000, 240000000},
        {10000, 240000000},
        {25000, 300000000},
        {50000, 240000000},
        {100000, 240000000},
        {250000, 300000000},
        {500000, 240000000},
        {1000000, 240000000},
        {2500000, 300000000},
        {5000000, 240000000},
        {10000000, 240000000},
        {25000000, 300000000},
        {50000000, 240000000},
        {100000000, 240000000},
        {200000000, 240000000},
        {400000000, 192000000},
        {800000000, 192000000},
        {1600000000, 192000000}
    };

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(MagnovaOscilloscope)
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels();
};
#endif
