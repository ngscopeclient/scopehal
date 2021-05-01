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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of DemoOscilloscope
 */

#ifndef DemoOscilloscope_h
#define DemoOscilloscope_h

#include "TestWaveformSource.h"
#include <random>

class DemoOscilloscope : public SCPIOscilloscope
{
public:
	DemoOscilloscope(SCPITransport* transport);
	virtual ~DemoOscilloscope();

	//not copyable or assignable
	DemoOscilloscope(const DemoOscilloscope& rhs) =delete;
	DemoOscilloscope& operator=(const DemoOscilloscope& rhs) =delete;

	virtual std::string IDPing();

	virtual std::string GetTransportConnectionString();
	virtual std::string GetTransportName();

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

	virtual bool IsADCModeConfigurable();
	virtual std::vector<std::string> GetADCModeNames(size_t channel);
	virtual size_t GetADCMode(size_t channel);
	virtual void SetADCMode(size_t channel, size_t mode);
	virtual std::vector<AnalogBank> GetAnalogBanks();
	virtual AnalogBank GetAnalogBank(size_t channel);

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
	std::map<size_t, size_t> m_channelModes;

	enum ChannelModes
	{
		CHANNEL_MODE_IDEAL,
		CHANNEL_MODE_NOISE,
		CHANNEL_MODE_NOISE_LPF
	};

	bool m_triggerArmed;
	bool m_triggerOneShot;

	float m_sweepFreq;

	size_t m_depth;
	size_t m_rate;

	std::random_device m_rd;
	std::mt19937 m_rng;

	TestWaveformSource m_source;

public:
	static std::string GetDriverNameInternal();

	OSCILLOSCOPE_INITPROC(DemoOscilloscope)
};

#endif

