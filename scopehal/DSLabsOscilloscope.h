/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.g                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2022- Andrew D. Zonenberg and contributors                                                         *
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

#ifndef DSLabsOscilloscope_h
#define DSLabsOscilloscope_h

#include "RemoteBridgeOscilloscope.h"

/**
	@brief DSLabsOscilloscope - driver for talking to the scopehal-dslabs-bridge daemons
 */
class DSLabsOscilloscope : public RemoteBridgeOscilloscope
{
public:
	DSLabsOscilloscope(SCPITransport* transport);
	virtual ~DSLabsOscilloscope();

	//not copyable or assignable
	DSLabsOscilloscope(const DSLabsOscilloscope& rhs) =delete;
	DSLabsOscilloscope& operator=(const DSLabsOscilloscope& rhs) =delete;

public:

	//Device information
	virtual unsigned int GetInstrumentTypes();
	virtual void FlushConfigCache();

	//Channel configuration
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i);
	virtual double GetChannelAttenuation(size_t i);
	virtual void SetChannelAttenuation(size_t i, double atten);
	virtual int GetChannelBandwidthLimit(size_t i);
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz);
	virtual OscilloscopeChannel* GetExternalTrigger();
	virtual bool CanEnableChannel(size_t i);

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool AcquireData();

	//Timebase
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved();
	virtual std::vector<uint64_t> GetSampleRatesInterleaved();
	virtual std::set<InterleaveConflict> GetInterleaveConflicts();
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved();
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved();
	virtual bool IsInterleaving();
	virtual bool SetInterleaving(bool combine);

	//ADC configuration
	virtual std::vector<AnalogBank> GetAnalogBanks();
	virtual AnalogBank GetAnalogBank(size_t channel);
	virtual bool IsADCModeConfigurable();
	virtual std::vector<std::string> GetADCModeNames(size_t channel);
	virtual size_t GetADCMode(size_t channel);
	virtual void SetADCMode(size_t channel, size_t mode);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Logic analyzer configuration

	virtual std::vector<DigitalBank> GetDigitalBanks();
	virtual DigitalBank GetDigitalBank(size_t channel);
	virtual bool IsDigitalHysteresisConfigurable();
	virtual bool IsDigitalThresholdConfigurable();
	virtual float GetDigitalHysteresis(size_t channel);
	virtual float GetDigitalThreshold(size_t channel);
	virtual void SetDigitalHysteresis(size_t channel, float level);
	virtual void SetDigitalThreshold(size_t channel, float level);

	enum Series
	{
		DSCOPE_U3P100,
		DSLOGIC_U3PRO16,

		SERIES_UNKNOWN	//unknown or invalid model name
	};

protected:
	void IdentifyHardware();

	std::string GetChannelColor(size_t i);

	size_t m_analogChannelCount;
	size_t m_digitalChannelBase;
	size_t m_digitalChannelCount;

	//Most DSLabs API calls are write only, so we have to maintain all state clientside.
	//This isn't strictly a cache anymore since it's never flushed!
	std::map<size_t, double> m_channelAttenuations;

	// Only configurable for the entire device
	float m_digitalThreshold;

	void SendDataSocket(size_t n, const uint8_t* p);
	bool ReadDataSocket(size_t n, uint8_t* p);

	Socket* m_dataSocket;

	Series m_series;

public:

	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(DSLabsOscilloscope);
};

#endif
