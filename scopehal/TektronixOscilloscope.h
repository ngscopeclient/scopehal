/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	: public SCPIOscilloscope
	, public FunctionGenerator
	, public Multimeter
{
public:
	TektronixOscilloscope(SCPITransport* transport);
	virtual ~TektronixOscilloscope();

	//not copyable or assignable
	TektronixOscilloscope(const TektronixOscilloscope& rhs) =delete;
	TektronixOscilloscope& operator=(const TektronixOscilloscope& rhs) =delete;

public:

	//Device information
	virtual unsigned int GetInstrumentTypes();

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
	std::vector<std::string> GetTriggerTypes();

	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved();
	virtual std::vector<uint64_t> GetSampleRatesInterleaved();
	virtual std::set<InterleaveConflict> GetInterleaveConflicts();
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved();
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved();
	virtual uint64_t GetSampleRate();
	virtual uint64_t GetSampleDepth();
	virtual void SetSampleDepth(uint64_t depth);
	virtual void SetSampleRate(uint64_t rate);
	virtual void SetTriggerOffset(int64_t offset);
	virtual int64_t GetTriggerOffset();
	virtual bool IsInterleaving();
	virtual bool SetInterleaving(bool combine);

	virtual void SetDeskewForChannel(size_t channel, int64_t skew);
	virtual int64_t GetDeskewForChannel(size_t channel);

	virtual void SetUseExternalRefclk(bool external);
	virtual void EnableTriggerOutput();

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
	// Multimeter stuff

	virtual unsigned int GetMeasurementTypes();
	virtual int GetMeterChannelCount();
	virtual std::string GetMeterChannelName(int chan);
	virtual int GetCurrentMeterChannel();
	virtual void SetCurrentMeterChannel(int chan);
	virtual MeasurementTypes GetMeterMode();
	virtual void SetMeterMode(MeasurementTypes type);
	virtual void SetMeterAutoRange(bool enable);
	virtual bool GetMeterAutoRange();
	virtual void StartMeter();
	virtual void StopMeter();
	virtual double GetMeterValue();
	virtual int GetMeterDigits();

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
	// Spectrum analyzer configuration

	virtual bool HasFrequencyControls();
	virtual void SetSpan(int64_t span);
	virtual int64_t GetSpan();
	virtual void SetCenterFrequency(size_t channel, int64_t freq);
	virtual int64_t GetCenterFrequency(size_t channel);
	virtual void SetResolutionBandwidth(int64_t rbw);
	virtual int64_t GetResolutionBandwidth();

protected:
	OscilloscopeChannel* m_extTrigChannel;

	//acquisition
	bool AcquireDataMSO56(std::map<int, std::vector<WaveformBase*> >& pending_waveforms);
	void DetectProbes();

	//Mutexing for thread safety
	std::recursive_mutex m_cacheMutex;

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;

	enum ProbeType
	{
		PROBE_TYPE_ANALOG,
		PROBE_TYPE_ANALOG_250K,
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
	bool m_rbwValid;
	int64_t m_rbw;
	bool m_dmmAutorangeValid;
	bool m_dmmAutorange;
	bool m_dmmChannelValid;
	int m_dmmChannel;
	bool m_dmmModeValid;
	Multimeter::MeasurementTypes m_dmmMode;

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
	OSCILLOSCOPE_INITPROC_H(TektronixOscilloscope)
};

#endif
