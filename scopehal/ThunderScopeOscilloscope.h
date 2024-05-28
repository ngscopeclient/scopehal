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

#ifndef ThunderScopeOscilloscope_h
#define ThunderScopeOscilloscope_h

#include "RemoteBridgeOscilloscope.h"
#include "../xptools/HzClock.h"

/**
	@brief ThunderScopeOscilloscope - driver for talking to the TS.NET daemons
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
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual bool CanEnableChannel(size_t i) override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;

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

protected:
	void ResetPerCaptureDiagnostics();

	std::string GetChannelColor(size_t i);

	size_t m_analogChannelCount;

	// Cache
	std::map<size_t, double> m_channelAttenuations;

	FilterParameter m_diag_hardwareWFMHz;
	FilterParameter m_diag_receivedWFMHz;
	FilterParameter m_diag_totalWFMs;
	FilterParameter m_diag_droppedWFMs;
	FilterParameter m_diag_droppedPercent;
	HzClock m_receiveClock;

public:

	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(ThunderScopeOscilloscope);
};

#endif
