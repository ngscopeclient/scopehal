/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of IBISParser and related classes
 */
#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IVCurve

float IVCurve::InterpolateCurrent(float voltage)
{
	//Binary search to find the points straddling us
	size_t len = m_curve.size();
	size_t pos = len/2;
	size_t last_lo = 0;
	size_t last_hi = len - 1;

	if(len == 0)
		return 0;

	//If out of range, clip
	if(voltage < m_curve[0].m_voltage)
		return m_curve[0].m_current;
	else if(voltage > m_curve[len-1].m_voltage)
		return m_curve[len-1].m_current;
	else
	{
		while(true)
		{
			//Dead on? Stop
			if( (last_hi - last_lo) <= 1)
				break;

			//Too high, move down
			if(m_curve[pos].m_voltage > voltage)
			{
				size_t delta = (pos - last_lo);
				last_hi = pos;
				pos = last_lo + delta/2;
			}

			//Too low, move up
			else
			{
				size_t delta = last_hi - pos;
				last_lo = pos;
				pos = last_hi - delta/2;
			}
		}
	}

	//Find position between the points for interpolation
	float vlo = m_curve[last_lo].m_voltage;
	float vhi = m_curve[last_hi].m_voltage;
	float dv = vhi - vlo;
	float frac = (voltage - vlo) / dv;

	//Interpolate current
	float ilo = m_curve[last_lo].m_current;
	float ihi = m_curve[last_hi].m_current;
	return ilo + (ihi - ilo)*frac;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VTCurve

float VTCurves::InterpolateVoltage(IBISCorner corner, float time)
{
	//Binary search to find the points straddling us
	size_t len = m_curves[corner].size();
	size_t pos = len/2;
	size_t last_lo = 0;
	size_t last_hi = len - 1;

	if(len == 0)
		return 0;

	//If out of range, clip
	if(time < m_curves[corner][0].m_time)
		return m_curves[corner][0].m_voltage;
	else if(time > m_curves[corner][len-1].m_time)
		return m_curves[corner][len-1].m_voltage;
	else
	{
		while(true)
		{
			//Dead on? Stop
			if( (last_hi - last_lo) <= 1)
				break;

			//Too high, move down
			if(m_curves[corner][pos].m_time > time)
			{
				size_t delta = (pos - last_lo);
				last_hi = pos;
				pos = last_lo + delta/2;
			}

			//Too low, move up
			else
			{
				size_t delta = last_hi - pos;
				last_lo = pos;
				pos = last_hi - delta/2;
			}
		}
	}

	//Find position between the points for interpolation
	float tlo = m_curves[corner][last_lo].m_time;
	float thi = m_curves[corner][last_hi].m_time;
	float dt = thi - tlo;
	float frac = (time - tlo) / dt;

	//Interpolate voltage
	float vlo = m_curves[corner][last_lo].m_voltage;
	return vlo + (m_curves[corner][last_hi].m_voltage - vlo)*frac;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IBISModel

/**
	@brief Get the falling-edge waveform terminated to ground (or lowest available voltage)
 */
VTCurves* IBISModel::GetLowestFallingWaveform()
{
	VTCurves* ret = &m_falling[0];
	for(auto& curve : m_falling)
	{
		if(curve.m_fixtureVoltage < ret->m_fixtureVoltage)
			ret = &curve;
	}
	return ret;
}

/**
	@brief Get the rising-edge waveform terminated to ground (or lowest available voltage)
 */
VTCurves* IBISModel::GetLowestRisingWaveform()
{
	VTCurves* ret = &m_rising[0];
	for(auto& curve : m_rising)
	{
		if(curve.m_fixtureVoltage < ret->m_fixtureVoltage)
			ret = &curve;
	}
	return ret;
}

/**
	@brief Get the falling-edge waveform terminated to Vcc (or highest available voltage)
 */
VTCurves* IBISModel::GetHighestFallingWaveform()
{
	VTCurves* ret = &m_falling[0];
	for(auto& curve : m_falling)
	{
		if(curve.m_fixtureVoltage > ret->m_fixtureVoltage)
			ret = &curve;
	}
	return ret;
}

/**
	@brief Get the rising-edge waveform terminated to Vcc (or lowest available voltage)
 */
VTCurves* IBISModel::GetHighestRisingWaveform()
{
	VTCurves* ret = &m_rising[0];
	for(auto& curve : m_rising)
	{
		if(curve.m_fixtureVoltage > ret->m_fixtureVoltage)
			ret = &curve;
	}
	return ret;
}

/**
	@brief Calculate the turn-on curve for buffer.

	Each output point ranges from 0 (fully off) to 1 (fully on)

	TODO: take in multiple corners so we can use low voltage and high cap, etc

	@param curve	V/T curve to use
	@param oldstate	I/V curve set for currently-on buffer
	@param newstate	I/V curve set for currently-off buffer
	@param corner	Corner to target
	@param dt		Timestep for the simulation
	@param rising	True for rising edge, false for falling edge
 */
vector<float> IBISModel::CalculateTurnonCurve(
	VTCurves* curve,
	IVCurve* pullup,
	IVCurve* pulldown,
	IBISCorner corner,
	float dt,
	bool rising)
{
	vector<float> ret;

	float cap = m_dieCapacitance[corner];
	float vcc = m_voltages[corner];
	float last_v = curve->InterpolateVoltage(corner, 0);

	float epsilon = 0.005;
	int last_percent = 0;
	for(size_t nstep=0; nstep<2000; nstep ++)
	{
		float time = dt*nstep;
		float v = curve->InterpolateVoltage(corner, time);

		//See how much the capacitor voltage changed in this time, then calculate charge/discharge current
		float dv = v - last_v;
		float icap = cap * dv/dt;
		last_v = v;

		//Total drive current is cap charge/discharge current plus load current pulled by the transmission line
		float iline = (v - curve->m_fixtureVoltage) / curve->m_fixtureResistance;
		float idrive = icap + iline;

		//Bruteforce sweep pullup and pulldown current to find the best combination
		float onfrac = 0;
		float delta = FLT_MAX;
		for(int percent=last_percent; percent <= 100; percent ++)
		{
			float f = percent / 100.0f;

			float iup, idown;
			if(rising)
			{
				iup = -pullup[corner].InterpolateCurrent(vcc-v) * f;
				idown = -pulldown[corner].InterpolateCurrent(v) * (1-f);
			}
			else
			{
				iup = -pullup[corner].InterpolateCurrent(vcc-v) * (1-f);
				idown = -pulldown[corner].InterpolateCurrent(v) * (f);
			}

			float itotal = iup + idown;
			float dnew = fabs(itotal - idrive);

			if(dnew < delta)
			{
				last_percent = percent;
				onfrac = f;
				delta = dnew;
			}
		}

		if(rising)
			ret.push_back(onfrac);
		else
			ret.push_back(1 - onfrac);

		//If we're almost fully on, stop the curve
		if(fabs(1.0 - onfrac) < epsilon)
			break;
	}

	return ret;
}

/**
	@brief Simulates this model and returns the waveform

	For now, hard coded to PRBS-31 waveform

	@param corner		Process corner to simulate (TODO others)
	@param timescale	Picoseconds per simulation time step
	@param length		Number of time steps to simulate
	@param ui			Unit interval for the PRBS
 */
AnalogWaveform* IBISModel::SimulatePRBS(
	/*DigitalWaveform* input, */
	uint32_t seed,
	IBISCorner corner,
	int64_t timescale,
	size_t length,
	size_t ui)
{
	//Find the rising and falling edge waveform terminated to the highest voltage (Vcc etc)
	//TODO: make this configurable
	VTCurves* rising = GetHighestRisingWaveform();
	VTCurves* falling = GetHighestFallingWaveform();

	const float dt = timescale * 1e-12;

	//PRBS-31 generator
	uint32_t prbs = seed;

	//Create the output waveform
	auto ret = new AnalogWaveform;
	ret->m_timescale = timescale;
	float now = GetTime();
	float tfrac = fmodf(now, 1);
	ret->m_startTimestamp = round(now - tfrac);
	ret->m_startPicoseconds = tfrac * 1e12;
	ret->m_triggerPhase = 0;
	ret->Resize(length);

	//Play rising/falling waveforms
	size_t	last_ui_start			= 0;
	size_t	ui_start				= 0;
	bool	current_bit				= false;
	bool	last_bit				= false;
	float	current_v_old			= 0;
	bool	current_edge_started	= false;
	for(size_t nstep=0; nstep<length; nstep ++)
	{
		//Advance to next UI
		if(0 == (nstep % ui))
		{
			last_bit		= current_bit;

			if(nstep != 0)
			{
				//PRBS-31 generator
				uint32_t next = ( (prbs >> 31) ^ (prbs >> 28) ) & 1;
				prbs = (prbs << 1) | next;
				current_bit = next ? true : false;

				//Keep the old edge going
				current_edge_started = false;
			}

			ui_start		= nstep;
		}

		//Get phase of current and previous UI
		size_t	current_phase	= nstep - ui_start;
		size_t	last_phase		= nstep - last_ui_start;

		//Get value for current and previous edge
		float current_v;
		if(current_bit)
			current_v = rising->InterpolateVoltage(corner, current_phase*dt);
		else
			current_v = falling->InterpolateVoltage(corner, current_phase*dt);

		float last_v;
		if(last_bit)
			last_v = rising->InterpolateVoltage(corner, last_phase*dt);
		else
			last_v = falling->InterpolateVoltage(corner, last_phase*dt);

		//See if the current UI's edge has started
		float delta = current_v - current_v_old;
		if(current_phase < 1)
			delta = 0;
		if( (fabs(delta) > 0.001) && (last_bit != current_bit) )
		{
			last_ui_start	= ui_start;
			current_edge_started = true;
		}

		//If so, use the new value. If propagation delay isn't over, keep the old edge going
		float v;
		if(current_edge_started)
			v = current_v;
		else
			v = last_v;

		current_v_old	= current_v;

		//Save the voltage
		ret->m_offsets[nstep] = nstep;
		ret->m_durations[nstep] = 1;
		ret->m_samples[nstep] = (double)v;
	}

	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IBISParser

IBISParser::IBISParser()
{
}

IBISParser::~IBISParser()
{
	Clear();
}

void IBISParser::Clear()
{
	for(auto it : m_models)
		delete it.second;
	m_models.clear();
}

bool IBISParser::Load(string fname)
{
	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("IBIS file \"%s\" could not be opened\n", fname.c_str());
		return false;
	}

	//Comment char defaults to pipe, but can be changed (weird)
	char comment = '|';

	enum
	{
		BLOCK_NONE,
		BLOCK_PULLDOWN,
		BLOCK_PULLUP,
		BLOCK_GND_CLAMP,
		BLOCK_POWER_CLAMP,
		BLOCK_RISING_WAVEFORM,
		BLOCK_FALLING_WAVEFORM,
		BLOCK_MODEL_SPEC,
		BLOCK_RAMP,
		BLOCK_SUBMODEL
	} data_block = BLOCK_NONE;

	//IBIS file is line oriented, so fetch an entire line then figure out what to do with it.
	//Per IBIS 6.0 spec rule 3.4, files cannot be >120 chars so if we truncate at 127 we should be good.
	char line[128];
	char command[128];
	char tmp[128];
	IBISModel* model = NULL;
	VTCurves waveform;
	while(!feof(fp))
	{
		if(fgets(line, sizeof(line), fp) == NULL)
			break;

		//Skip comments
		if(line[0] == comment)
			continue;

		//Parse commands
		if(line[0] == '[')
		{
			if(1 != sscanf(line, "[%[^]]]", command))
				continue;
			string scmd(command);

			//If in a waveform, save it when the block ends
			if(data_block == BLOCK_RISING_WAVEFORM)
				model->m_rising.push_back(waveform);
			else if(data_block == BLOCK_FALLING_WAVEFORM)
				model->m_falling.push_back(waveform);

			//End of file
			if(scmd == "END")
				break;

			//Metadata
			if(scmd == "Component")
			{
				sscanf(line, "[Component] %s", tmp);
				m_component = tmp;
			}
			else if(scmd == "Manufacturer")
			{
				sscanf(line, "[Manufacturer] %s", tmp);
				m_manufacturer = tmp;
			}
			else if(scmd == "IBIS ver")
			{}
			else if(scmd == "File name")
			{}
			else if(scmd == "File Rev")
			{}
			else if(scmd == "Date")
			{}
			else if(scmd == "Source")
			{}
			else if(scmd == "Notes")
			{}
			else if(scmd == "Disclaimer")
			{}
			else if(scmd == "Copyright")
			{}
			else if(scmd == "Package")
			{}

			//Start a new model
			else if(scmd == "Model")
			{
				sscanf(line, "[Model] %s", tmp);
				model = new IBISModel(tmp);
				m_models[tmp] = model;
				data_block = BLOCK_NONE;
			}

			//Start a new section
			else if(scmd == "Pullup")
				data_block = BLOCK_PULLUP;
			else if(scmd == "Pulldown")
				data_block = BLOCK_PULLDOWN;
			else if(scmd == "GND_clamp")
				data_block = BLOCK_GND_CLAMP;
			else if(scmd == "POWER_clamp")
				data_block = BLOCK_POWER_CLAMP;
			else if(scmd == "Rising Waveform")
			{
				data_block = BLOCK_RISING_WAVEFORM;
				for(int i=0; i<3; i++)
					waveform.m_curves[i].clear();
			}
			else if(scmd == "Falling Waveform")
			{
				data_block = BLOCK_FALLING_WAVEFORM;
				for(int i=0; i<3; i++)
					waveform.m_curves[i].clear();
			}
			else if(scmd == "Model Spec")
				data_block = BLOCK_MODEL_SPEC;
			else if(scmd == "Ramp")
				data_block = BLOCK_RAMP;
			else if(scmd == "Add Submodel")
				data_block = BLOCK_SUBMODEL;

			//TODO: Terminations
			else if(scmd == "R Series")
			{}

			//Ignore pin table
			else if( (scmd == "Pin") || (scmd == "Diff Pin") | (scmd == "Series Pin Mapping"))
			{
				data_block = BLOCK_NONE;
				model = NULL;
			}

			//TODO: submodels
			else if(scmd == "Submodel")
			{
				data_block = BLOCK_NONE;
				model = NULL;
			}

			//Temp/voltage range are one-liners
			else if(scmd == "Temperature Range")
			{
				sscanf(
					line,
					"[Temperature Range] %f %f %f",
					&model->m_temps[CORNER_TYP],
					&model->m_temps[CORNER_MIN],
					&model->m_temps[CORNER_MAX]);
			}
			else if(scmd == "Voltage Range")
			{
				sscanf(
					line,
					"[Voltage Range] %f %f %f",
					&model->m_voltages[CORNER_TYP],
					&model->m_voltages[CORNER_MIN],
					&model->m_voltages[CORNER_MAX]);
			}

			else
			{
				LogWarning("Unrecognized command %s\n", command);
			}

			continue;
		}

		//Alphanumeric? It's a keyword. Parse it out.
		else if(isalpha(line[0]))
		{
			sscanf(line, "%[^ =]", tmp);
			string skeyword = tmp;

			//If there's not an active model, skip it
			if(!model)
				continue;

			//Skip anything in a submodel section
			if(data_block == BLOCK_SUBMODEL)
				continue;

			//Type of buffer
			if(skeyword == "Model_type")
			{
				if(1 != sscanf(line, "Model_type %s", tmp))
					continue;

				string type(tmp);
				if(type == "I/O")
					model->m_type = IBISModel::TYPE_IO;
				else if(type == "Input")
					model->m_type = IBISModel::TYPE_INPUT;
				else if(type == "Output")
					model->m_type = IBISModel::TYPE_OUTPUT;
				else if(type == "Open_drain")
					model->m_type = IBISModel::TYPE_OPEN_DRAIN;
				else if(type == "Series")
					model->m_type = IBISModel::TYPE_SERIES;
				else if(type == "Terminator")
					model->m_type = IBISModel::TYPE_TERMINATOR;
				else
					LogWarning("Don't know what to do with Model_type %s\n", tmp);
			}

			//Input thresholds
			//The same keywords appear under the [Model] section. Ignore these and only grab the full corners
			else if(skeyword == "Vinl")
			{
				if(data_block == BLOCK_MODEL_SPEC)
				{
					sscanf(
						line,
						"Vinl %f %f %f",
						&model->m_vil[CORNER_TYP],
						&model->m_vil[CORNER_MIN],
						&model->m_vil[CORNER_MAX]);
				}
			}
			else if(skeyword == "Vinh")
			{
				if(data_block == BLOCK_MODEL_SPEC)
				{
					sscanf(
						line,
						"Vinh %f %f %f",
						&model->m_vih[CORNER_TYP],
						&model->m_vih[CORNER_MIN],
						&model->m_vih[CORNER_MAX]);
				}
			}

			//Ignore various metadata about the buffer
			else if(skeyword == "Polarity")
			{}
			else if(skeyword == "Enable")
			{}
			else if(skeyword == "Vmeas")
			{}
			else if(skeyword == "Cref")
			{}
			else if(skeyword == "Rref")
			{}
			else if(skeyword == "Vref")
			{}

			//Die capacitance
			else if(skeyword == "C_comp")
			{
				char scale[3];
				sscanf(
					line,
					"C_comp %f%cF %f%cF %f%cF",
					&model->m_dieCapacitance[CORNER_TYP],
					&scale[CORNER_TYP],
					&model->m_dieCapacitance[CORNER_MIN],
					&scale[CORNER_MIN],
					&model->m_dieCapacitance[CORNER_MAX],
					&scale[CORNER_MAX]);

				for(int i=0; i<3; i++)
				{
					if(scale[i] == 'p')
						model->m_dieCapacitance[i] *= 1e-12;
					else if(scale[i] == 'n')
						model->m_dieCapacitance[i] *= 1e-9;
					else if(scale[i] == 'u')
						model->m_dieCapacitance[i] *= 1e-6;
				}
			}

			//Fixture properties in waveforms
			else if(skeyword == "R_fixture")
				sscanf(line, "R_fixture = %f", &waveform.m_fixtureResistance);

			else if(skeyword == "V_fixture")
				sscanf(line, "V_fixture = %f", &waveform.m_fixtureVoltage);

			else if(skeyword == "V_fixture_min")
			{}
			else if(skeyword == "V_fixture_max")
			{}
			else if(skeyword == "R_load")
			{}

			//Ramp rate
			else if(skeyword == "dV/dt_r")
			{}
			else if(skeyword == "dV/dt_f")
			{}

			//Something else we havent seen before
			else
			{
				LogWarning("Unrecognized keyword %s\n", tmp);
			}
		}

		//If we get here, it's a data table.
		else
		{
			//If not in a data block, do nothing
			if(data_block == BLOCK_NONE)
				continue;

			//Crack individual numbers
			char sindex[32] = {0};
			char styp[32];
			char smin[32];
			char smax[32];
			if(4 != sscanf(line, " %31[^ ] %31[^ ] %31[^ ] %31[^ \n]", sindex, styp, smin, smax))
				continue;

			//Parse the numbers
			float index = ParseNumber(sindex);
			float vtyp = ParseNumber(styp);
			float vmin = ParseNumber(smin);
			float vmax = ParseNumber(smax);

			switch(data_block)
			{
				//Curves
				case BLOCK_PULLDOWN:
					model->m_pulldown[CORNER_TYP].m_curve.push_back(IVPoint(index, vtyp));
					model->m_pulldown[CORNER_MIN].m_curve.push_back(IVPoint(index, vmin));
					model->m_pulldown[CORNER_MAX].m_curve.push_back(IVPoint(index, vmax));
					break;

				case BLOCK_PULLUP:
					model->m_pullup[CORNER_TYP].m_curve.push_back(IVPoint(index, vtyp));
					model->m_pullup[CORNER_MIN].m_curve.push_back(IVPoint(index, vmin));
					model->m_pullup[CORNER_MAX].m_curve.push_back(IVPoint(index, vmax));
					break;

				case BLOCK_RISING_WAVEFORM:
				case BLOCK_FALLING_WAVEFORM:
					waveform.m_curves[CORNER_TYP].push_back(VTPoint(index, vtyp));
					waveform.m_curves[CORNER_MIN].push_back(VTPoint(index, vmin));
					waveform.m_curves[CORNER_MAX].push_back(VTPoint(index, vmax));
					break;

				//Ignore other curves for now
				default:
					break;
			}
		}
	}

	fclose(fp);
	return true;
}

float IBISParser::ParseNumber(const char* str)
{
	//Pull out the digits
	string digits;
	char scale = ' ';
	for(size_t i=0; i<32; i++)
	{
		char c = str[i];

		if( (c == '-') || (c == '.') || (isdigit(c)))
			digits += c;

		else if(isspace(c))
			continue;

		else if(c == '\0')
			break;

		else
		{
			scale = c;
			break;
		}
	}

	float ret;
	sscanf(digits.c_str(), "%f", &ret);

	switch(scale)
	{
		case 'm':
			return ret * 1e-3;

		case 'u':
			return ret * 1e-6;

		case 'n':
			return ret * 1e-9;

		case 'p':
			return ret * 1e-12;

		default:
			return ret;
	}
}
