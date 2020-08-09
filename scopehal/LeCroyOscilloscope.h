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

#ifndef LeCroyOscilloscope_h
#define LeCroyOscilloscope_h

#include <mutex>
#include "../xptools/Socket.h"

/**
	@brief A LeCroy VICP oscilloscope

	Protocol layer is based on LeCroy's released VICPClient.h, but rewritten and modernized heavily
 */
class LeCroyOscilloscope
	: public SCPIOscilloscope
	, public Multimeter
	, public FunctionGenerator
{
public:
	LeCroyOscilloscope(SCPITransport* transport);
	virtual ~LeCroyOscilloscope();

protected:
	void IdentifyHardware();
	void SharedCtorInit();
	virtual void DetectAnalogChannels();
	void AddDigitalChannels(unsigned int count);
	void DetectOptions();

public:
	//Device information
	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();
	virtual unsigned int GetInstrumentTypes();
	virtual unsigned int GetMeasurementTypes();

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
	virtual void ResetTriggerConditions();
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool AcquireData(bool toQueue = false);
	virtual void Start();
	virtual void StartSingleTrigger();
	virtual void Stop();
	virtual bool IsTriggerArmed();
	virtual size_t GetTriggerChannelIndex();
	virtual void SetTriggerChannelIndex(size_t i);
	virtual float GetTriggerVoltage();
	virtual void SetTriggerVoltage(float v);
	virtual Oscilloscope::TriggerType GetTriggerType();
	virtual void SetTriggerType(Oscilloscope::TriggerType type);
	virtual void SetTriggerForChannel(OscilloscopeChannel* channel, std::vector<TriggerType> triggerbits);
	virtual void EnableTriggerOutput();

	//DMM acquisition
	virtual double GetVoltage();
	virtual double GetPeakToPeak();
	virtual double GetFrequency();
	virtual double GetCurrent();
	virtual double GetTemperature();

	//DMM configuration
	virtual int GetMeterChannelCount();
	virtual std::string GetMeterChannelName(int chan);
	virtual int GetCurrentMeterChannel();
	virtual void SetCurrentMeterChannel(int chan);
	virtual void StartMeter();
	virtual void StopMeter();
	virtual void SetMeterAutoRange(bool enable);
	virtual bool GetMeterAutoRange();

	virtual Multimeter::MeasurementTypes GetMeterMode();
	virtual void SetMeterMode(Multimeter::MeasurementTypes type);

	//Function generator
	virtual int GetFunctionChannelCount();
	virtual std::string GetFunctionChannelName(int chan);
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
	virtual FunctionGenerator::WaveShape GetFunctionChannelShape(int chan);
	virtual void SetFunctionChannelShape(int chan, WaveShape shape);
	virtual float GetFunctionChannelRiseTime(int chan);
	virtual void SetFunctionChannelRiseTime(int chan, float sec);
	virtual float GetFunctionChannelFallTime(int chan);
	virtual void SetFunctionChannelFallTime(int chan, float sec);

	//Scope models.
	//We only distinguish down to the series of scope, exact SKU is irrelevant.
	enum Model
	{
		MODEL_WAVESURFER_3K,
		MODEL_WAVERUNNER_8K,
		MODEL_HDO_9K,
		MODEL_DDA_5K,
		MODEL_SDA_3K,
		MODEL_SIGLENT_SDS2000X,

		MODEL_UNKNOWN
	};

	Model GetModelID()
	{ return m_modelid; }

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
	virtual void SetUseExternalRefclk(bool external);
	virtual bool IsInterleaving();
	virtual bool SetInterleaving(bool combine);

	virtual void SetTriggerOffset(int64_t offset);
	virtual int64_t GetTriggerOffset();
	virtual void SetDeskewForChannel(size_t channel, int64_t skew);
	virtual int64_t GetDeskewForChannel(size_t channel);

protected:
	void BulkCheckChannelEnableState();

	bool ReadWaveformBlock(std::string& data);
	bool ReadWavedescs(
		std::vector<std::string>& wavedescs,
		bool* enabled,
		unsigned int& firstEnabledChannel,
		bool& any_enabled);
	void RequestWaveforms(bool* enabled, uint32_t num_sequences, bool denabled);
	time_t ExtractTimestamp(unsigned char* wavedesc, double& basetime);
	std::vector<WaveformBase*> ProcessAnalogWaveform(
		const char* data,
		size_t datalen,
		std::string& wavedesc,
		uint32_t num_sequences,
		time_t ttime,
		double basetime,
		double* wavetime
		);
	std::map<int, DigitalWaveform*> ProcessDigitalWaveform(
		std::string& data,
		time_t ttime,
		double basetime);

	void Convert8BitSamples(
		int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase);
	void Convert8BitSamplesAVX2(
		int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase);

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelCount;

	Model m_modelid;

	//set of SW/HW options we have
	bool m_hasLA;
	bool m_hasDVM;
	bool m_hasFunctionGen;
	bool m_hasFastSampleRate;	//-M models

	bool m_triggerArmed;
	bool m_triggerOneShot;

	//Cached configuration
	bool m_triggerChannelValid;
	size_t m_triggerChannel;
	bool m_triggerLevelValid;
	float m_triggerLevel;
	bool m_triggerTypeValid;
	TriggerType m_triggerType;
	std::map<size_t, double> m_channelVoltageRanges;
	std::map<size_t, double> m_channelOffsets;
	std::map<int, bool> m_channelsEnabled;
	bool m_sampleRateValid;
	int64_t m_sampleRate;
	bool m_memoryDepthValid;
	int64_t m_memoryDepth;
	bool m_triggerOffsetValid;
	int64_t m_triggerOffset;
	std::map<size_t, int64_t> m_channelDeskew;
	bool m_interleaving;
	bool m_interleavingValid;

	//True if we have >8 bit capture depth
	bool m_highDefinition;

	//External trigger input
	OscilloscopeChannel* m_extTrigChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;

	//Mutexing for thread safety
	std::recursive_mutex m_cacheMutex;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(LeCroyOscilloscope)
};
#endif
