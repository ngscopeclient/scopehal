/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
#ifndef FunctionGenerator_h
#define FunctionGenerator_h

/**
	@brief A generic waveform generator
 */
class FunctionGenerator : public virtual Instrument
{
public:
	FunctionGenerator();
	virtual ~FunctionGenerator();

	enum WaveShape
	{
		SHAPE_SINE,
		SHAPE_SQUARE,
		SHAPE_TRIANGLE,
		SHAPE_PULSE,
		SHAPE_DC,
		SHAPE_NOISE,
		SHAPE_ARB
	};

	//Channel info
	virtual int GetFunctionChannelCount() =0;
	virtual std::string GetFunctionChannelName(int chan) =0;

	//Configuration
	virtual bool GetFunctionChannelActive(int chan) =0;
	virtual void SetFunctionChannelActive(int chan, bool on) =0;

	virtual float GetFunctionChannelDutyCycle(int chan) =0;
	virtual void SetFunctionChannelDutyCycle(int chan, float duty) =0;

	virtual float GetFunctionChannelAmplitude(int chan) =0;
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude) =0;

	virtual float GetFunctionChannelOffset(int chan) =0;
	virtual void SetFunctionChannelOffset(int chan, float offset) =0;

	virtual float GetFunctionChannelFrequency(int chan) =0;
	virtual void SetFunctionChannelFrequency(int chan, float hz) =0;

	virtual WaveShape GetFunctionChannelShape(int chan) =0;
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) =0;

	virtual float GetFunctionChannelRiseTime(int chan) =0;
	virtual void SetFunctionChannelRiseTime(int chan, float sec) =0;

	virtual float GetFunctionChannelFallTime(int chan) =0;
	virtual void SetFunctionChannelFallTime(int chan, float sec) =0;
};

#endif
