/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "scopeprotocols.h"
#include "FullWidthHalfMax.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FullWidthHalfMax::FullWidthHalfMax(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "FWHM", Stream::STREAM_TYPE_ANALOG, Stream::STREAM_DO_NOT_INTERPOLATE);
	AddStream(Unit(Unit::UNIT_VOLTS), "Amplitude", Stream::STREAM_TYPE_ANALOG, Stream::STREAM_DO_NOT_INTERPOLATE);
	AddStream(Unit(Unit::UNIT_FS), "Average FWHM", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FullWidthHalfMax::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string FullWidthHalfMax::GetProtocolName()
{
	return "Full Width Half Max";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FullWidthHalfMax::Refresh()
{
	auto din = GetInputWaveform(0);

	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	din->PrepareForCpuAccess();

	auto len = din->size();

	auto uniform = dynamic_cast<UniformAnalogWaveform*>(din);

	float min_voltage = GetMinVoltage(NULL, uniform);

	//Set up the output waveform for Full Width at Half Maximum
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	//Set up the output waveform for Amplitude of peaks
	auto cap1 = SetupEmptySparseAnalogOutputWaveform(din, 1, true);
	cap1->m_timescale = 1;
	cap1->PrepareForCpuAccess();

	//Vector to store normalized version of input waveform
	vector<float> din_norm;
	din_norm.resize(len);

	//Vector to store first difference of input signal
	vector<float> first_diff;
	first_diff.resize(len-1);

	//Threshold first difference signal in digital format, to extract falling edges later on
	//These falling edges will correspond to peaks in the input signal
	auto thresh_diff = new UniformDigitalWaveform;
	thresh_diff->m_startTimestamp = din->m_startTimestamp;
	thresh_diff->m_startFemtoseconds = din->m_startFemtoseconds;
	thresh_diff->m_triggerPhase = din->m_triggerPhase;
	thresh_diff->m_timescale = din->m_timescale;
	thresh_diff->Resize(len - 1);

	float* fin = (float*)__builtin_assume_aligned(uniform->m_samples.GetCpuPointer(), 16);

	#pragma omp parallel
	#pragma omp single nowait
	{			
		#pragma omp task
		{
			// Normalize the input signal to have all positive values
			for(size_t i = 0; i < len; i++)
				din_norm[i] = fin[i] - min_voltage;
		}

		#pragma omp task
		{
			// Calculate the first difference of normalized input signal
			for(size_t i = 1; i < (len - 1); i++)
				first_diff[i - 1] = fin[i] - fin[i - 1];
		}
	}

	// Threshold the first difference vector to get a digital signal
	bool cur = first_diff[0] > 0.0f;

	for(size_t i = 0; i < (len - 1); i++)
	{
		float f = first_diff[i];

		if(cur && (f < 0.0f))
			cur = false;
		else if(!cur && (f > 0.0f))
			cur = true;

		thresh_diff->m_samples[i] = cur;
	}

	//Vector to store falling edges
	vector<int64_t> falling_edges;

	// Get falling edges. These falling edges will correspond to peaks in the input signal
	FindFallingEdges(NULL, thresh_diff, falling_edges);

	// Get the number of falling edges
	size_t num_of_edges = falling_edges.size();

	int64_t sum_half_widths = 0;

	// Calculate and store the full width at half maximum and amplitude for all peaks
	for(size_t i = 0; i < num_of_edges; i++)
	{
		size_t j;
		int64_t width = 0;
		int64_t index = falling_edges[i] / din->m_timescale;
		float half_max = din_norm[index] / 2;

		// Calculate the distance from the peak to its half maximum on x-axis in forward direction
		for(j = index; din_norm[j] > half_max; j++)
		{
			width++;
		}

		// Calculate the distance from the peak to its half maximum on x-axis in backward direction
		for(j = index; din_norm[j] > half_max; j--)
		{
			width++;
		}

		int64_t fwhm = width * din->m_timescale;
		int64_t offset = j * din->m_timescale;

		// Push FWHM information
		cap->m_offsets.push_back(offset);
		cap->m_durations.push_back(fwhm);
		cap->m_samples.push_back(fwhm);

		// Push amplitude information
		cap1->m_offsets.push_back(offset);
		cap1->m_durations.push_back(fwhm);
		cap1->m_samples.push_back(fin[index]);

		sum_half_widths += fwhm;
	}

	SetData(cap, 0);
	SetData(cap1, 1);

	cap->MarkModifiedFromCpu();
	cap1->MarkModifiedFromCpu();

	m_streams[2].m_value = sum_half_widths / num_of_edges;
}
