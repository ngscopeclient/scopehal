/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of IBISParser and related classes
 */

#ifndef IBISParser_h
#define IBISParser_h

//Almost all properties are indexed by a corner
enum IBISCorner
{
	CORNER_MIN,
	CORNER_TYP,
	CORNER_MAX
};

/**
	@brief A single current/voltage point
 */
class IVPoint
{
public:
	IVPoint()
	{}

	IVPoint(float v, float i)
	: m_voltage(v)
	, m_current(i)
	{}

	float m_voltage;
	float m_current;
};

/**
	@brief A generic current/voltage curve
 */
class IVCurve
{
public:

	float InterpolateCurrent(float voltage);

	///@brief The raw I/V curve data
	std::vector<IVPoint> m_curve;
};

/**
	@brief A single voltage/time point
 */
class VTPoint
{
public:
	VTPoint()
	{}

	VTPoint(float t, float v)
	: m_time(t)
	, m_voltage(v)
	{}

	float m_time;
	float m_voltage;
};

/**
	@brief Voltage/time curves for a waveform
 */
class VTCurves
{
public:
	VTCurves()
	: m_fixtureResistance(50)
	, m_fixtureVoltage(0)
	{}

	float InterpolateVoltage(IBISCorner corner, float time);

	float m_fixtureResistance;
	float m_fixtureVoltage;

	///@brief The raw V/T curve data
	std::vector<VTPoint> m_curves[3];
};

/**
	@brief An IBIS model (for a single type of buffer)

	For now, we only support I/O or output type models and ignore all inputs.
 */
class IBISModel
{
public:
	IBISModel(const std::string& name)
	: m_type(TYPE_IO)
	, m_name(name)
	, m_vil{0}
	, m_vih{0}
	, m_temps{0}
	, m_voltages{0}
	, m_dieCapacitance{0}
	{}

	//Model type
	enum type_t
	{
		TYPE_INPUT,
		TYPE_IO,
		TYPE_OPEN_DRAIN,
		TYPE_OUTPUT,
		TYPE_SERIES,
		TYPE_TERMINATOR
	} m_type;

	//Name of the model
	std::string	m_name;

	//I/V curves for each output buffer
	IVCurve m_pulldown[3];
	IVCurve m_pullup[3];

	//V/T curves for each output buffer
	std::vector<VTCurves> m_rising;
	std::vector<VTCurves> m_falling;

	//Input thresholds
	float m_vil[3];
	float m_vih[3];

	//Temperature and voltage values at each corner
	float m_temps[3];
	float m_voltages[3];

	//Component capacitance
	//TODO: support C_comp_pull* separately
	float m_dieCapacitance[3];

	VTCurves* GetLowestFallingWaveform();
	VTCurves* GetLowestRisingWaveform();

	VTCurves* GetHighestFallingWaveform();
	VTCurves* GetHighestRisingWaveform();

	std::vector<float> CalculateTurnonCurve(
		VTCurves* curve,
		IVCurve* pullup,
		IVCurve* pulldown,
		IBISCorner corner,
		float dt,
		bool rising);

	AnalogWaveform* SimulatePRBS(
		/*DigitalWaveform* input, */
		uint32_t seed,
		IBISCorner corner,
		int64_t timescale,
		size_t length,
		size_t ui);
};

/**
	@brief IBIS file parser (may contain multiple models)
 */
class IBISParser
{
public:
	IBISParser();
	virtual ~IBISParser();

	void Clear();
	bool Load(std::string fname);

	std::string m_component;
	std::string m_manufacturer;

	std::map<std::string, IBISModel*> m_models;

protected:
	float ParseNumber(const char* str);
};

#endif
