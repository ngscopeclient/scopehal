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

	m_peak_threshold = "Peak Threshold";
	m_parameters[m_peak_threshold] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_peak_threshold].SetFloatVal(0.0f);
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

	int64_t len = (int64_t) din->size();

	auto uniform = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sparse = dynamic_cast<SparseAnalogWaveform*>(din);

	float min_voltage = GetMinVoltage(sparse, uniform);

	//Vector to store indices of peaks
	vector<int64_t> peak_indices;

	float peak_threshold = m_parameters[m_peak_threshold].GetFloatVal();

	// Get peaks
	FindPeaks(sparse, uniform, peak_threshold, peak_indices);

	// Get the number of peaks
	size_t num_of_peaks = peak_indices.size();

	int64_t sum_half_widths = 0;

	//Set up the output waveform for Full Width at Half Maximum
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->PrepareForCpuAccess();

	//Set up the output waveform for Amplitude of peaks
	auto cap1 = SetupEmptySparseAnalogOutputWaveform(din, 1, true);
	cap1->PrepareForCpuAccess();

	//Vector to store normalized version of input waveform
	vector<float> din_norm;
	din_norm.resize(len);

	if (uniform)
	{
		// Normalize the input signal to have all positive values
		for(int64_t i = 0; i < len; i++)
			din_norm[i] = uniform->m_samples[i] - min_voltage;

		// Calculate and store the full width at half maximum and amplitude for all peaks
		for(size_t i = 0; i < num_of_peaks; i++)
		{
			int64_t offset;
			int64_t width = 0;
			int64_t index = peak_indices[i];
			float half_max = din_norm[index] / 2;

			// Calculate the distance from the peak to its half maximum on x-axis in forward direction
			for(offset = index; (din_norm[offset] > half_max) && (offset < len); offset++)
			{
				width++;
			}

			// Calculate the distance from the peak to its half maximum on x-axis in backward direction
			for(offset = index; (din_norm[offset] > half_max) && (offset >= 0); offset--)
			{
				width++;
			}

			int64_t fwhm = width * din->m_timescale;

			// Push FWHM information
			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(width);
			cap->m_samples.push_back((float)fwhm);

			// Push amplitude information
			cap1->m_offsets.push_back(offset);
			cap1->m_durations.push_back(width);
			cap1->m_samples.push_back(uniform->m_samples[index]);

			sum_half_widths += fwhm;
		}
	}
	else if (sparse)
	{
		// Normalize the input signal to have all positive values
		for(int64_t i = 0; i < len; i++)
			din_norm[i] = sparse->m_samples[i] - min_voltage;

		// Calculate and store the full width at half maximum and amplitude for all peaks
		for(size_t i = 0; i < num_of_peaks; i++)
		{
			int64_t offset1, offset2;
			int64_t index = peak_indices[i];
			float half_max = din_norm[index] / 2;

			// Calculate the distance from the peak to its half maximum on x-axis in forward direction
			for(offset2 = index; (din_norm[offset2] > half_max) && (offset2 < len); offset2++);

			// Calculate the distance from the peak to its half maximum on x-axis in backward direction
			for(offset1 = index; (din_norm[offset1] > half_max) && (offset1 >= 0); offset1--);

			int64_t fwhm = (sparse->m_offsets[offset2] - sparse->m_offsets[offset1]) * din->m_timescale;
			int64_t offset = sparse->m_offsets[offset1];

			// Push FWHM information
			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(fwhm);
			cap->m_samples.push_back((float)fwhm);

			// Push amplitude information
			cap1->m_offsets.push_back(offset);
			cap1->m_durations.push_back(fwhm);
			cap1->m_samples.push_back(uniform->m_samples[index]);

			sum_half_widths += fwhm;
		}
	}

	SetData(cap, 0);
	SetData(cap1, 1);

	cap->MarkModifiedFromCpu();
	cap1->MarkModifiedFromCpu();

	if (num_of_peaks > 0)
		m_streams[2].m_value = (float) sum_half_widths / num_of_peaks;
}
