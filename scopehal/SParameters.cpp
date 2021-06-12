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
	@brief Implementation of SParameters
 */
#include "scopehal.h"
#include <math.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SParameterVector

SParameterPoint SParameterVector::InterpolatePoint(float frequency) const
{
	//Binary search to find the points straddling us
	size_t len = m_points.size();
	size_t pos = len/2;
	size_t last_lo = 0;
	size_t last_hi = len - 1;

	//If out of range, clip
	if(frequency < m_points[0].m_frequency)
		return SParameterPoint(frequency, m_points[0].m_amplitude, 0);
	else if(frequency > m_points[len-1].m_frequency)
		return SParameterPoint(frequency, 0, 0);
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
	float frac;
	if(dfreq > FLT_EPSILON)
		frac = (frequency - freq_lo) / dfreq;
	else
		frac = 0;

	//Interpolate amplitude
	SParameterPoint ret;
	float amp_lo = m_points[last_lo].m_amplitude;
	float amp_hi = m_points[last_hi].m_amplitude;
	ret.m_amplitude = amp_lo + (amp_hi - amp_lo)*frac;

	//Interpolate phase (angles in radians)
	float phase_lo = m_points[last_lo].m_phase;
	float phase_hi = m_points[last_hi].m_phase;

	//If both values have the same sign, no wrapping needed.
	//If values have opposite signs, but are smallish, we cross at 0 vs +/- pi, so also no wrapping needed.
	if(
		( (phase_hi > 0) && (phase_lo > 0) ) ||
		( (phase_hi < 0) && (phase_lo < 0) ) ||
		( (fabs(phase_hi) < M_PI_4) && (fabs(phase_lo) < M_PI_4) )
	  )
	{
		ret.m_phase = phase_lo + (phase_hi - phase_lo)*frac;
	}

	//Wrapping needed.
	//Shift everything by pi, then interpolate normally, then shift back.
	else
	{
		//Shift the negative phase by a full circle
		if(phase_lo < 0)
			phase_lo += 2*M_PI;
		if(phase_hi < 0)
			phase_hi += 2*M_PI;

		//Normal interpolation
		ret.m_phase = phase_lo + (phase_hi - phase_lo)*frac;

		//If we went out of range, rescale
		if(ret.m_phase > 2*M_PI)
			ret.m_phase -= 2*M_PI;
	}


	ret.m_frequency = frequency;
	return ret;
}

/**
	@brief Multiplies this vector by another set of S-parameters.

	Sampling points are kept unchanged, and incident points are interpolated as necessary.
 */
SParameterVector& SParameterVector::operator *=(const SParameterVector& rhs)
{
	size_t len = m_points.size();
	for(size_t i=0; i<len; i++)
	{
		auto& us = m_points[i];
		auto point = rhs.InterpolatePoint(us.m_frequency);

		//Phases add mod +/- pi
		us.m_phase += point.m_phase;
		if(us.m_phase < -M_PI)
			us.m_phase += 2*M_PI;
		if(us.m_phase > M_PI)
			us.m_phase -= 2*M_PI;

		//Amplitudes get multiplied
		us.m_amplitude *= point.m_amplitude;
	}

	return *this;
}

/**
	@brief Gets the group delay at a given bin
 */
float SParameterVector::GetGroupDelay(size_t bin)
{
	if(bin+1 >= m_points.size())
		return 0;

	auto a = m_points[bin];
	auto b = m_points[bin+1+1];

	//frequency is in Hz, not rad/sec, so we need to convert
	float dfreq = (b.m_frequency - a.m_frequency) * 2*M_PI;

	return (a.m_phase - b.m_phase) / dfreq;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SParameters

SParameters::SParameters()
{
}

SParameters::~SParameters()
{
	Clear();
}

/**
	@brief Clears out current S-parameters before reloading them
 */
void SParameters::Clear()
{
	for(auto it : m_params)
		delete it.second;
	m_params.clear();
}

void SParameters::Allocate()
{
	//Allocate new arrays to hold the S-parameters.
	//For now, assume full 2 port.
	for(int d=1; d <= 2; d++)
	{
		for(int s=1; s <= 2; s++)
			m_params[SPair(d, s)] = new SParameterVector;
	}
}

/**
	@brief Applies a second set of S-parameters after this one
 */
SParameters& SParameters::operator *=(const SParameters& rhs)
{
	//Make sure we have parameters to work with
	if(rhs.empty())
	{
	}

	//If we have no parameters, just copy whatever is there
	else if(m_params.empty())
	{
		Allocate();

		for(int d=1; d <= 2; d++)
		{
			for(int s=1; s <= 2; s++)
				*m_params[SPair(d, s)] = *rhs.m_params.find(SPair(d,s))->second;
		}
	}

	//If we have parameters, append the new ones
	else
	{
		for(int d=1; d <= 2; d++)
		{
			for(int s=1; s <= 2; s++)
				*m_params[SPair(d, s)] *= *rhs.m_params.find(SPair(d,s))->second;
		}
	}

	return *this;
}

/**
	@brief Serializes a S-parameter model to a Touchstone file

	For now, assumes full 2 port
 */
void SParameters::SaveToFile(const string& path)
{
	FILE* fp = fopen(path.c_str(), "w");
	if(!fp)
	{
		LogError("Couldn't open %s for writing\n", path.c_str());
		return;
	}

	//File header
	fprintf(fp, "# GHz S MA R 50.000");

	//Get the parameters
	auto& s11 = (*this)[SPair(1, 1)];
	auto& s12 = (*this)[SPair(1, 2)];
	auto& s21 = (*this)[SPair(2, 1)];
	auto& s22 = (*this)[SPair(2, 2)];

	for(size_t i=0; i<s11.size(); i++)
	{
		float freq = s11[i].m_frequency;
		fprintf(fp, "%f %f %f %f %f %f %f %f %f\n", freq * 1e-9,
			s11[i].m_amplitude, s11[i].m_phase, s21[i].m_amplitude, s21[i].m_phase,
			s12[i].m_amplitude, s12[i].m_phase, s22[i].m_amplitude, s22[i].m_phase);
	}

	fclose(fp);
}
