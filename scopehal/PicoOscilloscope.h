/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef PicoOscilloscope_h
#define PicoOscilloscope_h

class EdgeTrigger;

#include "RemoteBridgeOscilloscope.h"

/**
	@brief PicoOscilloscope - driver for talking to the scopehal-pico-bridge daemons
 */
class PicoOscilloscope 	: public virtual RemoteBridgeOscilloscope
						, public virtual SCPIFunctionGenerator
{
public:
	PicoOscilloscope(SCPITransport* transport);
	virtual ~PicoOscilloscope();

	//not copyable or assignable
	PicoOscilloscope(const PicoOscilloscope& rhs) =delete;
	PicoOscilloscope& operator=(const PicoOscilloscope& rhs) =delete;

public:

	//Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void FlushConfigCache() override;

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual bool CanEnableChannel(size_t i) override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;

	//Timebase
	virtual bool CanInterleave() override;
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

	//ADC configuration
	virtual std::vector<AnalogBank> GetAnalogBanks() override;
	virtual AnalogBank GetAnalogBank(size_t channel) override;
	virtual bool IsADCModeConfigurable() override;
	virtual std::vector<std::string> GetADCModeNames(size_t channel) override;
	virtual size_t GetADCMode(size_t channel) override;
	virtual void SetADCMode(size_t channel, size_t mode) override;

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
	// Logic analyzer configuration

	virtual std::vector<DigitalBank> GetDigitalBanks() override;
	virtual DigitalBank GetDigitalBank(size_t channel) override;
	virtual bool IsDigitalHysteresisConfigurable() override;
	virtual bool IsDigitalThresholdConfigurable() override;
	virtual float GetDigitalHysteresis(size_t channel) override;
	virtual float GetDigitalThreshold(size_t channel) override;
	virtual void SetDigitalHysteresis(size_t channel, float level) override;
	virtual void SetDigitalThreshold(size_t channel, float level) override;

	enum Series
	{
		SERIES_3x0xD,   //3000 series (first x=2 or 4 Chan, 2nd x is BW)
		SERIES_3x0xDMSO,//3000 series+16bits MSO(first x=2 or 4 Chan, 2nd x is BW)
		SERIES_6403E,	//Lowest end 6000E model has less ADCs
		SERIES_6x0xE,	//6000 series with 8 bit resolution only
		SERIES_6x2xE,	//6000 series with FlexRes

		SERIES_UNKNOWN	//unknown or invalid model name
	};

	enum ADCMode
	{
		ADC_MODE_8BIT	= 0,
		ADC_MODE_10BIT	= 1,
		ADC_MODE_12BIT	= 2
	};

	bool IsDigitalPodPresent(size_t npod);
	bool IsDigitalPodActive(size_t npod);
	bool IsChannelIndexDigital(size_t i);
	size_t GetDigitalPodIndex(size_t i)
	{ return (i - m_digitalChannelBase) / 8; }
	size_t GetDigitalLaneIndex(size_t i)
	{ return (i - m_digitalChannelBase) % 8; }

protected:
	void IdentifyHardware();

	//Helpers for determining legal configurations
	bool Is10BitModeAvailable();
	bool Is12BitModeAvailable();
	size_t GetEnabledAnalogChannelCount();
	size_t GetEnabledDigitalPodCount();

	size_t GetEnabledAnalogChannelCountRange(size_t start, size_t end);

	size_t GetEnabledAnalogChannelCountAToD()
	{ return GetEnabledAnalogChannelCountRange(0, 3); }
	size_t GetEnabledAnalogChannelCountEToH()
	{ return GetEnabledAnalogChannelCountRange(4, 7); }
	size_t GetEnabledAnalogChannelCountAToB()
	{ return GetEnabledAnalogChannelCountRange(0, 1); }
	size_t GetEnabledAnalogChannelCountCToD()
	{ return GetEnabledAnalogChannelCountRange(2, 3); }
	size_t GetEnabledAnalogChannelCountEToF()
	{ return GetEnabledAnalogChannelCountRange(4, 5); }
	size_t GetEnabledAnalogChannelCountGToH()
	{ return GetEnabledAnalogChannelCountRange(6, 7); }

	bool CanEnableChannel6000Series8Bit(size_t i);
	bool CanEnableChannel6000Series10Bit(size_t i);
	bool CanEnableChannel6000Series12Bit(size_t i);

	std::string GetChannelColor(size_t i);

	//hardware analog channel count, independent of LA option etc
	size_t m_analogChannelCount;
	size_t m_digitalChannelBase;
	size_t m_digitalChannelCount;

	OscilloscopeChannel* m_extTrigChannel;
	FunctionGeneratorChannel* m_awgChannel;

	//Most Pico API calls are write only, so we have to maintain all state clientside.
	//This isn't strictly a cache anymore since it's never flushed!
	std::map<size_t, double> m_channelAttenuations;
	ADCMode m_adcMode;
	std::map<int, bool> m_digitalBankPresent;
	std::map<int, float> m_digitalThresholds;
	std::map<int, float> m_digitalHysteresis;

	//Function generator state
	bool m_awgEnabled;
	float m_awgDutyCycle;
	float m_awgRange;
	float m_awgOffset;
	float m_awgFrequency;
	FunctionGenerator::WaveShape m_awgShape;
	FunctionGenerator::OutputImpedance m_awgImpedance;

	Series m_series;

	///@brief Buffers for storing raw ADC samples before converting to fp32
	std::vector<std::unique_ptr<AcceleratorBuffer<int16_t> > > m_analogRawWaveformBuffers;

	//Vulkan waveform conversion
	std::shared_ptr<QueueHandle> m_queue;
	std::unique_ptr<vk::raii::CommandPool> m_pool;
	std::unique_ptr<vk::raii::CommandBuffer> m_cmdBuf;
	std::unique_ptr<ComputePipeline> m_conversionPipeline;

public:

	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(PicoOscilloscope)
};

#endif
