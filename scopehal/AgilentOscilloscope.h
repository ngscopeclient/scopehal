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

#ifndef AgilentOscilloscope_h
#define AgilentOscilloscope_h

#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "NthEdgeBurstTrigger.h"

class AgilentOscilloscope : public virtual SCPIOscilloscope
{
public:
	AgilentOscilloscope(SCPITransport* transport);
	virtual ~AgilentOscilloscope();

	//not copyable or assignable
	AgilentOscilloscope(const AgilentOscilloscope& rhs) =delete;
	AgilentOscilloscope& operator=(const AgilentOscilloscope& rhs) =delete;

public:

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void FlushConfigCache() override;

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
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
	virtual std::vector<std::string> GetTriggerTypes() override;

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

protected:
	OscilloscopeChannel* m_extTrigChannel;

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelCount;
	unsigned int m_digitalChannelBase;

	enum ProbeType {
		None,
		AutoProbe,
		SmartProbe,
	};

	//config cache
	std::map<size_t, float> m_channelOffsets;
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCouplings;
	std::map<size_t, double> m_channelAttenuations;
	std::map<size_t, int> m_channelBandwidthLimits;
	std::map<int, bool> m_channelsEnabled;
	std::map<size_t, ProbeType> m_probeTypes;

	bool m_sampleDepthValid;
	uint64_t m_sampleDepth;
	bool m_sampleRateValid;
	uint64_t m_sampleRate;

	bool m_triggerArmed;
	bool m_triggerOneShot;

	void PullEdgeTrigger();
	void PullNthEdgeBurstTrigger();
	void PullPulseWidthTrigger();

	void GetTriggerSlope(EdgeTrigger* trig, std::string reply);
	void GetTriggerSlope(NthEdgeBurstTrigger* trig, std::string reply);
	Trigger::Condition GetCondition(std::string reply);
	void GetProbeType(size_t i);

	void PushEdgeTrigger(EdgeTrigger* trig);
	void PushNthEdgeBurstTrigger(NthEdgeBurstTrigger* trig);
	void PushPulseWidthTrigger(PulseWidthTrigger* trig);
	void PushCondition(std::string path, Trigger::Condition cond);
	void PushFloat(std::string path, float f);
	void PushSlope(std::string path, EdgeTrigger::EdgeType slope);
	void PushSlope(std::string path, NthEdgeBurstTrigger::EdgeType slope);

private:
	static std::map<uint64_t, uint64_t> m_sampleRateToDuration;

	struct WaveformPreamble {
		unsigned int format;
		unsigned int type;
		size_t length;
		unsigned int average_count;
		double xincrement;
		double xorigin;
		double xreference;
		double yincrement;
		double yorigin;
		double yreference;
	};

	void ConfigureWaveform(std::string channel);
	bool IsAnalogChannel(size_t i);
	size_t GetDigitalPodIndex(size_t i);
	std::string GetDigitalPodName(size_t i);
	std::vector<uint8_t> GetWaveformData(std::string channel);
	WaveformPreamble GetWaveformPreamble(std::string channel);
	void ProcessDigitalWaveforms(
		std::map<int, std::vector<WaveformBase*>> &pending_waveforms,
		std::vector<uint8_t> &data, WaveformPreamble &preamble,
		size_t chan_start);
	void SetSampleRateAndDepth(uint64_t rate, uint64_t depth);


public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(AgilentOscilloscope)
};

#endif
