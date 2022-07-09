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

#ifndef LeCroyOscilloscope_h
#define LeCroyOscilloscope_h

#include <mutex>

class DropoutTrigger;
class EdgeTrigger;
class GlitchTrigger;
class PulseWidthTrigger;
class RuntTrigger;
class SlewRateTrigger;
class UartTrigger;
class WindowTrigger;
class CDR8B10BTrigger;
class CDRNRZPatternTrigger;

/**
	@brief A Teledyne LeCroy oscilloscope using the MAUI/XStream command set.

	May not work on lower-end instruments that are rebranded third-party hardware.
 */
class LeCroyOscilloscope
	: public virtual SCPIOscilloscope
	, public virtual SCPIMultimeter
	, public FunctionGenerator
{
public:
	LeCroyOscilloscope(SCPITransport* transport);
	virtual ~LeCroyOscilloscope();

	//not copyable or assignable
	LeCroyOscilloscope(const LeCroyOscilloscope& rhs) =delete;
	LeCroyOscilloscope& operator=(const LeCroyOscilloscope& rhs) =delete;

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
	virtual bool CanAutoZero(size_t i);
	virtual void AutoZero(size_t i);
	virtual std::string GetProbeName(size_t i);
	virtual bool HasInputMux(size_t i);
	virtual size_t GetInputMuxSetting(size_t i);
	virtual std::vector<std::string> GetInputMuxNames(size_t i);
	virtual void SetInputMux(size_t i, size_t select);

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool PeekTriggerArmed();
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
	bool IsCDRLocked();

	//DMM configuration
	virtual int GetMeterChannelCount();
	virtual std::string GetMeterChannelName(int chan);
	virtual int GetCurrentMeterChannel();
	virtual void SetCurrentMeterChannel(int chan);
	virtual void StartMeter();
	virtual void StopMeter();
	virtual void SetMeterAutoRange(bool enable);
	virtual bool GetMeterAutoRange();
	virtual double GetMeterValue();
	virtual Multimeter::MeasurementTypes GetMeterMode();
	virtual void SetMeterMode(Multimeter::MeasurementTypes type);
	virtual int GetMeterDigits();

	//Function generator
	virtual int GetFunctionChannelCount();
	virtual std::string GetFunctionChannelName(int chan);
	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan);
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
	virtual FunctionGenerator::WaveShape GetFunctionChannelShape(int chan);
	virtual void SetFunctionChannelShape(int chan, WaveShape shape);
	virtual float GetFunctionChannelRiseTime(int chan);
	virtual void SetFunctionChannelRiseTime(int chan, float sec);
	virtual float GetFunctionChannelFallTime(int chan);
	virtual void SetFunctionChannelFallTime(int chan, float sec);
	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan);
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z);

	//Scope models.
	//We only distinguish down to the series of scope, exact SKU is mostly irrelevant.
	enum Model
	{
		MODEL_DDA_5K,

		MODEL_HDO_4KA,
		MODEL_HDO_6KA,
		MODEL_HDO_9K,

		MODEL_LABMASTER_ZI_A,

		MODEL_MDA_800,

		MODEL_SDA_3K,

		MODEL_SDA_8ZI,
		MODEL_SDA_8ZI_A,
		MODEL_SDA_8ZI_B,
		MODEL_WAVEMASTER_8ZI,
		MODEL_WAVEMASTER_8ZI_A,
		MODEL_WAVEMASTER_8ZI_B,

		MODEL_WAVEPRO_HD,

		MODEL_WAVERUNNER_8K,
		MODEL_WAVERUNNER_8K_HD,
		MODEL_WAVERUNNER_9K,

		MODEL_WAVESURFER_3K,

		MODEL_UNKNOWN
	};

	Model GetModelID()
	{ return m_modelid; }

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
	virtual bool IsSamplingModeAvailable(SamplingMode mode);
	virtual SamplingMode GetSamplingMode();
	virtual void SetSamplingMode(SamplingMode mode);

	//DBI mode
	bool HasDBICapability();
	bool IsDBIEnabled(size_t channel);

	virtual void SetTriggerOffset(int64_t offset);
	virtual int64_t GetTriggerOffset();
	virtual void SetDeskewForChannel(size_t channel, int64_t skew);
	virtual int64_t GetDeskewForChannel(size_t channel);

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

	//All currently supported LeCroy scopes have only one analog bank (same ADC config for all channels)
	//so no need to override those

	virtual bool IsADCModeConfigurable();
	virtual std::vector<std::string> GetADCModeNames(size_t channel);
	virtual size_t GetADCMode(size_t channel);
	virtual void SetADCMode(size_t channel, size_t mode);

protected:

	//Trigger config
	void Pull8b10bTrigger();
	void PullNRZTrigger();
	void PullDropoutTrigger();
	void PullEdgeTrigger();
	void PullGlitchTrigger();
	void PullPulseWidthTrigger();
	void PullRuntTrigger();
	void PullSlewRateTrigger();
	void PullUartTrigger();
	void PullWindowTrigger();
	void PullTriggerSource(Trigger* trig);

	void GetTriggerSlope(EdgeTrigger* trig, std::string reply);
	Trigger::Condition GetCondition(std::string reply);

	void Push8b10bTrigger(CDR8B10BTrigger* trig);
	void PushNRZTrigger(CDRNRZPatternTrigger* trig);
	void PushDropoutTrigger(DropoutTrigger* trig);
	void PushEdgeTrigger(EdgeTrigger* trig, const std::string& tree);
	void PushGlitchTrigger(GlitchTrigger* trig);
	void PushCondition(const std::string& path, Trigger::Condition cond);
	void PushPatternCondition(const std::string& path, Trigger::Condition cond);
	void PushFloat(std::string path, float f);
	void PushPulseWidthTrigger(PulseWidthTrigger* trig);
	void PushRuntTrigger(RuntTrigger* trig);
	void PushSlewRateTrigger(SlewRateTrigger* trig);
	void PushUartTrigger(UartTrigger* trig);
	void PushWindowTrigger(WindowTrigger* trig);

	void OnCDRTriggerAutoBaud();

	void BulkCheckChannelEnableState();

	std::string GetPossiblyEmptyString(const std::string& property);

	bool ReadWaveformBlock(std::string& data);
	bool ReadWavedescs(
		std::vector<std::string>& wavedescs,
		bool* enabled,
		unsigned int& firstEnabledChannel,
		bool& any_enabled);
	void RequestWaveforms(bool* enabled, uint32_t num_sequences, bool denabled);
	time_t ExtractTimestamp(unsigned char* wavedesc, double& basetime);
	std::vector<WaveformBase*> ProcessAnalogWaveform(
		const char* data,
		size_t datalen,
		std::string& wavedesc,
		uint32_t num_sequences,
		time_t ttime,
		double basetime,
		double* wavetime
		);
	std::map<int, DigitalWaveform*> ProcessDigitalWaveform(std::string& data, int64_t analog_hoff);

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelCount;
	size_t m_digitalChannelBase;

	Model m_modelid;

	//set of SW/HW options we have
	bool m_hasLA;
	bool m_hasDVM;
	bool m_hasFunctionGen;
	bool m_hasFastSampleRate;	//-M models
	int m_memoryDepthOption;	//0 = base, after that number is max sample count in millions
	bool m_hasI2cTrigger;
	bool m_hasSpiTrigger;
	bool m_hasUartTrigger;
	bool m_has8b10bTrigger;
	bool m_hasNrzTrigger;

	///Maximum bandwidth we support, in MHz
	unsigned int m_maxBandwidth;

	bool m_triggerArmed;
	bool m_triggerOneShot;

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
	bool m_interleaving;
	bool m_interleavingValid;
	Multimeter::MeasurementTypes m_meterMode;
	bool m_meterModeValid;
	std::map<size_t, bool> m_probeIsActive;

	//True if we have >8 bit capture depth
	bool m_highDefinition;

	//External trigger input
	OscilloscopeChannel* m_extTrigChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;

	//Mutexing for thread safety
	std::recursive_mutex m_cacheMutex;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(LeCroyOscilloscope)
};
#endif
