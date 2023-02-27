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
	virtual unsigned int GetInstrumentTypes();
	virtual void FlushConfigCache();

	//Channel configuration
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i);
	virtual double GetChannelAttenuation(size_t i);
	virtual void SetChannelAttenuation(size_t i, double atten);
	virtual unsigned int GetChannelBandwidthLimit(size_t i);
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz);
	virtual OscilloscopeChannel* GetExternalTrigger();
	virtual bool CanEnableChannel(size_t i);
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool AcquireData();

	// Captures
	virtual void Start();
	virtual void StartSingleTrigger();
	virtual void ForceTrigger();

	//Timebase
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved();
	virtual std::vector<uint64_t> GetSampleRatesInterleaved();
	virtual std::set<InterleaveConflict> GetInterleaveConflicts();
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved();
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved();
	virtual bool IsInterleaving();
	virtual bool SetInterleaving(bool combine);

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
