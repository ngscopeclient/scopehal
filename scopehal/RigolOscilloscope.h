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

#ifndef RigolOscilloscope_h
#define RigolOscilloscope_h

class EdgeTrigger;

class RigolOscilloscope : public virtual SCPIOscilloscope
{
public:
	RigolOscilloscope(SCPITransport* transport);
	virtual ~RigolOscilloscope();

	//not copyable or assignable
	RigolOscilloscope(const RigolOscilloscope& rhs) = delete;
	RigolOscilloscope& operator=(const RigolOscilloscope& rhs) = delete;

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
	virtual std::vector<unsigned int> GetChannelBandwidthLimiters(size_t i) override;
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

	//Timebase
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

	void ForceHDMode(bool mode);

protected:
	enum family
	{
		DS1x_OLD,
		DS1x,
		MSO2x_DS2x, // MSO2000A(-S) / DS2000A series
		MSO5,	 //MSO5000 series
		DHO,	//DHO800, DHO900, DHO1000 and DHO4000 series
	};
	
	
	typedef struct
	{
		bool advanced_trigger;
		bool decoding;
		bool CAN_analysis;
		bool deep_memory;
	} stOpts_MSO2xDS2x;
	
	typedef struct
	{
		bool bw200M;
	} stOpts_MSO5;
	
	typedef union
	{
		stOpts_MSO2xDS2x mso2;
		stOpts_MSO5 mso5;
	} stOpts;
	
	OscilloscopeChannel* m_extTrigChannel;

	//hardware analog channel count, independent of LA option etc
	unsigned int m_analogChannelCount;

	//config cache
	std::map<size_t, double> m_channelAttenuations;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCouplings;
	std::map<size_t, float> m_channelOffsets;
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, unsigned int> m_channelBandwidthLimits;
	std::map<int, bool> m_channelsEnabled;
	bool m_srateValid;
	uint64_t m_srate;
	bool m_mdepthValid;
	uint64_t m_mdepth;
	int64_t m_triggerOffset;
	bool m_triggerOffsetValid;

	bool m_triggerArmed;
	bool m_triggerWasLive;
	bool m_triggerOneShot;

	bool m_liveMode;

	int m_modelNumber;
	unsigned int m_bandwidth;
	stOpts m_opts;
	
	uint64_t m_maxMdepth; /* Maximum Memory depth for DHO model s*/
	uint64_t m_maxSrate;  /* Maximum Sample rate for DHO models */
	bool m_lowSrate;	  /* True for DHO low sample rate models (DHO800/900) */
	family m_family;

	//True if we have >8 bit capture depth
	bool m_highDefinition;

	void PushEdgeTrigger(EdgeTrigger* trig);
	void PullEdgeTrigger();

	void PrepareStart();

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(RigolOscilloscope)
};

#endif
