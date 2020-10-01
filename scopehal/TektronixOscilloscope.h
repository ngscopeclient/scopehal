/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
class TektronixOscilloscope : public SCPIOscilloscope
{
public:
	TektronixOscilloscope(SCPITransport* transport);
	virtual ~TektronixOscilloscope();

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
	virtual double GetChannelAttenuation(size_t i);
	virtual void SetChannelAttenuation(size_t i, double atten);
	virtual int GetChannelBandwidthLimit(size_t i);
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz);
	virtual double GetChannelVoltageRange(size_t i);
	virtual void SetChannelVoltageRange(size_t i, double range);
	virtual OscilloscopeChannel* GetExternalTrigger();
	virtual double GetChannelOffset(size_t i);
	virtual void SetChannelOffset(size_t i, double offset);

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool AcquireData();
	virtual void Start();
	virtual void StartSingleTrigger();
	virtual void Stop();
	virtual bool IsTriggerArmed();
	virtual void PushTrigger();
	virtual void PullTrigger();

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
		PROBE_TYPE_DIGITAL_8BIT
	};

	//config cache
	std::map<size_t, double> m_channelOffsets;
	std::map<size_t, double> m_channelVoltageRanges;
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

	//available instrument bandwidth in MHz
	int m_bandwidth;

	enum Family
	{
		FAMILY_MSO5,
		FAMILY_MSO6,
		FAMILY_UNKNOWN
	} m_family;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC_H(TektronixOscilloscope)
};

#endif
