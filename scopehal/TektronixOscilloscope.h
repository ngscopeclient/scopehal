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

#ifndef TektronixOscilloscope_h
#define TektronixOscilloscope_h

class EdgeTrigger;
class PulseWidthTrigger;
class DropoutTrigger;
class RuntTrigger;
class SlewRateTrigger;
class WindowTrigger;

/**
	@brief Driver for Tektronix oscilloscopes

	Tek scopes appear to adhere strictly to the LXI-style request-response model.
	Sending a new command while another is currently executing will result in one or both commands aborting.
	Unfortunately, this poses significant problems getting good performance over a high-latency WAN.

	Additionally, at least the 5/6 series appear to maintain state in the SCPI parser across connections.
	If a command is sent and the connection is immediately dropped, reconnecting may result in seeing the reply!!

	To read the error log (helpful for driver development):
	ALLEV?
		Should print one of the following messages:
		* 0,"No events to report - queue empty"
		* 1,"No events to report - new events pending *ESR?"
	*ESR?
		Prints a status register, not quite sure what this does
	ALLEV?
		Prints the error log in a somewhat confusing and not-human-readable format
 */
class TektronixOscilloscope
	: public virtual SCPIOscilloscope
	, public virtual SCPIFunctionGenerator
	, public virtual SCPIMultimeter
{
public:
	TektronixOscilloscope(SCPITransport* transport);
	virtual ~TektronixOscilloscope();

	//not copyable or assignable
	TektronixOscilloscope(const TektronixOscilloscope& rhs) =delete;
	TektronixOscilloscope& operator=(const TektronixOscilloscope& rhs) =delete;

public:

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;

	virtual void FlushConfigCache() override;

	//Channel configuration
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;
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
	Unit GetYAxisUnit(size_t i);
	virtual bool CanDegauss(size_t i) override;
	virtual bool ShouldDegauss(size_t i) override;
	virtual void Degauss(size_t i) override;
	virtual std::string GetProbeName(size_t i) override;

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
	std::vector<std::string> GetTriggerTypes() override;

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

	virtual void SetDeskewForChannel(size_t channel, int64_t skew) override;
	virtual int64_t GetDeskewForChannel(size_t channel) override;

	virtual void SetUseExternalRefclk(bool external) override;
	virtual void EnableTriggerOutput() override;

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
	// Multimeter stuff

	virtual unsigned int GetMeasurementTypes() override;
	virtual int GetMeterChannelCount();
	virtual int GetCurrentMeterChannel() override;
	virtual void SetCurrentMeterChannel(int chan) override;
	virtual MeasurementTypes GetMeterMode() override;
	virtual void SetMeterMode(MeasurementTypes type) override;
	virtual void SetMeterAutoRange(bool enable) override;
	virtual bool GetMeterAutoRange() override;
	virtual void StartMeter() override;
	virtual void StopMeter() override;
	virtual double GetMeterValue() override;
	virtual int GetMeterDigits() override;

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
	// Spectrum analyzer configuration

	virtual bool HasFrequencyControls() override;
	virtual void SetSpan(int64_t span) override;
	virtual int64_t GetSpan() override;
	virtual void SetCenterFrequency(size_t channel, int64_t freq) override;
	virtual int64_t GetCenterFrequency(size_t channel) override;
	virtual void SetResolutionBandwidth(int64_t rbw) override;
	virtual int64_t GetResolutionBandwidth() override;

protected:
	OscilloscopeChannel* m_extTrigChannel;
	FunctionGeneratorChannel* m_awgChannel;

	struct mso56_preamble
	{
		int byte_num;
		int bit_num;
		char encoding[32];
		char bin_format[32];
		char asc_format[32];
		char byte_order[32];
		char wfid[256];
		int nr_pt;
		char pt_fmt[32];
		char pt_order[32];
		char xunit[32];
		union
		{
			double xincrement;
			double hzbase;
		};
		union
		{
			double xzero;
			double hzoff;
		};
		int pt_off;
		char yunit[32];
		double ymult;
		double yoff;
		double yzero;
		char domain[32];
		char wfmtype[32];
		double centerfreq;
		double span;
	};

	//acquisition
	void ResynchronizeSCPI();
	bool ReadPreamble(std::string& preamble_in, mso56_preamble& preamble_out);
	bool AcquireDataMSO56(std::map<int, std::vector<WaveformBase*> >& pending_waveforms);
	void DetectProbes();

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;

	enum ProbeType
	{
		PROBE_TYPE_ANALOG,
		PROBE_TYPE_ANALOG_250K,
		PROBE_TYPE_ANALOG_CURRENT,
		PROBE_TYPE_DIGITAL_8BIT
	};

	//config cache
	std::map<size_t, float> m_channelOffsets;
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCouplings;
	std::map<size_t, double> m_channelAttenuations;
	std::map<size_t, int> m_channelBandwidthLimits;
	std::map<int, bool> m_channelsEnabled;
	bool m_triggerChannelValid;
	size_t m_triggerChannel;
	bool m_sampleRateValid;
	uint64_t m_sampleRate;
	bool m_sampleDepthValid;
	uint64_t m_sampleDepth;
	bool m_triggerOffsetValid;
	int64_t m_triggerOffset;
	std::map<size_t, int64_t> m_channelDeskew;
	std::map<size_t, ProbeType> m_probeTypes;
	std::map<size_t, std::string> m_probeNames;
	bool m_rbwValid;
	int64_t m_rbw;
	bool m_dmmAutorangeValid;
	bool m_dmmAutorange;
	bool m_dmmChannelValid;
	int m_dmmChannel;
	bool m_dmmModeValid;
	Multimeter::MeasurementTypes m_dmmMode;
	std::map<size_t, Unit> m_channelUnits;

	///The analog channel for each flex channel
	std::map<OscilloscopeChannel*, size_t> m_flexChannelParents;

	//The lane number for each flex channel
	std::map<OscilloscopeChannel*, size_t> m_flexChannelLanes;

	size_t m_digitalChannelBase;
	size_t m_spectrumChannelBase;

	bool m_triggerArmed;
	bool m_triggerOneShot;

	void PullEdgeTrigger();
	void PushEdgeTrigger(EdgeTrigger* trig);
	void PullPulseWidthTrigger();
	void PushPulseWidthTrigger(PulseWidthTrigger* trig);
	void PullDropoutTrigger();
	void PushDropoutTrigger(DropoutTrigger* trig);
	void PullRuntTrigger();
	void PushRuntTrigger(RuntTrigger* trig);
	void PullSlewRateTrigger();
	void PushSlewRateTrigger(SlewRateTrigger* trig);
	void PullWindowTrigger();
	void PushWindowTrigger(WindowTrigger* trig);

	float ReadTriggerLevelMSO56(OscilloscopeChannel* chan);
	void SetTriggerLevelMSO56(Trigger* trig);

	//Helpers for figuring out type of a channel by the index
	bool IsAnalog(size_t index)
	{ return index < m_analogChannelCount; }

	bool IsDigital(size_t index)
	{
		if(index < m_digitalChannelBase)
			return false;
		if(index >= (m_digitalChannelBase + 8*m_analogChannelCount) )
			return false;
		return true;
	}

	bool IsSpectrum(size_t index)
	{
		if(index < m_spectrumChannelBase)
			return false;
		if(index >= (m_spectrumChannelBase + m_analogChannelCount) )
			return false;
		return true;
	}

	///Maximum bandwidth we support, in MHz
	unsigned int m_maxBandwidth;

	enum Family
	{
		FAMILY_MSO5,
		FAMILY_MSO6,
		FAMILY_UNKNOWN
	} m_family;

	//Installed software options
	bool m_hasDVM;

	/**
		@brief True if this channel's status has changed (on/off) since the last time the trigger was armed

		This is needed to work around a bug in the MSO64 SCPI stack.

		Per 5/6 series programmer manual for DAT:SOU:AVAIL?:

			"This query returns a list of enumerations representing the source waveforms that currently available for
			:CURVe? queries. This means that the waveforms have been acquired. If there are none, NONE is returned."

		This is untrue. In reality it returns whether the channel is *currently* enabled. If a channel is enabled after
		the trigger event, DAT:SOU:AVAIL? will report the channel as available, however CURV? queries will silently
		fail and return no data.
	*/
	std::set<size_t> m_channelEnableStatusDirty;
	bool IsEnableStateDirty(size_t chan);
	void FlushChannelEnableStates();

	//Function generator state
	bool m_hasAFG;
	bool m_afgEnabled;
	float m_afgAmplitude;
	float m_afgOffset;
	float m_afgFrequency;
	float m_afgDutyCycle;
	FunctionGenerator::WaveShape m_afgShape;
	FunctionGenerator::OutputImpedance m_afgImpedance;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(TektronixOscilloscope)
};

#endif
