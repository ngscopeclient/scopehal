/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2026 Shiz <hi@shiz.me>                                                                                 *
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

#ifndef Tektronix22xOscilloscope_h
#define Tektronix22xOscilloscope_h

#include <optional>
#include <cinttypes>

class Tektronix22xOscilloscope : public virtual SCPIOscilloscope
{
public:
	Tektronix22xOscilloscope(SCPITransport* transport);
	virtual ~Tektronix22xOscilloscope();

	// Not copyable or assignable
	Tektronix22xOscilloscope(const Tektronix22xOscilloscope& rhs) = delete;
	Tektronix22xOscilloscope& operator=(const Tektronix22xOscilloscope& rhs) = delete;

	// Driver information
	static std::string GetDriverNameInternal();
	// cppcheck-suppress duplInheritedMember
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels();

	//Device information
	virtual std::string IDPing() override;
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;
	virtual bool AcquireData() override;
	virtual void FlushConfigCache() override;

	// Oscilloscope interface: channels
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
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
	virtual bool CanAverage(size_t i) override;
	virtual size_t GetNumAverages(size_t i) override;
	virtual void SetNumAverages(size_t i, size_t navg) override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;
	virtual bool CanInvert(size_t i) override;
	virtual void Invert(size_t i, bool invert) override;
	virtual bool IsInverted(size_t i) override;

	// Oscilloscope interface: triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;

	// Oscilloscope interface: timebase & parameters
	virtual uint64_t GetSampleRate() override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual std::set<InterleaveConflict> GetInterleaveConflicts() override;
	virtual bool HasInterleavingControls() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;
	virtual bool IsSamplingModeAvailable(SamplingMode mode) override;
	virtual SamplingMode GetSamplingMode() override;
	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;

	enum Status : uint16_t
	{
		STA_UNKNOWN_CMD = 1,
		STA_UNKNOWN_CHAR = 2,
		STA_SET_ONLY = 3,
		STA_GET_ONLY = 4,
		STA_BAD_ARG = 5,
		STA_BAD_DATA = 6,
		STA_NO_DATA = 7,
		STA_NO_ARG = 8,
		STA_BUSY = 9,
		STA_CHECKSUM = 10,
		STA_BAD_NAME = 11,
		STA_ESCAPE = 0xFFFF,
	};
protected:
	// Trigger
	OscilloscopeChannel* m_extTrigChannel;
	std::optional<double> m_acqDoneTime;
	bool m_triggerArmed;
	bool m_rearmTrigger;

	// Config cache
	enum Model
	{
		MODEL_UNKNOWN,
		MODEL_222,
		MODEL_222A,
		MODEL_222PS,
		MODEL_224,
	} m_modelId;
	std::map<int, uint64_t> m_fpValues;
	std::map<int, uint16_t> m_dacValues;
	std::map<size_t, OscilloscopeChannel::CouplingType> m_channelCouplings;

	// Commands
	std::optional<Status> SendCommand(const std::string &cmd, const std::string &arg = "");
	std::optional<std::string> SendCommandWithResponse(const std::string &cmd, const std::string &arg = "");

	// Button interface
	enum Button
	{
		BUT_CLEAR = 1,
		BUT_MENU0 = 2,
		BUT_MENU1 = 3,
		BUT_MENU2 = 4,
		BUT_MENU3 = 5,
		BUT_OFF = 6,
		BUT_TRIG_SRC = 9,
		BUT_TRIG_MODE = 10,
		BUT_TRIG_SLOPE = 11,
		BUT_CH2 = 12,
		BUT_CH1 = 13,
		BUT_AUTO_SETUP = 14,
		BUT_SETUP = 17,
		BUT_TRIG_POS = 18,
		BUT_AUX_FUNC = 19,
		BUT_DISPL = 20,
		BUT_WAVE_SAVE = 25,
		BUT_WAVE_RCL = 26,
		BUT_STORE_MODE = 27,
		BUT_ACQ_MODE = 28,
		BUT_X10_MAG = 32,
		BUT_VAR = 33,
		BUT_AUTO_LVL = 34,
	};
	void PushButton(Button i);

	// FP interface
	enum FP
	{
		FP_ACQ,
		FP_REF1,
		FP_REF2,
		FP_REF3,
		FP_REF4,
		FP_STR1,
		FP_STR2,
		FP_STR3,
		FP_STR4,
	};
	const static size_t FP_COUNT = 9;
	std::optional<uint64_t> QueryFP(FP i);
	bool SetFP(FP i, uint64_t val);
	bool AdjustFP(FP i, uint8_t cat, uint8_t val, uint8_t mask);
	std::optional<uint8_t> QueryFPChannel(size_t i);
	bool AdjustFPChannel(size_t i, uint8_t val, uint8_t mask);

	// DAC interface
	enum DAC
	{
		DAC_HOR_POS = 0,
		DAC_CH1_TRIG_LEVEL = 1,
		DAC_CH2_TRIG_LEVEL = 2,
		DAC_EXT_TRIG_LEVEL = 3,
		DAC_CH2_GAIN = 4,
		DAC_CH1_GAIN = 5,
		DAC_CH2_POS = 6,
		DAC_CH1_POS = 7,
	};
	std::optional<uint16_t> QueryDAC(DAC i);
	bool SetDAC(DAC i, uint16_t val);

	// Trigger/misc interface
	void ArmTrigger();
	std::optional<uint64_t> QueryHorizontalScale();
	bool SetHorizontalScale(uint64_t scale);

public:
	OSCILLOSCOPE_INITPROC(Tektronix22xOscilloscope)
};

#endif
