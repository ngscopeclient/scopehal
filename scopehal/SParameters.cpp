/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

/**
	@brief Copy our state to analog mag/angle waveforms
 */
void SParameterVector::ConvertToWaveforms(SparseAnalogWaveform* wmag, SparseAnalogWaveform* wang)
{
	size_t len = m_points.size();

	//Resize outputs as needed
	wmag->Resize(len);
	wmag->PrepareForCpuAccess();
	wmag->m_triggerPhase = 0;
	wmag->m_timescale = 1;

	wang->Resize(len);
	wang->PrepareForCpuAccess();
	wang->m_triggerPhase = 0;
	wang->m_timescale = 1;

	float ascale = 180 / M_PI;

	for(size_t i=0; i<len; i++)
	{
		auto freq = m_points[i].m_frequency;

		//Magnitude
		wmag->m_offsets[i] = freq;
		wmag->m_samples[i] = 20 * log10(m_points[i].m_amplitude);

		//Angle
		wang->m_offsets[i] = freq;
		wang->m_samples[i] = m_points[i].m_phase * ascale;

		//Last sample?
		if(i+1 == len)
		{
			wang->m_durations[i] = 1;
			wmag->m_durations[i] = 1;
		}

		//No, valid duration
		else
		{
			auto dur = m_points[i+1].m_frequency - freq;

			wang->m_durations[i] = dur;
			wmag->m_durations[i] = dur;
		}
	}

	wmag->MarkSamplesModifiedFromCpu();
	wang->MarkSamplesModifiedFromCpu();

	wmag->MarkTimestampsModifiedFromCpu();
	wang->MarkTimestampsModifiedFromCpu();
}

SParameterPoint SParameterVector::InterpolatePoint(float frequency) const
{
	//Binary search to find the points straddling us
	size_t len = m_points.size();
	size_t pos = len/2;
	size_t last_lo = 0;
	size_t last_hi = len - 1;

	//If out of range, clip
	if(frequency < m_points[0].m_frequency)
	{
		//Use insertion loss of the lowest point, but interpolate phase to zero at time zero
		float phase = InterpolatePhase(0, m_points[0].m_phase, frequency / m_points[0].m_frequency);
		return SParameterPoint(frequency, m_points[0].m_amplitude, phase);
	}
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

	//Output data point is always at the exact frequency we requested, by definition
	SParameterPoint ret;
	ret.m_frequency = frequency;

	//Interpolate amplitude
	float amp_lo = m_points[last_lo].m_amplitude;
	float amp_hi = m_points[last_hi].m_amplitude;
	ret.m_amplitude = amp_lo + (amp_hi - amp_lo)*frac;

	//Interpolate phase
	ret.m_phase = InterpolatePhase(m_points[last_lo].m_phase, m_points[last_hi].m_phase, frac);

	return ret;
}

/**
	@brief Interpolates a phase angle, wrapping appropriately
 */
float SParameterVector::InterpolatePhase(float phase_lo, float phase_hi, float frac) const
{
	//Wrap so we have a well defined linear range to interpolate, with no wrapping.
	if(fabs(phase_lo - phase_hi) > M_PI)
	{
		if(phase_lo < phase_hi)
			phase_lo += 2*M_PI;
		else
			phase_hi += 2*M_PI;
	}

	//Now we can interpolate normally
	float ret = phase_lo + (phase_hi - phase_lo)*frac;

	//If we went out of range, rescale
	if(ret > 2*M_PI)
		ret -= 2*M_PI;

	return ret;
}

float SParameterVector::InterpolateMagnitude(float frequency) const
{
	return InterpolatePoint(frequency).m_amplitude;
}

float SParameterVector::InterpolateAngle(float frequency) const
{
	return InterpolatePoint(frequency).m_phase;
}

/**
	@brief Gets the group delay at a given bin
 */
float SParameterVector::GetGroupDelay(size_t bin) const
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
	: m_nports(0)
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

void SParameters::Allocate(int nports)
{
	//Allocate new arrays to hold the S-parameters.
	for(int d=1; d <= nports; d++)
	{
		for(int s=1; s <= nports; s++)
			m_params[SPair(d, s)] = new SParameterVector;
	}

	m_nports = nports;
}

/**
	@brief Serializes a S-parameter model to a Touchstone file

	For now, assumes full 2 port

	@param path		Output file name
	@param format	Output data format
	@param freqUnit	Frequency units for the generated file
 */
void SParameters::SaveToFile(const string& path, ParameterFormat format, FreqUnit freqUnit)
{
	if(m_nports != 2)
	{
		LogError("SParameters::SaveToFile() only supports 2-port for now\n");
		return;
	}

	if(format != FORMAT_MAG_ANGLE)
		LogWarning("Formats other than mag-angle not implemented yet (exporting as mag-angle)\n");

	FILE* fp = fopen(path.c_str(), "w");
	if(!fp)
	{
		LogError("Couldn't open %s for writing\n", path.c_str());
		return;
	}

	//File header
	string freqText;
	float freqScale = 1;
	switch(freqUnit)
	{
		case FREQ_HZ:
			freqText = "Hz";
			freqScale = 1;
			break;

		case FREQ_KHZ:
			freqText = "kHz";
			freqScale = 1e-3;
			break;

		case FREQ_MHZ:
			freqText = "MHz";
			freqScale = 1e-6;
			break;

		case FREQ_GHZ:
		default:
			freqText = "GHz";
			freqScale = 1e-9;
			break;
	}
	fprintf(fp, "# %s S MA R 50.000\n", freqText.c_str());

	//Get the parameters
	auto& s11 = (*this)[SPair(1, 1)];
	auto& s12 = (*this)[SPair(1, 2)];
	auto& s21 = (*this)[SPair(2, 1)];
	auto& s22 = (*this)[SPair(2, 2)];

	//Mag-angle format
	float rad2deg = 180 / M_PI;
	for(size_t i=0; i<s11.size(); i++)
	{
		float freq = s11[i].m_frequency;
		fprintf(fp, "%f %f %f %f %f %f %f %f %f\n", freq * freqScale,
			s11[i].m_amplitude, s11[i].m_phase * rad2deg,
			s21[i].m_amplitude, s21[i].m_phase * rad2deg,
			s12[i].m_amplitude, s12[i].m_phase * rad2deg,
			s22[i].m_amplitude, s22[i].m_phase * rad2deg);
	}

	fclose(fp);
}
