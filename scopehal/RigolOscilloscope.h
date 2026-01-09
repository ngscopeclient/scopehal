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
	virtual bool CanEnableChannel(size_t i) override;
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
	virtual bool IsDigitalThresholdConfigurable() override;
	virtual float GetDigitalThreshold(size_t channel) override;
	virtual void SetDigitalThreshold(size_t channel, float level) override;
	virtual std::vector<DigitalBank> GetDigitalBanks() override;
	virtual DigitalBank GetDigitalBank(std::size_t channel) override;

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
	virtual bool HasInterleavingControls() override;
	virtual bool CanInterleave() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;

	void ForceHDMode(bool mode);

protected:
	// private/internal types and functions
	enum class Series
	{
		UNKNOWN,

		DS1000,
		MSODS1000Z,
		// MSODS2000,
		// MSO4000,
		MSO5000,
		// MSODS7000,
		// MSO8000,
		// DS70000,

		DHO1000,
		DHO4000,
		DHO800,
		DHO900,
	};

	enum class CaptureFormat : int {
		BYTE_ = 0, // a waveform point occupies one byte (namely 8 bits). Suffix _ because msys64/ucrt64/include/minwindef.h defines BYTE as char :facepalm:
		WORD_ = 1, // a waveform point occupies two bytes (namely 16 bits) in which the lower 8 bits are valid and the higher 8 bits are 0. Suffix _ because msys64/ucrt64/include/minwindef.h defines BYTE as char :facepalm:
		ASC = 2   // waveform points in character number. Waveform points are returned in scientific notation and separated by commas./
	};

	enum class CaptureType : int {
		NORMAL = 0,  // NORMal: read the waveform data displayed on the screen.
		MAXIMUM = 1, // MAXimum: read the waveform data displayed on the screen when the instrument is in the run state and the waveform data in the internal memory in the stop state.
		RAW = 2      // RAW: read the waveform data in the internal memory. Note that the waveform data in the internal memory can only be read when the oscilloscope is in the stop state and the oscilloscope can not be operated.
		// NOTE: If the MATH channel is selected, only the NORMal mode is valid.
	};

	// Rigol scopes organize digital channels into "PODs" of size 8, we call them banks
	static constexpr auto DIGITAL_BANK_SIZE = 8;

	bool IsLaEnabled();
	void LaEnable();
	void LaDisable();

	// bool IsLaPodEnabled(std::size_t pod);
	// void LaPodEnable(std::size_t pod);
	// void LaPodDisable(std::size_t pod);

	std::size_t GetEnabledAnalogChannelCount();

	struct CapturePreamble {
		CaptureFormat format {};
		CaptureType type {};
		std::uint_least32_t npoints {}; // an integer between 1 and 12000000.
		std::uint_least32_t averages {}; // the number of averages in the average sample mode and 1 in other modes.
		double sec_per_sample {}; // the time difference between two neighboring points in the X direction.
		double xorigin {}; // the time from the trigger point to the "Reference Time" in the X direction.
		double xreference {}; // the reference time of the data point in the X direction.
		double yincrement {}; // the waveform increment in the Y direction.
		double yorigin {}; // the vertical offset relative to the "Vertical Reference Position" in the Y direction.
		double yreference {}; // the vertical reference position in the Y direction.
	};

	std::optional<CapturePreamble> GetCapturePreamble();
	void StartPre();
	void StartPost();
	void DecodeDeviceSeries();
	void AnalyzeDeviceCapabilities();
	void UpdateDynamicCapabilities(); // capabilities dependent on enabled chanel count
	std::size_t GetMsods1000ZChannelDivisor(); // helper function to get memory depth/sample rate divisor base on current scope state (amount of enabled channels)
	std::array<std::size_t, 2> GetMso5000AnalogBankUsage();
	bool IsChannelAnalog(std::size_t i);
	bool IsChannelDigital(std::size_t i);
	std::uint64_t GetPendingWaveformBlockLength(); // extratcs waveform block size from the TMC header at beginnign of each response to :WAV:DATA? command
	void LogLaNotPresent();
	std::size_t IdxToAnalogChannelNumber(std::size_t i); // does not check for validity!
	std::size_t AnalogChannelNumberToIdx(std::size_t i); // does not check for validity!
	std::size_t IdxToDigitalChannelNumber(std::size_t i); // does not check for validity!
	std::size_t DigitalChannelNumberToIdx(std::size_t i); // does not check for validity!
	std::size_t IdxToDigitalBankIdx(std::size_t i); // does not check for validity!

protected:
	OscilloscopeChannel* m_extTrigChannel {};
	OscilloscopeChannel* m_aclTrigChannel {}; // TODO: scopehal does not handle AC "external triggers" ATM

	// hardware analog channel count, independent of LA option etc
	size_t m_analogChannelCount {};

	size_t m_digitalChannelCount {};
	std::vector<Oscilloscope::DigitalBank> m_digitalBanks;

	// config cache, values that can be updated whenever needed
	// all access to these shall be exclusive using `m_cacheMutex`
	std::map<size_t, double> m_channelAttenuations;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCouplings;
	std::map<size_t, float> m_channelOffsets;
	std::map<size_t, float> m_channelVoltageRanges;
	std::map<size_t, unsigned int> m_channelBandwidthLimits;
	std::map<size_t, float> m_bankThresholds;
	std::map<int, bool> m_channelsEnabled;
	std::vector<std::uint64_t> m_depths;
	std::optional<uint64_t> m_srate;
	std::optional<uint64_t> m_mdepth;
	std::optional<int64_t> m_triggerOffset;
	std::optional<bool> m_laEnabled;

	// state variables, may alter values during runtime
	bool m_triggerArmed {};
	bool m_triggerWasLive {};
	bool m_triggerOneShot {};
	std::uint_least32_t m_pointsWhenStarted {}; // used for some series as a part of trigger state detection workaround, sampled points reported right after arming

	bool m_liveMode {};

	// constants once the ctor finishes
	struct Model {
		std::string prefix; // e.g.: DS
		unsigned int number; // e.g.: 1054
		std::string suffix; // e.g.: Z
	} m_modelNew;
	
	unsigned int m_bandwidth {};
	bool m_opt200M {}; // 200M memory depth is MSO5000 specific option 
	bool m_opt24M {};  // 24M memory depth is DS1000Z specific option 
	uint64_t m_maxMdepth {}; // Maximum Memory depth for DHO models
	uint64_t m_maxSrate {};  // Maximum Sample rate for DHO models
	bool m_lowSrate {};	  // True for DHO low sample rate models (DHO800/900)
	Series m_series;

	//True if we have >8 bit capture depth
	//TODO: switch to GetADCMode/SetADCMode API instead of having custom attribute switching DHOs to 12 bits
	bool m_highDefinition;

	void PushEdgeTrigger(EdgeTrigger* trig);
	void PullEdgeTrigger();

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(RigolOscilloscope)
};

#endif
