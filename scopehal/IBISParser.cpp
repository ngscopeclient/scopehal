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
	return ilo + (m_curve[last_hi].m_current - ilo)*frac;
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
				data_block = BLOCK_RISING_WAVEFORM;
			else if(scmd == "Falling Waveform")
				data_block = BLOCK_FALLING_WAVEFORM;
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
					&model->m_temps[IBISModel::CORNER_TYP],
					&model->m_temps[IBISModel::CORNER_MIN],
					&model->m_temps[IBISModel::CORNER_MAX]);
			}
			else if(scmd == "Voltage Range")
			{
				sscanf(
					line,
					"[Voltage Range] %f %f %f",
					&model->m_voltages[IBISModel::CORNER_TYP],
					&model->m_voltages[IBISModel::CORNER_MIN],
					&model->m_voltages[IBISModel::CORNER_MAX]);
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
						&model->m_vil[IBISModel::CORNER_TYP],
						&model->m_vil[IBISModel::CORNER_MIN],
						&model->m_vil[IBISModel::CORNER_MAX]);
				}
			}
			else if(skeyword == "Vinh")
			{
				if(data_block == BLOCK_MODEL_SPEC)
				{
					sscanf(
						line,
						"Vinh %f %f %f",
						&model->m_vih[IBISModel::CORNER_TYP],
						&model->m_vih[IBISModel::CORNER_MIN],
						&model->m_vih[IBISModel::CORNER_MAX]);
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
			else if(skeyword == "C_comp")
			{}

			//Fixture properties in waveforms
			else if(skeyword == "R_fixture")
			{}
			else if(skeyword == "V_fixture")
			{}
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

		//If we get here, it's a data value.
		else
		{
			switch(data_block)
			{
				//I/V curves for pullup/down
				case BLOCK_PULLDOWN:
				case BLOCK_PULLUP:
					{
						char iunits[16][3];

						//Pull out the line
						float voltage;
						float current[3];
						if(7 != sscanf(line, "%f %f%15s %f%15s %f%15s",
							&voltage,
							&current[0],
							iunits[0],
							&current[1],
							iunits[1],
							&current[2],
							iunits[2]))
						{
							continue;
						}

						//Apply scaling factors as needed
						for(int j=0; j<3; j++)
						{
							if(iunits[j][0] == 'm')
								current[j] *= 1e-3;
							else if(iunits[j][0] == 'n')
								current[j] *= 1e-6;
							else if(iunits[j][0] == 'p')
								current[j] *= 1e-9;
						}

						//Save
						if(data_block == BLOCK_PULLDOWN)
						{
							model->m_pulldown[IBISModel::CORNER_TYP].m_curve.push_back(IVPoint(voltage, current[0]));
							model->m_pulldown[IBISModel::CORNER_MIN].m_curve.push_back(IVPoint(voltage, current[1]));
							model->m_pulldown[IBISModel::CORNER_MAX].m_curve.push_back(IVPoint(voltage, current[2]));
						}
						else if(data_block == BLOCK_PULLUP)
						{
							model->m_pullup[IBISModel::CORNER_TYP].m_curve.push_back(IVPoint(voltage, current[0]));
							model->m_pullup[IBISModel::CORNER_MIN].m_curve.push_back(IVPoint(voltage, current[1]));
							model->m_pullup[IBISModel::CORNER_MAX].m_curve.push_back(IVPoint(voltage, current[2]));
						}
					}
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
