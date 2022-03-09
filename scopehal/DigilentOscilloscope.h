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

#ifndef DigilentOscilloscope_h
#define DigilentOscilloscope_h

class EdgeTrigger;

/**
	@brief DigilentOscilloscope - driver for talking to the scopehal-waveforms-bridge daemon (wfmserver)
 */
class DigilentOscilloscope : public SCPIOscilloscope
{
public:
	DigilentOscilloscope(SCPITransport* transport);
	virtual ~DigilentOscilloscope();

	//not copyable or assignable
	DigilentOscilloscope(const DigilentOscilloscope& rhs) =delete;
	DigilentOscilloscope& operator=(const DigilentOscilloscope& rhs) =delete;

public:

	//Device information
	virtual unsigned int GetInstrumentTypes();

	virtual void FlushConfigCache();

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i);
	virtual void EnableChannel(size_t i);
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
	virtual bool CanEnableChannel(size_t i);

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
	virtual void SetTriggerOffset(int64_t offset);
	virtual int64_t GetTriggerOffset();
	virtual bool IsInterleaving();
	virtual bool SetInterleaving(bool combine);

	//ADC configuration
	virtual std::vector<AnalogBank> GetAnalogBanks();
	virtual AnalogBank GetAnalogBank(size_t channel);
	virtual bool IsADCModeConfigurable();
	virtual std::vector<std::string> GetADCModeNames(size_t channel);
	virtual size_t GetADCMode(size_t channel);
	virtual void SetADCMode(size_t channel, size_t mode);

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

	enum Series
	{
		SERIES_ANALOG_DISCOVERY,
		SERIES_ANALOG_DISCOVERY_2,
		SERIES_DIGITAL_DISCOVERY,
		SERIES_ANALOG_DISCOVERY_PRO,

		SERIES_UNKNOWN
	};

protected:
	void IdentifyHardware();

	std::string GetChannelColor(size_t i);

	//hardware analog channel count, independent of LA option etc
	size_t m_analogChannelCount;
	size_t m_digitalChannelBase;
	size_t m_digitalChannelCount;

	//OscilloscopeChannel* m_extTrigChannel;

	//Mutexing for thread safety
	std::recursive_mutex m_cacheMutex;

	//Most SCPI API calls are write only, so we have to maintain all state clientside.
	//This isn't strictly a cache anymore since it's never flushed!
	std::map<int, bool> m_channelsEnabled;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCouplings;
	std::map<size_t, float> m_channelOffsets;
	std::map<size_t, float> m_channelVoltageRanges;
	bool m_triggerArmed;
	bool m_triggerOneShot;
	uint64_t m_srate;
	uint64_t m_mdepth;
	int64_t m_triggerOffset;
	std::map<size_t, double> m_channelAttenuations;

	void PushEdgeTrigger(EdgeTrigger* trig);

	Socket* m_dataSocket;

	Series m_series;

public:

	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(DigilentOscilloscope)
};

#endif
