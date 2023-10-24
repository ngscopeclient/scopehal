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

#ifndef FunctionGenerator_h
#define FunctionGenerator_h

/**
	@brief A baseband waveform generator
 */
class FunctionGenerator : public virtual Instrument
{
public:
	FunctionGenerator();
	virtual ~FunctionGenerator();

	virtual bool AcquireData() override;

	enum WaveShape
	{
		SHAPE_SINE,
		SHAPE_SQUARE,
		SHAPE_TRIANGLE,
		SHAPE_PULSE,
		SHAPE_DC,
		SHAPE_NOISE,
		SHAPE_SAWTOOTH_UP,
		SHAPE_SAWTOOTH_DOWN,
		SHAPE_SINC,
		SHAPE_GAUSSIAN,
		SHAPE_LORENTZ,
		SHAPE_HALF_SINE,
		SHAPE_PRBS_NONSTANDARD,
		SHAPE_EXPONENTIAL_RISE,
		SHAPE_EXPONENTIAL_DECAY,
		SHAPE_HAVERSINE,
		SHAPE_CARDIAC,

		SHAPE_STAIRCASE_UP,
		SHAPE_STAIRCASE_DOWN,
		SHAPE_STAIRCASE_UP_DOWN,
		SHAPE_NEGATIVE_PULSE,
		SHAPE_LOG_RISE,
		SHAPE_LOG_DECAY,
		SHAPE_SQUARE_ROOT,
		SHAPE_CUBE_ROOT,
		SHAPE_QUADRATIC,
		SHAPE_CUBIC,
		SHAPE_DLORENTZ,
		SHAPE_GAUSSIAN_PULSE,
		SHAPE_HAMMING,
		SHAPE_HANNING,
		SHAPE_KAISER,
		SHAPE_BLACKMAN,
		SHAPE_GAUSSIAN_WINDOW,
		SHAPE_HARRIS,
		SHAPE_BARTLETT,
		SHAPE_TAN,
		SHAPE_COT,
		SHAPE_SEC,
		SHAPE_CSC,
		SHAPE_ASIN,
		SHAPE_ACOS,
		SHAPE_ATAN,
		SHAPE_ACOT,

		SHAPE_ARB
	};

	static std::string GetNameOfShape(WaveShape shape);
	static WaveShape GetShapeOfName(const std::string& name);

	//Configuration
	virtual bool GetFunctionChannelActive(int chan) =0;
	virtual void SetFunctionChannelActive(int chan, bool on) =0;

	/**
		@brief Determines if the function generator allows control over rise/fall times

		If this function returns false, GetFunctionChannelDutyCycle() will always return 0.5,
		and SetFunctionChannelRiseTime() and SetFunctionChannelDutyCycle() is a no-op.
	 */
	virtual bool HasFunctionDutyCycleControls(int chan);

	virtual float GetFunctionChannelDutyCycle(int chan);
	virtual void SetFunctionChannelDutyCycle(int chan, float duty);

	virtual float GetFunctionChannelAmplitude(int chan) =0;
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude) =0;

	virtual float GetFunctionChannelOffset(int chan) =0;
	virtual void SetFunctionChannelOffset(int chan, float offset) =0;

	virtual float GetFunctionChannelFrequency(int chan) =0;
	virtual void SetFunctionChannelFrequency(int chan, float hz) =0;

	virtual WaveShape GetFunctionChannelShape(int chan) =0;
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) =0;

	virtual float GetFunctionChannelRiseTime(int chan);
	virtual void SetFunctionChannelRiseTime(int chan, float fs);

	virtual float GetFunctionChannelFallTime(int chan);
	virtual void SetFunctionChannelFallTime(int chan, float fs);

	/**
		@brief Determines if the function generator allows control over rise/fall times

		If this function returns false, GetFunctionChannelRiseTime() and GetFunctionChannelFallTime()
		will always return 0, and SetFunctionChannelRiseTime() and SetFunctionChannelFallTime() are no-ops.
	 */
	virtual bool HasFunctionRiseFallTimeControls(int chan) =0;

	enum OutputImpedance
	{
		IMPEDANCE_HIGH_Z,
		IMPEDANCE_50_OHM
	};

	static std::string GetNameOfImpedance(OutputImpedance imp);

	/**
		@brief Determines if the function generator allows control over rise/fall times

		If this function returns false, GetFunctionChannelOutputImpedance() will always return IMPEDANCE_50_OHM
		and SetFunctionChannelOutputImpedance() is a no-op.
	 */
	virtual bool HasFunctionImpedanceControls(int chan);

	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan);
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z);

	//Query the set of available pre-defined waveforms for this generator
	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Serialization

protected:
	/**
		@brief Serializes this oscilloscope's configuration to a YAML node.
	 */
	void DoSerializeConfiguration(YAML::Node& node, IDTable& table);

	/**
		@brief Load instrument and channel configuration from a save file
	 */
	void DoLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap);

	/**
		@brief Validate instrument and channel configuration from a save file
	 */
	void DoPreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& list);
};

#endif
