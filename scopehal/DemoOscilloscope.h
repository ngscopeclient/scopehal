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
	@brief Declaration of DemoOscilloscope
	@ingroup scopedrivers
 */

#ifndef DemoOscilloscope_h
#define DemoOscilloscope_h

#include "TestWaveformSource.h"
#include <random>

/**
	@brief Simulated oscilloscope for demonstrations and testing
	@ingroup scopedrivers

	Four channels:
	* 1 GHz sine
	* 1 GHz sine plus 1.1 - 1.5 GHz ramp
	* 10.3125 Gbps PRBS-31
	* 1.25 Gbps 8B/10B K28.5 D16.2 idle sequence
 */
class DemoOscilloscope : public virtual SCPIOscilloscope
{
public:
	DemoOscilloscope(SCPITransport* transport);
	virtual ~DemoOscilloscope();

	//not copyable or assignable
	DemoOscilloscope(const DemoOscilloscope& rhs) =delete;
	DemoOscilloscope& operator=(const DemoOscilloscope& rhs) =delete;

	virtual std::string IDPing() override;

	virtual std::string GetTransportConnectionString() override;
	virtual std::string GetTransportName() override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

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
	virtual bool CanInterleave() override;

	virtual bool IsADCModeConfigurable() override;
	virtual std::vector<std::string> GetADCModeNames(size_t channel) override;
	virtual size_t GetADCMode(size_t channel) override;
	virtual void SetADCMode(size_t channel, size_t mode) override;
	virtual std::vector<AnalogBank> GetAnalogBanks() override;
	virtual AnalogBank GetAnalogBank(size_t channel) override;

	virtual unsigned int GetInstrumentTypes() const override;
	virtual void LoadConfiguration(int version, const YAML::Node& node, IDTable& idmap) override;

protected:

	///@brief External trigger
	OscilloscopeChannel* m_extTrigger;

	///@brief Map of channel ID to on/off state
	std::map<size_t, bool> m_channelsEnabled;

	///@brief Map of channel ID to coupling
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCoupling;

	///@brief Map of channel ID to probe attenuation
	std::map<size_t, double> m_channelAttenuation;

	///@brief Map of channel ID to bandwidth limit
	std::map<size_t, unsigned int> m_channelBandwidth;

	///@brief Map of channel ID to vertical scale range
	std::map<size_t, float> m_channelVoltageRange;

	///@brief Map of channel ID to offset
	std::map<size_t, float> m_channelOffset;

	///@brief Map of channel ID to ADC mode
	std::map<size_t, size_t> m_channelModes;

	///@brief ADC mode selectors (used to select the simulated channel)
	enum ChannelModes
	{
		///@brief Ideal waveform with no impairments
		CHANNEL_MODE_IDEAL,

		///@brief 5 mV RMS AWGN
		CHANNEL_MODE_NOISE,

		///@brief 5 mV RMS AWGN + 300mm lossy S-parameter channel
		CHANNEL_MODE_NOISE_LPF
	};

	///@brief True if trigger is armed
	bool m_triggerArmed;

	///@brief True if most recent trigger arm was a single-shot trigger
	bool m_triggerOneShot;

	///@brief Current frequency within the sweep for channel 2
	float m_sweepFreq;

	///@brief Memory depth
	uint64_t m_depth;

	///@brief Sample rate
	uint64_t m_rate;

	///@brief Random number source for seeding the generators
	std::random_device m_rd;

	///@brief Random number generators for AWGN synthesis
	std::minstd_rand* m_rng[4];

	/**
		@brief Signal sources for each channel

		Must be separate to enable parallel waveform synthesis
	 */
	TestWaveformSource* m_source[4];

	///@brief Vulkan queue for ISI channel
	std::shared_ptr<QueueHandle> m_queue[4];

	///@brief Vulkan command pool for ISI channel
	std::unique_ptr<vk::raii::CommandPool> m_pool[4];

	///@brief Vulkan command buffer for ISI channel
	std::unique_ptr<vk::raii::CommandBuffer> m_cmdBuf[4];

public:
	static std::string GetDriverNameInternal();

	OSCILLOSCOPE_INITPROC(DemoOscilloscope)
};

#endif

