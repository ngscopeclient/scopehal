/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

public:
	//Device information
	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();
	virtual unsigned int GetInstrumentTypes();
	virtual unsigned int GetMeasurementTypes();

	virtual void FlushConfigCache();

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i);
	virtual void EnableChannel(size_t i);
	virtual bool CanEnableChannel(size_t i);
	virtual void DisableChannel(size_t i);
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i);
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type);
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i);
	virtual double GetChannelAttenuation(size_t i);
	virtual void SetChannelAttenuation(size_t i, double atten);
	virtual int GetChannelBandwidthLimit(size_t i);
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz);
	virtual float GetChannelVoltageRange(size_t i, size_t stream);
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range);
	virtual OscilloscopeChannel* GetExternalTrigger();
	virtual float GetChannelOffset(size_t i, size_t stream);
	virtual void SetChannelOffset(size_t i, size_t stream, float offset);
	virtual std::string GetChannelDisplayName(size_t i);
	virtual void SetChannelDisplayName(size_t i, std::string name);
	virtual std::vector<unsigned int> GetChannelBandwidthLimiters(size_t i);
	virtual bool CanInvert(size_t i);
	virtual void Invert(size_t i, bool invert);
	virtual bool IsInverted(size_t i);

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool AcquireData();
	virtual void Start();
	virtual void StartSingleTrigger();
	virtual void Stop();
	virtual void ForceTrigger();
	virtual bool IsTriggerArmed();
	virtual void PushTrigger();
	virtual void PullTrigger();
	virtual void EnableTriggerOutput();
	virtual std::vector<std::string> GetTriggerTypes();

	//Scope models.
	//We only distinguish down to the series of scope, exact SKU is mostly irrelevant.
	enum Model
	{
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
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved();
	virtual std::vector<uint64_t> GetSampleRatesInterleaved();
	virtual std::set<InterleaveConflict> GetInterleaveConflicts();
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved();
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved();
	virtual uint64_t GetSampleRate();
	virtual uint64_t GetSampleDepth();
	virtual void SetSampleDepth(uint64_t depth);
	virtual void SetSampleRate(uint64_t rate);
	virtual void SetUseExternalRefclk(bool external);
	virtual bool IsInterleaving();
	virtual bool SetInterleaving(bool combine);

	virtual void SetTriggerOffset(int64_t offset);
	virtual int64_t GetTriggerOffset();
	virtual void SetDeskewForChannel(size_t channel, int64_t skew);
	virtual int64_t GetDeskewForChannel(size_t channel);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Function generator

	//Channel info
	virtual int GetFunctionChannelCount();
	virtual std::string GetFunctionChannelName(int chan);

	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan);

	//Configuration
	virtual bool GetFunctionChannelActive(int chan);
	virtual void SetFunctionChannelActive(int chan, bool on);

	virtual float GetFunctionChannelDutyCycle(int chan);
	virtual void SetFunctionChannelDutyCycle(int chan, float duty);

	virtual float GetFunctionChannelAmplitude(int chan);
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude);

	virtual float GetFunctionChannelOffset(int chan);
	virtual void SetFunctionChannelOffset(int chan, float offset);

	virtual float GetFunctionChannelFrequency(int chan);
	virtual void SetFunctionChannelFrequency(int chan, float hz);

	virtual WaveShape GetFunctionChannelShape(int chan);
	virtual void SetFunctionChannelShape(int chan, WaveShape shape);

	virtual float GetFunctionChannelRiseTime(int chan);
	virtual void SetFunctionChannelRiseTime(int chan, float sec);

	virtual float GetFunctionChannelFallTime(int chan);
	virtual void SetFunctionChannelFallTime(int chan, float sec);

	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan);
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Logic analyzer configuration

	virtual std::vector<DigitalBank> GetDigitalBanks();
	virtual DigitalBank GetDigitalBank(size_t channel);
	virtual bool IsDigitalHysteresisConfigurable();
	virtual bool IsDigitalThresholdConfigurable();
	virtual float GetDigitalHysteresis(size_t channel);
	virtual float GetDigitalThreshold(size_t channel);
	virtual void SetDigitalHysteresis(size_t channel, float level);
	virtual void SetDigitalThreshold(size_t channel, float level);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ADC bit depth configuration

	//All currently supported Sig2 scopes have only one analog bank (same ADC config for all channels)
	//so no need to override those

	enum ADCMode
	{
		ADC_MODE_8BIT	= 0,
		ADC_MODE_10BIT	= 1
	};

	virtual bool IsADCModeConfigurable();
	virtual std::vector<std::string> GetADCModeNames(size_t channel);
	virtual size_t GetADCMode(size_t channel);
	virtual void SetADCMode(size_t channel, size_t mode);

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

	//set of SW/HW options we have
	bool m_hasLA;
	bool m_hasDVM;
	bool m_hasFunctionGen;
	bool m_hasFastSampleRate;	 //-M models
	int m_memoryDepthOption;	 //0 = base, after that number is max sample count in millions
	bool m_hasI2cTrigger;
	bool m_hasSpiTrigger;
	bool m_hasUartTrigger;

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

	//External trigger input
	OscilloscopeChannel* m_extTrigChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;

	//Mutexing for thread safety
	std::recursive_mutex m_cacheMutex;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(SiglentSCPIOscilloscope)
};
#endif
