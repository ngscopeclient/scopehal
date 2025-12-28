/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of ThunderScopeOscilloscope
	@ingroup scopedrivers
 */

#ifndef ThunderScopeOscilloscope_h
#define ThunderScopeOscilloscope_h

#include "RemoteBridgeOscilloscope.h"
#include "../xptools/HzClock.h"

/**
	@brief Driver for talking to the TS.NET server controlling a ThunderScope

	@ingroup scopedrivers
 */
class ThunderScopeOscilloscope : public RemoteBridgeOscilloscope
{
public:
	ThunderScopeOscilloscope(SCPITransport* transport);
	virtual ~ThunderScopeOscilloscope();

	//not copyable or assignable
	ThunderScopeOscilloscope(const ThunderScopeOscilloscope& rhs) =delete;
	ThunderScopeOscilloscope& operator=(const ThunderScopeOscilloscope& rhs) =delete;

public:

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual void FlushConfigCache() override;

	//Channel configuration
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual std::vector<unsigned int> GetChannelBandwidthLimiters(size_t i) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual bool CanEnableChannel(size_t i) override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) override;
	virtual void EnableChannel(size_t i) override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void PushEdgeTrigger(EdgeTrigger* trig) override;

	// Captures
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void ForceTrigger() override;

	//Timebase
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual std::set<InterleaveConflict> GetInterleaveConflicts() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;
	virtual bool CanInterleave() override;
	virtual bool HasInterleavingControls() override;
	void SetSampleDepth(uint64_t depth) override;
	void SetSampleRate(uint64_t rate) override;

	//ADC modes
	virtual bool IsADCModeConfigurable() override;
	virtual std::vector<std::string> GetADCModeNames(size_t channel) override;
	virtual size_t GetADCMode(size_t channel) override;
	virtual void SetADCMode(size_t channel, size_t mode) override;

protected:
	void ResetPerCaptureDiagnostics();
	void RefreshSampleRate();

	std::string GetChannelColor(size_t i);

	///@brief Number of analog channels (always 4 at the moment)
	size_t m_analogChannelCount;

	///@brief Map of channel numbers to attenuation levels
	std::map<size_t, double> m_channelAttenuations;

	///@brief Number of WFM/s acquired by hardware
	FilterParameter m_diag_hardwareWFMHz;

	///@brief Number of WFM/s recieved by the driver
	FilterParameter m_diag_receivedWFMHz;

	///@brief Number of waveforms acquired during this session
	FilterParameter m_diag_totalWFMs;

	///@brief Number of waveforms dropped because some part of the pipeline couldn't keep up
	FilterParameter m_diag_droppedWFMs;

	///@brief Percentage of waveforms which were dropped
	FilterParameter m_diag_droppedPercent;

	///@brief Counter of average trigger rate
	HzClock m_receiveClock;

	///@brief Buffers for storing raw ADC samples before converting to fp32
	std::vector<std::unique_ptr<AcceleratorBuffer<int16_t> > > m_analogRawWaveformBuffers;

	///@brief Vulkan queue used for sample conversion
	std::shared_ptr<QueueHandle> m_queue;

	///@brief Command pool from which m_cmdBuf was allocated
	std::unique_ptr<vk::raii::CommandPool> m_pool;

	///@brief Command buffer for sample conversion
	std::unique_ptr<vk::raii::CommandBuffer> m_cmdBuf;

	///@brief Compute pipeline for converting raw ADC codes to float32 samples
	std::unique_ptr<ComputePipeline> m_conversion8BitPipeline;

	///@brief Compute pipeline for converting raw ADC codes to float32 samples
	std::unique_ptr<ComputePipeline> m_conversion16BitPipeline;

	///@brief Buffer for storing channel clip state
	AcceleratorBuffer<uint32_t> m_clippingBuffer;

	///@brief Bandwidth limiters
	std::vector<unsigned int> m_bandwidthLimits;

	///@brief ADC modes
	enum ADCMode
	{
		MODE_8BIT,
		MODE_12BIT
	} m_adcMode;

	///@brief True if we've already requested data for the current acquisition from the server
	bool m_dataRequested;

public:

	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(ThunderScopeOscilloscope);
};

#endif
