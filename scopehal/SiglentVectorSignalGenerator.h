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
	virtual unsigned int GetInstrumentTypes() override;
	virtual std::string GetName() override;
	virtual std::string GetVendor() override;
	virtual std::string GetSerial() override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) override;

	//RF signal generator base stuff
	virtual std::string GetChannelName(int chan) override;
	virtual bool GetChannelOutputEnable(int chan) override;
	virtual void SetChannelOutputEnable(int chan, bool on) override;
	virtual float GetChannelOutputPower(int chan) override;
	virtual void SetChannelOutputPower(int chan, float power) override;
	virtual float GetChannelCenterFrequency(int chan) override;
	virtual void SetChannelCenterFrequency(int chan, float freq) override;

	//Vector modulation
	virtual bool IsVectorModulationAvailable(int chan) override;

	//Sweep
	virtual bool IsSweepAvailable(int chan) override;
	virtual float GetSweepStartFrequency(int chan) override;
	virtual float GetSweepStopFrequency(int chan) override;
	virtual void SetSweepStartFrequency(int chan, float freq) override;
	virtual void SetSweepStopFrequency(int chan, float freq) override;
	virtual float GetSweepStartLevel(int chan) override;
	virtual float GetSweepStopLevel(int chan) override;
	virtual void SetSweepStartLevel(int chan, float level) override;
	virtual void SetSweepStopLevel(int chan, float level) override;
	virtual void SetSweepDwellTime(int chan, float fs) override;
	virtual float GetSweepDwellTime(int chan) override;
	virtual void SetSweepPoints(int chan, int npoints) override;
	virtual int GetSweepPoints(int chan) override;
	virtual SweepShape GetSweepShape(int chan) override;
	virtual void SetSweepShape(int chan, SweepShape shape) override;
	virtual SweepSpacing GetSweepSpacing(int chan) override;
	virtual void SetSweepSpacing(int chan, SweepSpacing shape) override;
	virtual SweepDirection GetSweepDirection(int chan) override;
	virtual void SetSweepDirection(int chan, SweepDirection dir) override;
	virtual SweepType GetSweepType(int chan) override;
	virtual void SetSweepType(int chan, SweepType type) override;

	//Function generator
	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) override;
	virtual bool GetFunctionChannelActive(int chan) override;
	virtual void SetFunctionChannelActive(int chan, bool on) override;
	virtual bool HasFunctionDutyCycleControls(int chan) override;
	virtual float GetFunctionChannelAmplitude(int chan) override;
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude) override;
	virtual float GetFunctionChannelOffset(int chan) override;
	virtual void SetFunctionChannelOffset(int chan, float offset) override;
	virtual float GetFunctionChannelFrequency(int chan) override;
	virtual void SetFunctionChannelFrequency(int chan, float hz) override;
	virtual WaveShape GetFunctionChannelShape(int chan) override;
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) override;
	virtual bool HasFunctionRiseFallTimeControls(int chan) override;
	virtual bool HasFunctionImpedanceControls(int chan) override;

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
