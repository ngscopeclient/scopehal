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
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}
	din->PrepareForCpuAccess();
	auto len = din->size() - 1;

	auto sparse = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uniform = dynamic_cast<UniformAnalogWaveform*>(din);

	float min_voltage = GetMinVoltage(sparse, uniform);

	if(sparse)
	{
		SetData(nullptr, 0);
		return;
	}
	else
	{
		//Set up the output waveform
		auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
		cap->m_timescale = 1;
		cap->PrepareForCpuAccess();

		auto cap1 = SetupEmptySparseAnalogOutputWaveform(din, 1, true);
		cap1->m_timescale = 1;
		cap1->PrepareForCpuAccess();

		auto din_copy = new UniformAnalogWaveform;
		din_copy->m_startTimestamp 		= din->m_startTimestamp;
		din_copy->m_startFemtoseconds	= din->m_startFemtoseconds;
		din_copy->m_triggerPhase		= din->m_triggerPhase;
		din_copy->m_timescale			= din->m_timescale;
		din_copy->Resize(len+1);

		auto diff = new UniformAnalogWaveform;
		diff->m_startTimestamp 		= din->m_startTimestamp;
		diff->m_startFemtoseconds	= din->m_startFemtoseconds;
		diff->m_triggerPhase		= din->m_triggerPhase;
		diff->m_timescale			= din->m_timescale;
		diff->Resize(len);

        float* fdin = (float*)__builtin_assume_aligned(din_copy->m_samples.GetCpuPointer(), 16);
		float* fin = (float*)__builtin_assume_aligned(uniform->m_samples.GetCpuPointer(), 16);
		float* fdiff = (float*)__builtin_assume_aligned(diff->m_samples.GetCpuPointer(), 16);

		for(size_t i=0; i<len; i++)
			fdin[i] = fin[i] - min_voltage;

		for(size_t i=1; i<len; i++)
			fdiff[i-1] = fdin[i] - fdin[i-1];

		vector<int64_t> edges;

		auto digital_diff = new UniformDigitalWaveform;
		digital_diff->m_startTimestamp 		= din->m_startTimestamp;
		digital_diff->m_startFemtoseconds	= din->m_startFemtoseconds;
		digital_diff->m_triggerPhase		= din->m_triggerPhase;
		digital_diff->m_timescale			= din->m_timescale;
		digital_diff->Resize(len);

		bool cur = diff->m_samples[0] > 0.0f;

		for(size_t i=0; i<len; i++)
		{
			float f = diff->m_samples[i];
			if(cur && (f < 0.0f))
				cur = false;
			else if(!cur && (f > 0.0f))
				cur = true;
			digital_diff->m_samples[i] = cur;
		}

		FindFallingEdges(NULL, digital_diff, edges);

		size_t num_of_zc = edges.size();

		for (size_t i=0; i<num_of_zc; i++)
		{
			int64_t width = 0;
			int64_t index = edges[i]/din->m_timescale;
			float max = fdin[index];

			size_t j = 0;
			
			for(j=index; fdin[j]>(max/2); j++)
			{
				width++;
			}

			for(j=index; fdin[j]>(max/2); j--)
			{
				width++;
			}

			cap->m_offsets.push_back(j*din->m_timescale);
			cap->m_durations.push_back(width*din->m_timescale);
			cap->m_samples.push_back(width*din->m_timescale);

			cap1->m_offsets.push_back(j*din->m_timescale);
			cap1->m_durations.push_back(width*din->m_timescale);
			cap1->m_samples.push_back(fin[index]);
		}

		SetData(cap, 0);
		SetData(cap1, 1);

		cap->MarkModifiedFromCpu();
		cap1->MarkModifiedFromCpu();
	}
}
