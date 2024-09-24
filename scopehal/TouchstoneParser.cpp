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
	@brief Implementation of TouchstoneParser
	@ingroup core
 */
#include "scopehal.h"
#include <math.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TouchstoneParser

/**
	@brief Default constructor, does nothing
 */
TouchstoneParser::TouchstoneParser()
{
}

/**
	@brief Default destructor, does nothing
 */
TouchstoneParser::~TouchstoneParser()
{

}

/**
	@brief Reads a SxP file

	@param		fname	Path to the .s*p file
	@param[out] params	The parsed S-parameters (if successful)

	@return	True on success, false on failure
 */
bool TouchstoneParser::Load(string fname, SParameters& params)
{
	params.Clear();

	//If file doesn't exist, bail early
	//Open in binary mode because we ignore \r characters for files with Windows line endings,
	//but we need files with Unix line endings to open correctly on Windows even if no \r is present.
	FILE* fp = fopen(fname.c_str(), "rb");
	if(!fp)
	{
		LogError("Unable to open S-parameter file %s\n", fname.c_str());
		return false;
	}

	//Figure out port count from the file name
	size_t nports = 0;
	auto extoff = fname.rfind(".s");
	if(extoff != string::npos)
		nports = atoi(&fname[extoff] + 2);
	if(nports <= 0)
	{
		LogError("Unable to determine port count for S-parameter file %s\n", fname.c_str());
		fclose(fp);
		return false;
	}
	params.Allocate(nports);

	//Read entire file into working buffer
	fseek(fp, 0, SEEK_END);
	size_t len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char* buf = new char[len];
	if(len != fread(buf, 1, len, fp))
	{
		delete[] buf;
		fclose(fp);
		return false;
	}
	fclose(fp);

	//Main parsing loop
	size_t i = 0;
	bool ok = true;
	double unit_scale = 1;
	bool mag_is_db = false;
	bool polar = true;			//mag/angle
	while(i < len)
	{
		//Discard whitespace
		if(isspace(buf[i]))
			i++;

		//! is a comment, ignore everything until the next newline
		else if(buf[i] == '!')
		{
			while( (i < len) && (buf[i] != '\n') )
				i++;
		}

		//# is the option line
		else if(buf[i] == '#')
		{
			//Format: # [freq unit] S [MA|DB|RI] R [impedance]
			char freq_unit[32];
			char volt_unit[32];
			int impedance;
			if(3 != sscanf(buf+i, "# %31s S %31s R %d", freq_unit, volt_unit, &impedance))
			{
				LogError("Failed to parse Touchstone header line \"%s\"\n", buf+i);
				ok = false;
				break;
			}

			//Figure out units
			string funit(freq_unit);
			if( (funit == "MHZ") ||  (funit == "MHz") )
				unit_scale = 1e6;
			else if( (funit == "GHZ") || (funit == "GHz") )
				unit_scale = 1e9;
			else if( (funit == "KHZ") || (funit == "kHz") )
				unit_scale = 1e3;
			else if( (funit == "HZ") || (funit == "Hz") )
				unit_scale = 1;
			else
			{
				LogError("Unrecognized Touchstone frequency unit (got %s)\n", freq_unit);
				ok = false;
				break;
			}
			if(0 == strcmp(volt_unit, "MA"))
			{
				//magnitude, no action required
			}
			else if( (0 == strcmp(volt_unit, "DB")) || (0 == strcmp(volt_unit, "dB")) )
				mag_is_db = true;
			else if(0 == strcmp(volt_unit, "RI"))
				polar = false;
			else
			{
				LogError("Touchstone units other than magnitude, real/imaginary, and dB not supported (got %s)\n", volt_unit);
				ok = false;
				break;
			}

			//Skip ahead to the next newline
			while( (i < len) && (buf[i] != '\n') )
				i++;
		}

		//Actual network data
		else
		{
			//Read the frequency and scale appropriately
			float freq;
			if(!ReadFloat(buf, i, len, freq))
			{
				ok = false;
				break;
			}
			freq *= unit_scale;

			//The actual S-matrix is nports * nports mag/angle or real/imaginary tuples
			float mag;
			float angle;
			for(size_t outer=1; outer <= nports; outer ++)
			{
				for(size_t inner=1; inner <= nports; inner ++)
				{
					//NOTE! Parameter ordering is different for 2 vs 3+ port
					//For 2 port, we loop destination inner and source outer (S11 S21 S12 S22)
					//For 3+ port, we have source inner and destination outer (S11 S12 S13 S21 S22 S23 ...)
					//See pages 6 and 8 of Touchstone File Specification rev 1.1
					size_t src;
					size_t dest;
					if(nports <= 2)
					{
						dest = inner;
						src = outer;
					}
					else
					{
						dest = outer;
						src = inner;
					}

					//Read the inputs
					if(!ReadFloat(buf, i, len, mag) || !ReadFloat(buf, i, len, angle))
					{
						ok = false;
						break;
					}

					//Convert dB magnitudes to absolute magnitudes
					if(mag_is_db)
						mag = pow(10, mag/20);

					//Touchstone uses degrees, but we use radians internally
					if(polar)
						angle *= (M_PI / 180);

					//Convert real/imaginary format to mag/angle
					else
						ComplexToPolar(mag, angle);

					//and save the final results
					params.m_params[SPair(dest, src)]->m_points.push_back(SParameterPoint(freq, mag, angle));
				}
				if(!ok)
					break;
			}
			if(!ok)
				break;

			i++;
		}
	}


	delete[] buf;
	LogTrace("Loaded %zu S-parameter points\n", params.m_params[SPair(1,1)]->m_points.size());

	return ok;
}

/**
	@brief Reads a single ASCII float from the input buffer

	@param		 	buf		Input buffer
	@param		 	i		Index of the cursor within the buffer
	@param		 	len		Size of the buffer
	@param[out]		f		Output value

	@param True on success, false on EOF or malformed data
 */
bool TouchstoneParser::ReadFloat(const char* buf, size_t& i, size_t len, float& f)
{
	//eat spaces
	while(isspace(buf[i]) && (i < len) )
		i++;
	if(i >= len)
		return false;

	//read the value
	f = atof(buf + i);

	//eat digits
	while(!isspace(buf[i]) && (i < len) )
		i++;
	return true;

}

/**
	@brief Converts a complex number in (real, imaginary) form to (magnitude, angle)

	@param f1 Input real part, output magnitude part
	@param f2 Input imaginary part, output angle part
 */
void TouchstoneParser::ComplexToPolar(float& f1, float& f2)
{
	float real = f1;
	float imag = f2;
	f1 = sqrtf(real*real + imag*imag);
	f2 = atan2(imag, real);
}
