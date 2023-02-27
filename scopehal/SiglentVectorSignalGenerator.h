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

#ifndef SiglentVectorSignalGenerator_h
#define SiglentVectorSignalGenerator_h

/**
	@brief Siglent vector signal generators

	Tested on SSG5000X-V series. May also support 3000X but not tested.
 */
class SiglentVectorSignalGenerator
	: public virtual SCPIRFSignalGenerator
	, public virtual SCPIFunctionGenerator
{
public:
	SiglentVectorSignalGenerator(SCPITransport* transport);
	virtual ~SiglentVectorSignalGenerator();

	//Instrument
	virtual unsigned int GetInstrumentTypes();
	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();
	virtual size_t GetChannelCount();
	virtual uint32_t GetInstrumentTypesForChannel(size_t i);

	//RF signal generator base stuff
	virtual std::string GetChannelName(int chan);
	virtual bool GetChannelOutputEnable(int chan);
	virtual void SetChannelOutputEnable(int chan, bool on);
	virtual float GetChannelOutputPower(int chan);
	virtual void SetChannelOutputPower(int chan, float power);
	virtual float GetChannelCenterFrequency(int chan);
	virtual void SetChannelCenterFrequency(int chan, float freq);

	//Vector modulation
	virtual bool IsVectorModulationAvailable(int chan);

	//Sweep
	virtual bool IsSweepAvailable(int chan);
	virtual float GetSweepStartFrequency(int chan);
	virtual float GetSweepStopFrequency(int chan);
	virtual void SetSweepStartFrequency(int chan, float freq);
	virtual void SetSweepStopFrequency(int chan, float freq);
	virtual float GetSweepStartLevel(int chan);
	virtual float GetSweepStopLevel(int chan);
	virtual void SetSweepStartLevel(int chan, float level);
	virtual void SetSweepStopLevel(int chan, float level);
	virtual void SetSweepDwellTime(int chan, float fs);
	virtual float GetSweepDwellTime(int chan);
	virtual void SetSweepPoints(int chan, int npoints);
	virtual int GetSweepPoints(int chan);
	virtual SweepShape GetSweepShape(int chan);
	virtual void SetSweepShape(int chan, SweepShape shape);
	virtual SweepSpacing GetSweepSpacing(int chan);
	virtual void SetSweepSpacing(int chan, SweepSpacing shape);
	virtual SweepDirection GetSweepDirection(int chan);
	virtual void SetSweepDirection(int chan, SweepDirection dir);
	virtual SweepType GetSweepType(int chan);
	virtual void SetSweepType(int chan, SweepType type);

	//Function generator
	virtual int GetFunctionChannelCount();
	virtual std::string GetFunctionChannelName(int chan);
	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan);
	virtual bool GetFunctionChannelActive(int chan);
	virtual void SetFunctionChannelActive(int chan, bool on);
	virtual bool HasFunctionDutyCycleControls(int chan);
	virtual float GetFunctionChannelAmplitude(int chan);
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude);
	virtual float GetFunctionChannelOffset(int chan);
	virtual void SetFunctionChannelOffset(int chan, float offset);
	virtual float GetFunctionChannelFrequency(int chan);
	virtual void SetFunctionChannelFrequency(int chan, float hz);
	virtual WaveShape GetFunctionChannelShape(int chan);
	virtual void SetFunctionChannelShape(int chan, WaveShape shape);
	virtual bool HasFunctionRiseFallTimeControls(int chan);
	virtual bool HasFunctionImpedanceControls(int chan);

public:
	static std::string GetDriverNameInternal();
	VSG_INITPROC(SiglentVectorSignalGenerator)

protected:
	enum ChannelIDs
	{
		CHANNEL_RFOUT = 0,
		CHANNEL_LFO = 1
	};
};

#endif
