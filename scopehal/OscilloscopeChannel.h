/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of OscilloscopeChannel
 */

#ifndef OscilloscopeChannel_h
#define OscilloscopeChannel_h

class Oscilloscope;

#include "InstrumentChannel.h"

/**
	@brief A single channel on an oscilloscope.

	Each time the scope is triggered a new Waveform is created with the new capture's data.
 */
class OscilloscopeChannel : public InstrumentChannel
{
public:

	//Some drivers have to be able to call AddStream() for now (this will be refactored out eventually)
	friend class Oscilloscope;
	friend class MockOscilloscope;

	OscilloscopeChannel(
		Oscilloscope* scope,
		const std::string& hwname,
		const std::string& color,
		Unit xunit = Unit(Unit::UNIT_FS),
		size_t index = 0);

	OscilloscopeChannel(
		Oscilloscope* scope,
		const std::string& hwname,
		const std::string& color,
		Unit xunit = Unit(Unit::UNIT_FS),
		Unit yunit = Unit(Unit::UNIT_VOLTS),
		Stream::StreamType stype = Stream::STREAM_TYPE_ANALOG,
		size_t index = 0);
	virtual ~OscilloscopeChannel();

	//implemented in Oscilloscope.h
	Oscilloscope* GetScope();

	size_t GetRefCount()
	{ return m_refcount; }

	virtual void SetDisplayName(std::string name) override;
	virtual std::string GetDisplayName() override;

	//Hardware configuration
public:
	bool IsEnabled();

	//Warning: these functions FORCE the channel to be on or off. May break other code that assumes it's on.
	void Enable();
	void Disable();

	//These functions are preferred in GUI or other environments with multiple consumers of waveform data.
	//The channel is reference counted and only turned off when all consumers have released it.
	virtual void AddRef();
	virtual void Release();

	enum CouplingType
	{
		COUPLE_DC_1M,		//1M ohm, DC coupled
		COUPLE_AC_1M,		//1M ohm, AC coupled
		COUPLE_DC_50,		//50 ohm, DC coupled
		COUPLE_AC_50,		//50 ohm, AC coupled
		COUPLE_GND,			//tie to ground
		COUPLE_SYNTHETIC	//channel is math, digital, or otherwise not a direct voltage measurement
	};

	virtual CouplingType GetCoupling();
	virtual void SetCoupling(CouplingType type);

	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings();

	virtual double GetAttenuation();
	virtual void SetAttenuation(double atten);

	virtual int GetBandwidthLimit();
	virtual void SetBandwidthLimit(int mhz);

	virtual void SetDeskew(int64_t skew);
	virtual int64_t GetDeskew();

	bool IsPhysicalChannel()
	{ return (m_instrument != nullptr); }

	virtual bool IsInverted(size_t stream);

	virtual float GetVoltageRange(size_t stream);
	virtual void SetVoltageRange(float range, size_t stream);

	virtual float GetOffset(size_t stream);
	virtual void SetOffset(float offset, size_t stream);

	void SetDigitalHysteresis(float level);
	void SetDigitalThreshold(float level);

	void SetCenterFrequency(int64_t freq);

	bool CanAutoZero();
	void AutoZero();
	bool CanDegauss();
	void Degauss();
	std::string GetProbeName();

	virtual bool CanInvert();
	virtual void Invert(bool invert);
	virtual bool IsInverted();

	virtual bool HasInputMux();
	virtual size_t GetInputMuxSetting();
	virtual void SetInputMux(size_t select);

	void SetDefaultDisplayName();

	virtual DownloadState GetDownloadState() override;
	virtual float GetDownloadProgress() override;
	virtual double GetDownloadStartTime() override;

private:
	// to be accessed by Oscilloscope to update download status
	DownloadState m_downloadState;
	float m_downloadProgress;
	double m_downloadStartTime;

protected:
	void SharedCtorInit(Unit unit);

	///Number of references (channel is disabled when last ref is released)
	size_t m_refcount;
};

#endif
