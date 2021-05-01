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
	@brief Implementation of TouchstoneParser
 */
#include "scopehal.h"
#include <math.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TouchstoneParser

TouchstoneParser::TouchstoneParser()
{
}

TouchstoneParser::~TouchstoneParser()
{

}

/**
	@brief Reads a SxP file
 */
bool TouchstoneParser::Load(string fname, SParameters& params)
{
	params.Clear();

	//If file doesn't exist, bail early
	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("Unable to open S-parameter file %s\n", fname.c_str());
		return false;
	}

	params.Allocate();

	//Read line by line.
	char line[256];
	double unit_scale = 1;
	bool mag_is_db = false;
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
			string funit(freq_unit);
			if( (funit == "MHZ") ||  (funit == "MHz") )
				unit_scale = 1e6;
			else if(funit == "GHZ")
				unit_scale = 1e9;
			else if(funit == "KHZ")
				unit_scale = 1e3;
			else if(funit == "HZ")
				unit_scale = 1;
			else
			{
				LogError("Unrecognized S2P frequency unit (got %s)\n", freq_unit);
				return false;
			}
			if(0 == strcmp(volt_unit, "MA"))
			{
				//magnitude, no action required
			}
			else if( (0 == strcmp(volt_unit, "DB")) || (0 == strcmp(volt_unit, "dB")) )
				mag_is_db = true;
			else
			{
				LogError("S2P units other than magnitude and dB not supported (got %s)\n", volt_unit);
				return false;
			}

			continue;
		}

		//Each S2P line is formatted as freq s11 s21 s12 s22
		float hz, s11m, s11p, s21m, s21p, s12m, s12p, s22m, s22p;
		if(9 != sscanf(line, "%f %f %f %f %f %f %f %f %f", &hz, &s11m, &s11p, &s21m, &s21p, &s12m, &s12p, &s22m, &s22p))
		{
			LogError("Malformed S2P line \"%s\"", line);
			return false;
		}

		//Convert magnitudes if needed
		if(mag_is_db)
		{
			s11m = pow(10, s11m/20);
			s12m = pow(10, s12m/20);
			s21m = pow(10, s21m/20);
			s22m = pow(10, s22m/20);
		}

		//Rescale frequency
		hz *= unit_scale;

		//Convert angles from degrees to radians
		s11p *= (M_PI / 180);
		s21p *= (M_PI / 180);
		s12p *= (M_PI / 180);
		s22p *= (M_PI / 180);

		//Save everything
		params.m_params[SPair(1,1)]->m_points.push_back(SParameterPoint(hz, s11m, s11p));
		params.m_params[SPair(2,1)]->m_points.push_back(SParameterPoint(hz, s21m, s21p));
		params.m_params[SPair(1,2)]->m_points.push_back(SParameterPoint(hz, s12m, s12p));
		params.m_params[SPair(2,2)]->m_points.push_back(SParameterPoint(hz, s22m, s22p));
	}

	//Clean up
	fclose(fp);

	LogTrace("Loaded %zu S-parameter points\n", params.m_params[SPair(2,1)]->m_points.size());

	return true;
}
