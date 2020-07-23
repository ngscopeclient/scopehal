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
	@brief Implementation of TouchstoneParser
 */
#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SParameterVector

SParameterPoint SParameterVector::InterpolatePoint(float frequency)
{
	//Binary search to find the points straddling us
	size_t len = m_points.size();
	size_t pos = len/2;
	size_t last_lo = 0;
	size_t last_hi = len - 1;

	//If out of range, clip
	if(frequency < m_points[0].m_frequency)
		return m_points[0];
	else if(frequency > m_points[len-1].m_frequency)
		return m_points[len-1];
	else
	{
		while(true)
		{
			SParameterPoint pivot = m_points[pos];

			//Dead on? Stop
			if( (last_hi - last_lo) <= 1)
				break;

			//Too high, move down
			if(pivot.m_frequency > frequency)
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
	float freq_lo = m_points[last_lo].m_frequency;
	float freq_hi = m_points[last_hi].m_frequency;
	float dfreq = freq_hi - freq_lo;
	float frac = (frequency - freq_lo) / dfreq;

	//Interpolate amplitude
	SParameterPoint ret;
	float amp_lo = m_points[last_lo].m_amplitude;
	float amp_hi = m_points[last_hi].m_amplitude;
	ret.m_amplitude = amp_lo + (amp_hi - amp_lo)*frac;

	//TODO: figure out phase. For now leave it at zero (i.e. don't de-embed phase, just do scalar amplitude correction)
	ret.m_phase = 0;

	ret.m_frequency = frequency;
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TouchstoneParser

TouchstoneParser::TouchstoneParser()
{
}

TouchstoneParser::~TouchstoneParser()
{
	Clear();
}

/**
	@brief Clears out current S-parameters before reloading them
 */
void TouchstoneParser::Clear()
{
	for(auto it : m_params)
		delete it.second;
	m_params.clear();
}

/**
	@brief Loads the file
 */
bool TouchstoneParser::Load(string fname)
{
	Clear();

	//Allocate new arrays to hold the S-parameters.
	//For now, assume full 2 port.
	for(int d=1; d <= 2; d++)
	{
		for(int s=1; s <= 2; s++)
			m_params[SPair(d, s)] = new SParameterVector;
	}

	//If file doesn't exist, bail early
	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("Unable to open S-parameter file %s\n", fname.c_str());
		return false;
	}

	//Read line by line.
	char line[256];
	while(!feof(fp))
	{
		fgets(line, sizeof(line), fp);

		//Comments start with a !
		if(line[0] == '!')
			continue;

		//Header line with metadata starts with a #
		if(line[0] == '#')
		{
			//Format: # [freq unit] S [MA|DB] R [impedance]
			char freq_unit[32];
			char volt_unit[32];
			int impedance;
			if(3 != sscanf(line, "# %31s S %31s R %d", freq_unit, volt_unit, &impedance))
			{
				LogError("Failed to parse S2P header line \"%s\"\n", line);
				return false;
			}

			//Figure out units
			if(0 != strcmp(freq_unit, "MHZ"))
			{
				LogError("S2P frequency units other than MHZ not yet supported (got %s)\n", freq_unit);
				return false;
			}
			if(0 != strcmp(volt_unit, "MA"))
			{
				LogError("S2P voltage units other than MA not yet supported (got %s)\n", volt_unit);
				return false;
			}

			continue;
		}

		//Each S2P line is formatted as freq s11 s21 s12 s22
		float mhz, s11m, s11p, s21m, s21p, s12m, s12p, s22m, s22p;
		if(9 != sscanf(line, "%f %f %f %f %f %f %f %f %f", &mhz, &s11m, &s11p, &s21m, &s21p, &s12m, &s12p, &s22m, &s22p))
		{
			LogError("Malformed S2P line \"%s\"", line);
			return false;
		}

		//Save everything
		float hz = mhz * 1e6;
		m_params[SPair(1,1)]->m_points.push_back(SParameterPoint(hz, s11m, s11p));
		m_params[SPair(2,1)]->m_points.push_back(SParameterPoint(hz, s21m, s21p));
		m_params[SPair(1,2)]->m_points.push_back(SParameterPoint(hz, s12m, s12p));
		m_params[SPair(2,2)]->m_points.push_back(SParameterPoint(hz, s22m, s22p));
	}

	//Clean up
	fclose(fp);

	LogDebug("Loaded %zu S-parameter points\n", m_params[SPair(2,1)]->m_points.size());

	return true;
}
