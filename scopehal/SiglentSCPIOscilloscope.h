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

#ifndef SiglentSCPIOscilloscope_h
#define SiglentSCPIOscilloscope_h

#include <mutex>
#include <chrono>

class DropoutTrigger;
class EdgeTrigger;
class GlitchTrigger;
class PulseWidthTrigger;
class RuntTrigger;
class SlewRateTrigger;
class UartTrigger;
class WindowTrigger;

/**
	@brief A Siglent new generation scope based on linux (SDS2000X+/SDS5000/SDS6000)

 */

#define MAX_ANALOG 4
#define WAVEDESC_SIZE 346

// These SDS2000/SDS5000 scopes will actually sample 200MPoints, but the maximum it can transfer in one
// chunk is 10MPoints
// TODO(dannas): Can the Siglent SDS1104x-e really transfer 14MPoints? Update comment and constant
#define WAVEFORM_SIZE (14 * 1000 * 1000)

#define c_digiChannelsPerBus 8

class SiglentSCPIOscilloscope 	: public virtual SCPIOscilloscope
								, public virtual SCPIFunctionGenerator
{
public:
	SiglentSCPIOscilloscope(SCPITransport* transport);
	virtual ~SiglentSCPIOscilloscope();

	//not copyable or assignable
	SiglentSCPIOscilloscope(const SiglentSCPIOscilloscope& rhs) = delete;
	SiglentSCPIOscilloscope& operator=(const SiglentSCPIOscilloscope& rhs) = delete;

private:
	std::string converse(const char* fmt, ...);
	void sendOnly(const char* fmt, ...);

protected:
	void IdentifyHardware();
	void SharedCtorInit();
	virtual void DetectAnalogChannels();
	void AddDigitalChannels(unsigned int count);
	void DetectOptions();
	void ParseFirmwareVersion();

public:
	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual unsigned int GetMeasurementTypes();
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

	//Scope models.
	//We only distinguish down to the series of scope, exact SKU is mostly irrelevant.
	enum Model
	{
		MODEL_SIGLENT_SDS800X_HD,
		MODEL_SIGLENT_SDS1000,
		MODEL_SIGLENT_SDS2000XE,
		MODEL_SIGLENT_SDS2000XP,
		MODEL_SIGLENT_SDS2000X_HD,
		MODEL_SIGLENT_SDS5000X,
		MODEL_SIGLENT_SDS6000A,
		MODEL_UNKNOWN
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
	void PullDropoutTrigger();
	void PullEdgeTrigger();
	void PullPulseWidthTrigger();
	void PullRuntTrigger();
	void PullSlewRateTrigger();
	void PullUartTrigger();
	void PullWindowTrigger();
	void PullTriggerSource(Trigger* trig, std::string triggerModeName);

	void GetTriggerSlope(EdgeTrigger* trig, std::string reply);
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

	void BulkCheckChannelEnableState();

	std::string GetPossiblyEmptyString(const std::string& property);

	//  bool ReadWaveformBlock(std::string& data);
	int ReadWaveformBlock(uint32_t maxsize, char* data, bool hdSizeWorkaround = false);
	//  	bool ReadWavedescs(
	//		std::vector<std::string>& wavedescs,
	//		bool* enabled,
	//		unsigned int& firstEnabledChannel,
	//		bool& any_enabled);
	bool ReadWavedescs(
		char wavedescs[MAX_ANALOG][WAVEDESC_SIZE], bool* enabled, unsigned int& firstEnabledChannel, bool& any_enabled);

	void RequestWaveforms(bool* enabled, uint32_t num_sequences, bool denabled);
	time_t ExtractTimestamp(unsigned char* wavedesc, double& basetime);

	std::vector<WaveformBase*> ProcessAnalogWaveform(const char* data,
		size_t datalen,
		char* wavedesc,
		uint32_t num_sequences,
		time_t ttime,
		double basetime,
		double* wavetime,
		int i);
	std::map<int, SparseDigitalWaveform*> ProcessDigitalWaveform(std::string& data);

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelCount;
	size_t m_digitalChannelBase;

	Model m_modelid;

	// Firmware version
	int m_ubootMajorVersion;
	int m_ubootMinorVersion;
	int m_fwMajorVersion;
	int m_fwMinorVersion;
	int m_fwPatchVersion;
	int m_fwPatchRevision;

	//set of SW/HW options we have
	bool m_hasLA;
	bool m_hasDVM;
	bool m_hasFunctionGen;
	bool m_hasFastSampleRate;	 //-M models
	int m_memoryDepthOption;	 //0 = base, after that number is max sample count in millions
	bool m_hasI2cTrigger;
	bool m_hasSpiTrigger;
	bool m_hasUartTrigger;

	//SDS2000XP firmware <=1.3.6R6 has data size bug while in 10 bit mode
	bool m_requireSizeWorkaround;

	///Maximum bandwidth we support, in MHz
	unsigned int m_maxBandwidth;

	bool m_triggerArmed;
	bool m_triggerOneShot;
	bool m_triggerForced;

	// Transfer buffer. This is a bit hacky
	char m_analogWaveformData[MAX_ANALOG][WAVEFORM_SIZE];
	int m_analogWaveformDataSize[MAX_ANALOG];
	char m_wavedescs[MAX_ANALOG][WAVEDESC_SIZE];
	char m_digitalWaveformDataBytes[WAVEFORM_SIZE];
	std::string m_digitalWaveformData;

	//Cached configuration
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, float> m_channelOffsets;
	std::map<int, bool> m_channelsEnabled;
	bool m_sampleRateValid;
	int64_t m_sampleRate;
	bool m_memoryDepthValid;
	int64_t m_memoryDepth;
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
	std::map<size_t, FunctionGenerator::WaveShape> m_awgShape;
	std::map<size_t, FunctionGenerator::OutputImpedance> m_awgImpedance;
	ADCMode m_adcMode;
	bool m_adcModeValid;

	std::map<std::string, std::string> ParseCommaSeparatedNameValueList(std::string str, bool forwardMap = true);

	int64_t m_timeDiv;

	//True if we have >8 bit capture depth
	bool m_highDefinition;

	//Other channels
	OscilloscopeChannel* m_extTrigChannel;
	FunctionGeneratorChannel* m_awgChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(SiglentSCPIOscilloscope)
};
#endif
