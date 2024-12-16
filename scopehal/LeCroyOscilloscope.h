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
	@brief Declaration of LeCroyOscilloscope
	@ingroup scopedrivers
 */

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

	@ingroup scopedrivers
 */
class LeCroyOscilloscope
	: public virtual SCPIOscilloscope
	, public virtual SCPIMultimeter
	, public virtual SCPIFunctionGenerator
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
	virtual std::string GetName() const override;
	virtual std::string GetVendor() const override;
	virtual std::string GetSerial() const override;
	virtual unsigned int GetInstrumentTypes() const override;
	virtual unsigned int GetMeasurementTypes() override;
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
	virtual bool CanAutoZero(size_t i) override;
	virtual void AutoZero(size_t i) override;
	virtual std::string GetProbeName(size_t i) override;
	virtual bool HasInputMux(size_t i) override;
	virtual size_t GetInputMuxSetting(size_t i) override;
	virtual std::vector<std::string> GetInputMuxNames(size_t i) override;
	virtual void SetInputMux(size_t i, size_t select) override;
	virtual bool CanAverage(size_t i) override;
	virtual size_t GetNumAverages(size_t i) override;
	virtual void SetNumAverages(size_t i, size_t navg) override;

	OscilloscopeChannel* GetACLineTrigger()
	{ return m_acLineChannel; }

	OscilloscopeChannel* GetFastEdgeTrigger()
	{ return m_fastEdgeChannel; }

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool PeekTriggerArmed() override;
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
	bool IsCDRLocked();

	//DMM configuration
	virtual int GetMeterChannelCount();
	virtual int GetCurrentMeterChannel() override;
	virtual void SetCurrentMeterChannel(int chan) override;
	virtual void StartMeter() override;
	virtual void StopMeter() override;
	virtual void SetMeterAutoRange(bool enable) override;
	virtual bool GetMeterAutoRange() override;
	virtual double GetMeterValue() override;
	virtual Multimeter::MeasurementTypes GetMeterMode() override;
	virtual void SetMeterMode(Multimeter::MeasurementTypes type) override;
	virtual int GetMeterDigits() override;

	//Function generator
	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) override;
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
	virtual FunctionGenerator::WaveShape GetFunctionChannelShape(int chan) override;
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) override;
	virtual float GetFunctionChannelRiseTime(int chan) override;
	virtual void SetFunctionChannelRiseTime(int chan, float fs) override;
	virtual float GetFunctionChannelFallTime(int chan) override;
	virtual void SetFunctionChannelFallTime(int chan, float fs) override;
	virtual bool HasFunctionRiseFallTimeControls(int chan) override;
	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan) override;
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z) override;

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
	virtual bool IsSamplingModeAvailable(SamplingMode mode) override;
	virtual SamplingMode GetSamplingMode() override;
	virtual void SetSamplingMode(SamplingMode mode) override;

	//DBI mode
	bool HasDBICapability();
	bool IsDBIEnabled(size_t channel);

	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;
	virtual void SetDeskewForChannel(size_t channel, int64_t skew) override;
	virtual int64_t GetDeskewForChannel(size_t channel) override;

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

	//All currently supported LeCroy scopes have only one analog bank (same ADC config for all channels)
	//so no need to override those

	virtual bool IsADCModeConfigurable() override;
	virtual std::vector<std::string> GetADCModeNames(size_t channel) override;
	virtual size_t GetADCMode(size_t channel) override;
	virtual void SetADCMode(size_t channel, size_t mode) override;

	//public so it can be called by TRCImportFilter
	static time_t ExtractTimestamp(unsigned char* wavedesc, double& basetime);

protected:

	//Trigger config
	void Pull8b10bTrigger();
	void Pull64b66bTrigger();
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
	std::vector<WaveformBase*> ProcessAnalogWaveform(
		const char* data,
		size_t datalen,
		std::string& wavedesc,
		uint32_t num_sequences,
		time_t ttime,
		double basetime,
		double* wavetime
		);
	std::map<int, SparseDigitalWaveform*> ProcessDigitalWaveform(std::string& data, int64_t analog_hoff);

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
	bool m_hasXdev;

	///Maximum bandwidth we support, in MHz
	unsigned int m_maxBandwidth;

	///@brief True if we have sent an arm command to the scope (may not have executed  yet)
	bool m_triggerArmed;

	///@brief True if the scope has reported it is in fact in the arm state
	bool m_triggerReallyArmed;

	///@brief True if current trigger is a single-shot and should not re-arm
	bool m_triggerOneShot;

	//Cached configuration
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, float> m_channelOffsets;
	std::map<size_t, float> m_channelDigitalThresholds;
	std::map<size_t, size_t> m_channelNavg;
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
	bool m_dmmAutorangeValid;
	bool m_dmmAutorange;
	std::map<size_t, bool> m_probeIsActive;

	//True if we have >8 bit capture depth
	bool m_highDefinition;

	///@brief External trigger input
	OscilloscopeChannel* m_extTrigChannel;

	///@brief Internal "AC line" trigger source
	OscilloscopeChannel* m_acLineChannel;

	///@brief Internal "fast edge" trigger source
	OscilloscopeChannel* m_fastEdgeChannel;

	FunctionGeneratorChannel* m_awgChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(LeCroyOscilloscope)
};
#endif
