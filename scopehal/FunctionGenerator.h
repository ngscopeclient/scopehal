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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of FunctionGenerator
	@ingroup core
 */

#ifndef FunctionGenerator_h
#define FunctionGenerator_h

/**
	@brief A baseband waveform generator

	@ingroup core
 */
class FunctionGenerator : public virtual Instrument
{
public:
	FunctionGenerator();
	virtual ~FunctionGenerator();

	virtual bool AcquireData() override;

	///@brief Predefined waveform shapes
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

	/**
		@brief Returns true if the function generator channel's output is enabled

		@param chan	Channel index
	 */
	virtual bool GetFunctionChannelActive(int chan) =0;

	/**
		@brief Turns a function generator channel on or off

		@param chan	Channel index
		@param on	True to enable output, false to disable
	 */
	virtual void SetFunctionChannelActive(int chan, bool on) =0;

	/**
		@brief Determines if the function generator allows control over duty cycles

		If this function returns false, GetFunctionChannelDutyCycle() will always return 0.5,
		and SetFunctionChannelRiseTime() and SetFunctionChannelDutyCycle() is a no-op.

		@param	chan	Channel index
		@return			True if duty cycle control is available, false if unavailable
	 */
	virtual bool HasFunctionDutyCycleControls(int chan);

	/**
		@brief Gets the duty cycle for a function generator output

		@param 	chan	Channel index
		@return			Duty cycle, in range [0, 1]
	 */
	virtual float GetFunctionChannelDutyCycle(int chan);

	/**
		@brief Sets the duty cycle for a function generator output

		@param chan		Channel index
		@param duty		Duty cycle, in range [0, 1]
	 */
	virtual void SetFunctionChannelDutyCycle(int chan, float duty);

	/**
		@brief Gets the amplitude for a function generator output

		@param 	chan	Channel index
		@return			Amplitude, in Vpp
	 */
	virtual float GetFunctionChannelAmplitude(int chan) =0;

	/**
		@brief Sets the amplitude for a function generator output

		@param chan			Channel index
		@param amplitude	Output amplitude, in Vpp
	 */
	virtual void SetFunctionChannelAmplitude(int chan, float amplitude) =0;

	/**
		@brief Gets the DC offset for a function generator output

		@param 	chan	Channel index
		@return			Offset, in volts
	 */
	virtual float GetFunctionChannelOffset(int chan) =0;

	/**
		@brief Sets the DC offset for a function generator output

		@param chan			Channel index
		@param amplitude	Offset, in volts
	 */
	virtual void SetFunctionChannelOffset(int chan, float offset) =0;

	/**
		@brief Gets the frequency for a function generator output

		@param 	chan	Channel index
		@return			Frequency, in Hz
	 */
	virtual float GetFunctionChannelFrequency(int chan) =0;

	/**
		@brief Sets the frequency for a function generator output

		@param chan		Channel index
		@param hz		Frequency, in Hz
	 */
	virtual void SetFunctionChannelFrequency(int chan, float hz) =0;

	/**
		@brief Gets the waveform shape for a function generator output

		@param 	chan	Channel index
		@return			Waveform shape
	 */
	virtual WaveShape GetFunctionChannelShape(int chan) =0;

	/**
		@brief Sets the waveform shape for a function generator output

		@param chan		Channel index
		@param shape	Desired output waveform
	 */
	virtual void SetFunctionChannelShape(int chan, WaveShape shape) =0;

	/**
		@brief Gets the rise time for a function generator output (if supported)

		@param 	chan	Channel index
		@return			Rise time, in fs
	 */
	virtual float GetFunctionChannelRiseTime(int chan);

	/**
		@brief Sets the rise time for a function generator output (if supported)

		@param 	chan	Channel index
		@param fs		Rise time, in fs
	 */
	virtual void SetFunctionChannelRiseTime(int chan, float fs);

	/**
		@brief Gets the fall time for a function generator output (if supported)

		@param 	chan	Channel index
		@return			Fall time, in fs
	 */
	virtual float GetFunctionChannelFallTime(int chan);

	/**
		@brief Sets the fall time for a function generator output (if supported)

		@param 	chan	Channel index
		@param fs		Fall time, in fs
	 */
	virtual void SetFunctionChannelFallTime(int chan, float fs);

	/**
		@brief Determines if the function generator allows control over rise/fall times

		If this function returns false, GetFunctionChannelRiseTime() and GetFunctionChannelFallTime()
		will always return 0, and SetFunctionChannelRiseTime() and SetFunctionChannelFallTime() are no-ops.

		@param	chan	Channel index
		@return			True if rise/fall time control is available, false if unavailable
	 */
	virtual bool HasFunctionRiseFallTimeControls(int chan) =0;

	///@brief Nominal output impedance for a function generator channel
	enum OutputImpedance
	{
		///@brief Channel expects to drive a high-impedance load
		IMPEDANCE_HIGH_Z,

		///@brief Channel expects to drive a 50-ohm load
		IMPEDANCE_50_OHM
	};

	static std::string GetNameOfImpedance(OutputImpedance imp);
	static OutputImpedance GetImpedanceOfName(const std::string& name);

	/**
		@brief Determines if the function generator allows control over rise/fall times

		If this function returns false, GetFunctionChannelOutputImpedance() will always return IMPEDANCE_50_OHM
		and SetFunctionChannelOutputImpedance() is a no-op.
	 */
	virtual bool HasFunctionImpedanceControls(int chan);

	/**
		@brief Gets the currently selected output impedance for a function generator output (if supported)

		@param 	chan	Channel index
		@return			Output impedance
	 */
	virtual OutputImpedance GetFunctionChannelOutputImpedance(int chan);

	/**
		@brief Sets the currently selected output impedance for a function generator output (if supported)

		@param 	chan	Channel index
		@param	z		Output impedance
	 */
	virtual void SetFunctionChannelOutputImpedance(int chan, OutputImpedance z);

	/**
		@brief Query the set of available pre-defined waveforms for this generator

		@param chan		Channel index

		@return			Vector of supported WaveShape's
	 */
	virtual std::vector<WaveShape> GetAvailableWaveformShapes(int chan) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Serialization

protected:

	void DoSerializeConfiguration(YAML::Node& node, IDTable& table);

	void DoLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap);

	void DoPreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& list);
};

#endif
