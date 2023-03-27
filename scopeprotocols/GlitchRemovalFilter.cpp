/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "../scopehal/scopehal.h"
#include "GlitchRemovalFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

GlitchRemovalFilter::GlitchRemovalFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddDigitalStream("data");
	// AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG, Stream::STREAM_DO_NOT_INTERPOLATE);

	CreateInput("Input");

	m_minwidthname = "Minimum Width";
	m_parameters[m_minwidthname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_FS));
	m_parameters[m_minwidthname].SetIntVal(1000000000.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool GlitchRemovalFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	LogDebug("ValidateChannel false\n");
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string GlitchRemovalFilter::GetProtocolName()
{
	return "Glitch Removal";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

template<class T>
void DoGlitchRemoval(T* din, SparseDigitalWaveform* cap, size_t minwidth)
{
	size_t len = din->m_samples.size();
	cap->Resize(len);

	cap->PrepareForCpuAccess();
	din->PrepareForCpuAccess();

	size_t k = 0;
	bool last_sample = !din->m_samples[0];
	size_t running_length = 0;

	for (size_t i = 0; i < len; i++)
	{
		bool this_sample = din->m_samples[i];
		size_t this_offset;
		size_t this_duration;

		if constexpr (std::is_same<T, UniformDigitalWaveform>::value)
		{
			this_offset = i;
			this_duration = 1;
		}
		else
		{
			this_offset = din->m_offsets[i];
			this_duration = din->m_durations[i];
		}

		if (this_sample != last_sample)
		{
			bool coalesce = false;
			if (k != 0)
			{
				if (last_sample == cap->m_samples[k-1])
				{
					// Don't create a new sample, just extend the last one
					coalesce = true;
				}
			}

			if (running_length >= minwidth && !coalesce)
			{
				// Install pulse
				cap->m_offsets[k] = this_offset - running_length;
				cap->m_samples[k] = last_sample;
				cap->m_durations[k] = running_length;
				k++;
			}
			else
			{
				if (k != 0)
				{
					// Extend last pulse
					cap->m_durations[k-1] += running_length;
				}
				else
				{
					// At the beginning and no long-enough pulses yet
				}
			}

			running_length = 0;
		}

		running_length += this_duration;

		last_sample = this_sample;
	}

	if (k != 0)
	{
		cap->m_durations[k - 1] += running_length; // Extend last to end
	}

	cap->Resize(k);
	cap->m_offsets.shrink_to_fit();
	cap->m_durations.shrink_to_fit();
	cap->m_samples.shrink_to_fit();
}

void GlitchRemovalFilter::Refresh()
{
	//Get the input data
	auto udin = dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(0));
	auto sdin = dynamic_cast<SparseDigitalWaveform*>(GetInputWaveform(0));
	if (!udin && !sdin)
	{
		SetData(NULL, 0);
		return;
	}
	
	//Set up output waveform and get configuration
	auto cap = SetupEmptySparseDigitalOutputWaveform(GetInputWaveform(0), 0);

	size_t minwidth = floor(m_parameters[m_minwidthname].GetFloatVal() / cap->m_timescale);

	if (sdin)
		DoGlitchRemoval(sdin, cap, minwidth);
	else
		DoGlitchRemoval(udin, cap, minwidth);

	cap->MarkModifiedFromCpu();
}
