/***********************************************************************************************************************
*                                                                                                                      *
* WattWaveX4                                                                                                          *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of WattWaveX4
	@ingroup scopedrivers
 */

#ifndef WATTWAVEX4_H
#define WATTWAVEX4_H

#include "RemoteBridgeOscilloscope.h"

/**
	@brief WattWaveX4 - driver for interfacing with the WattWaveX4 power meter.

	@ingroup scopedrivers
 */
class WattWaveX4 : public RemoteBridgeOscilloscope
{
public:
	WattWaveX4(SCPITransport* transport);
	virtual ~WattWaveX4();

	// Not copyable or assignable
	WattWaveX4(const WattWaveX4& rhs) = delete;
	WattWaveX4& operator=(const WattWaveX4& rhs) = delete;

public:

	// Device information
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void FlushConfigCache() override;

	// Channel configuration
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual bool CanEnableChannel(size_t i) override;

	// Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void PushTrigger() override;

	// Timebase
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual std::set<InterleaveConflict> GetInterleaveConflicts() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual void SetSampleDepth(uint64_t) override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;








	// ADC configuration
	virtual std::vector<AnalogBank> GetAnalogBanks() override;
	virtual AnalogBank GetAnalogBank(size_t channel) override;
	virtual bool IsADCModeConfigurable() override;
	virtual std::vector<std::string> GetADCModeNames(size_t channel) override;
	virtual size_t GetADCMode(size_t channel) override;
	virtual void SetADCMode(size_t channel, size_t mode) override;

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
		SERIES_WattWaveX4,


		SERIES_UNKNOWN
	};

protected:
	void IdentifyHardware();

	std::string GetChannelColor(size_t i);

	// Hardware analog channel count, independent of LA option etc
	size_t m_analogChannelCount;
	size_t m_digitalChannelBase;
	size_t m_digitalChannelCount;

	// Most SCPI API calls are write-only, so we have to maintain all state client-side.
	// This isn't strictly a cache anymore since it's never flushed!
	std::map<size_t, double> m_channelAttenuations;
	
	Series m_series;
	
	
	

	
	
	

public:

	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(WattWaveX4)
	
	
	
		// Define the measurement data structure
struct meas_data_set {
    uint8_t stx;               // 1 byte (Start-of-text character, expected 0x02)
    uint16_t counter;          // 2 bytes (Counter)
    uint8_t channel1;         // 4 bytes (Assumed as uint32_t for INA229::Register)
    float meas_current[4];     // 16 bytes (4x4 bytes float)
} __attribute__((packed));      // Ensure no compiler padding
#define STX 0x55                    // Start-of-text character
//#define BUFFER_SIZE 1024             // Buffer size for reading multiple datasets
#define DATASET_SIZE sizeof(meas_data_set) // Ensure structure size matches binary data
	
	
	
};

#endif // WATTWAVEX4_H
