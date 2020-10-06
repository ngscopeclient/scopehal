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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of DemoOscilloscope
 */

#ifndef DemoOscilloscope_h
#define DemoOscilloscope_h

#include "../scopehal/AlignedAllocator.h"
#include <ffts.h>

class DemoOscilloscope : public SCPIOscilloscope
{
public:
	DemoOscilloscope(SCPITransport* transport);
	virtual ~DemoOscilloscope();

	virtual std::string IDPing();

	virtual std::string GetTransportConnectionString();
	virtual std::string GetTransportName();

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

	virtual unsigned int GetInstrumentTypes();
	virtual void LoadConfiguration(const YAML::Node& node, IDTable& idmap);

protected:

	OscilloscopeChannel* m_extTrigger;

	std::map<size_t, bool> m_channelsEnabled;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCoupling;
	std::map<size_t, double> m_channelAttenuation;
	std::map<size_t, unsigned int> m_channelBandwidth;
	std::map<size_t, double> m_channelVoltageRange;
	std::map<size_t, double> m_channelOffset;

	bool m_triggerArmed;
	bool m_triggerOneShot;

	//Helpers for waveform generation
	WaveformBase* GenerateNoisySinewave(
		float amplitude,
		float startphase,
		int64_t period,
		int64_t sampleperiod,
		size_t depth);

	WaveformBase* GenerateNoisySinewaveMix(
		float amplitude,
		float startphase1,
		float startphase2,
		float period1,
		float period2,
		int64_t sampleperiod,
		size_t depth);

	WaveformBase* GeneratePRBS31(
		float amplitude,
		float period,
		int64_t sampleperiod,
		size_t depth);

	WaveformBase* Generate8b10b(
		float amplitude,
		float period,
		int64_t sampleperiod,
		size_t depth);

	void DegradeSerialData(AnalogWaveform* cap, int64_t sampleperiod, size_t depth);

	//FFT stuff
	AlignedAllocator<float, 32> m_allocator;
	ffts_plan_t* m_forwardPlan;
	ffts_plan_t* m_reversePlan;
	size_t m_cachedNumPoints;
	size_t m_cachedRawSize;

	float* m_forwardInBuf;
	float* m_forwardOutBuf;
	float* m_reverseOutBuf;

	float m_sweepFreq;

	size_t m_depth;
	size_t m_rate;

public:
	static std::string GetDriverNameInternal();

	OSCILLOSCOPE_INITPROC(DemoOscilloscope)
};

#endif

