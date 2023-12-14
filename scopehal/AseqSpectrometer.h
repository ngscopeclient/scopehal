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

#ifndef AseqSpectrometer_h
#define AseqSpectrometer_h

class EdgeTrigger;

#include "RemoteBridgeOscilloscope.h"

/**
	@brief Helper class for creating output streams
 */
class AseqSpectrometerChannel : public OscilloscopeChannel
{
public:
	AseqSpectrometerChannel(
		Oscilloscope* scope,
		const std::string& hwname,
		const std::string& color,
		size_t index)
		: OscilloscopeChannel(scope, hwname, color, Unit(Unit::UNIT_PM), index)
	{
		ClearStreams();

		AddStream(Unit::UNIT_COUNTS, "RawCounts", Stream::STREAM_TYPE_ANALOG);
		AddStream(Unit::UNIT_COUNTS, "FlattenedCounts", Stream::STREAM_TYPE_ANALOG);
		AddStream(Unit::UNIT_W_M2_NM, "AbsoluteIrradiance", Stream::STREAM_TYPE_ANALOG);
	}

	enum StreamIndex
	{
		STREAM_RAW_COUNTS,
		STREAM_FLATTENED_COUNTS,
		STREAM_ABSOLUTE_IRRADIANCE
	};
};

/**
	@brief AseqSpectrometer - driver for talking to the scopehal-aseq-bridge server
 */
class AseqSpectrometer 	: public virtual SCPISpectrometer
{
public:
	AseqSpectrometer(SCPITransport* transport);
	virtual ~AseqSpectrometer();

	//not copyable or assignable
	AseqSpectrometer(const AseqSpectrometer& rhs) =delete;
	AseqSpectrometer& operator=(const AseqSpectrometer& rhs) =delete;

public:

	virtual unsigned int GetInstrumentTypes() const override;
	uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual void FlushConfigCache() override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;

	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual uint64_t GetSampleRate() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;

	virtual int64_t GetIntegrationTime() override;
	virtual void SetIntegrationTime(int64_t t) override;

protected:
	bool m_triggerArmed;
	bool m_triggerOneShot;

	std::vector<float> m_wavelengths;
	std::vector<float> m_flatcal;
	std::vector<float> m_irrcal;
	float m_irrcoeff;

	enum channelids
	{
		CHAN_SPECTRUM,
		CHAN_DARKFRAME
	};

	SpectrometerDarkFrameChannel* m_darkframe;

	int64_t m_integrationTime;

public:
	static std::string GetDriverNameInternal();
	SPECTROMETER_INITPROC(AseqSpectrometer)
};

#endif
