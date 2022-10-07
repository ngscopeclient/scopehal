/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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

#include "scopehal.h"
#include "Waveform.h"
#include "Filter.h"

using namespace std;

template<class T>
size_t BinarySearchForGequal(T* buf, size_t len, T value)
{
	size_t pos = len/2;
	size_t last_lo = 0;
	size_t last_hi = len-1;

	if (!len)
		return 0;

	//Clip if out of range
	if(buf[0] >= value)
		return 0;
	if(buf[last_hi] < value)
		return len-1;

	while(true)
	{
		LogIndenter li;

		//Stop if we've bracketed the target
		if( (last_hi - last_lo) <= 1)
			break;

		//Move down
		if(buf[pos] > value)
		{
			size_t delta = pos - last_lo;
			last_hi = pos;
			pos = last_lo + delta/2;
		}

		//Move up
		else
		{
			size_t delta = last_hi - pos;
			last_lo = pos;
			pos = last_hi - delta/2;
		}
	}

	return last_lo;
}

template size_t BinarySearchForGequal<int64_t>(int64_t* buf, size_t len, int64_t value);
template size_t BinarySearchForGequal<float>(float* buf, size_t len, float value);

size_t GetIndexNearestAtOrBeforeTimestamp(WaveformBase* wfm, int64_t time_fs, bool& out_of_bounds)
{
	//Make sure we have a current copy of the data
	wfm->PrepareForCpuAccess();

	if (!wfm->size())
		return 0;

	double ticks = 1.0f * (time_fs - wfm->m_triggerPhase) / wfm->m_timescale;

	//Find the approximate index of the sample of interest and interpolate the cursor position
	int64_t target = floor(ticks);

	int64_t result;

	if(auto swfm = dynamic_cast<SparseWaveformBase*>(wfm))
	{
		if(swfm->m_offsets[0] >= target)
		{
			out_of_bounds = true;
			return 0;
		}
		if(swfm->m_offsets[wfm->size() - 1] < target)
		{
			out_of_bounds = true;
			return wfm->size() - 1;
		}

		result = BinarySearchForGequal(
			swfm->m_offsets.GetCpuPointer(),
			wfm->size(),
			target);

		// Unless we found an exact match, step back one sample
		if (!(swfm->m_offsets[result] <= target))
			result--;
	}
	else
	{
		result = target;

		// Unless we found an exact match, step back one sample
		if (target != floor(ticks))
			result--;
	}

	if (result < 0)
	{
		// Possible by decrement above
		out_of_bounds = true;
		return 0;
	}
	else if (result >= (int64_t)wfm->size())
	{
		// Possible in dense case
		out_of_bounds = true;
		return wfm->size() - 1;
	}
	else
	{
		out_of_bounds = false;
		return result;
	}
}

optional<float> GetValueAtTime(WaveformBase* waveform, int64_t time_fs, bool zero_hold_behaviour)
{
	auto swaveform = dynamic_cast<SparseAnalogWaveform*>(waveform);
	auto uwaveform = dynamic_cast<UniformAnalogWaveform*>(waveform);

	if(!swaveform && !uwaveform)
		return {};

	//Find the approximate index of the sample of interest and interpolate the cursor position
	bool out_of_range;
	size_t index = GetIndexNearestAtOrBeforeTimestamp(waveform, time_fs, out_of_range);

	if(out_of_range)
		return {};

	// If waveform wants zero-hold rendering, do not interpolate cursor-displayed value
	if (zero_hold_behaviour)
	{
		if (swaveform)
		{
			if (GetOffsetScaled(swaveform, index) + GetDurationScaled(swaveform, index) < time_fs)
			{
				// Sample found with GE search does not extend to selected point
				return {};
			}
		}
		
		return GetValue(swaveform, uwaveform, index);
	}

	//In bounds, interpolate
	double ticks = 1.0f * (time_fs - waveform->m_triggerPhase) / waveform->m_timescale;

	if(swaveform)
		return Filter::InterpolateValue(swaveform, index-1, ticks - swaveform->m_offsets[index-1]);
	else
		return Filter::InterpolateValue(uwaveform, index-1, ticks - (index-1) );
}
