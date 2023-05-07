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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of MockOscilloscope
 */

#ifndef MockOscilloscope_h
#define MockOscilloscope_h

/**
	@brief Fake oscilloscope driver used for offline waveform analysis
 */
class MockOscilloscope : public Oscilloscope
{
public:
	MockOscilloscope(
		const std::string& name,
		const std::string& vendor,
		const std::string& serial,
		const std::string& transport,
		const std::string& driver,
		const std::string& args
		);
	virtual ~MockOscilloscope();

	virtual bool IsOffline();

	Unit::UnitType units[7] = {
		Unit::UNIT_COUNTS,	//Unused
		Unit::UNIT_VOLTS,
		Unit::UNIT_FS,
		Unit::UNIT_COUNTS,	//"Constant"
		Unit::UNIT_AMPS,
		Unit::UNIT_DB,
		Unit::UNIT_HZ
	};

	//not copyable or assignable
	MockOscilloscope(const MockOscilloscope& rhs) =delete;
	MockOscilloscope& operator=(const MockOscilloscope& rhs) =delete;

	void AddChannel(OscilloscopeChannel* chan)
	{ m_channels.push_back(chan); }

	virtual std::string IDPing();

	virtual std::string GetTransportConnectionString();
	virtual std::string GetTransportName();

	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i);
	virtual void EnableChannel(size_t i);
	virtual void DisableChannel(size_t i);
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i);
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type);
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i);
	virtual double GetChannelAttenuation(size_t i);
	virtual void SetChannelAttenuation(size_t i, double atten);
	virtual unsigned int GetChannelBandwidthLimit(size_t i);
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz);
	virtual float GetChannelVoltageRange(size_t i, size_t stream);
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range);
	virtual OscilloscopeChannel* GetExternalTrigger();
	virtual float GetChannelOffset(size_t i, size_t stream);
	virtual void SetChannelOffset(size_t i, size_t stream, float offset);

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
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) override;
	void DoLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap);

protected:

	void ArmTrigger();

	//standard *IDN? fields
	std::string m_name;
	std::string m_vendor;
	std::string m_serial;
	std::string m_fwVersion;

	OscilloscopeChannel* m_extTrigger;

	std::map<size_t, bool> m_channelsEnabled;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCoupling;
	std::map<size_t, double> m_channelAttenuation;
	std::map<size_t, unsigned int> m_channelBandwidth;
	std::map<std::pair<size_t, size_t>, float> m_channelVoltageRange;
	std::map<std::pair<size_t, size_t>, float> m_channelOffset;

	void AutoscaleVertical();

	//Simulated transport information
	std::string m_transport;
	std::string m_driver;
	std::string m_args;

	uint64_t m_sampleRate;
	uint64_t m_sampleDepth;

public:

	virtual std::string GetDriverName()
	{ return m_driver; }
};

#endif
