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
	@brief Implementation of ImportFilter
	@ingroup core
 */

#include "../scopehal/scopehal.h"
#include "ImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Base class constructor

	@param color	Display color for the filter
	@param xunit	Default X axis unit
 */
ImportFilter::ImportFilter(const string& color, Unit xunit)
	: Filter(color, CAT_GENERATION, xunit)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ImportFilter::ValidateChannel(
	[[maybe_unused]] size_t i,
	[[maybe_unused]] StreamDescriptor stream)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void ImportFilter::SetDefaultName()
{
	auto fname = m_parameters[m_fpname].ToString();

	char hwname[256];
	snprintf(hwname, sizeof(hwname), "%s", BaseName(fname).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

bool ImportFilter::NeedsConfig()
{
	return true;
}

void ImportFilter::Refresh()
{
	//everything happens in OnFileNameChanged
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Import helpers

/**
	@brief Cleans up timebase of data that might be regularly or irregularly sampled.

	This function identifies data sampled at regular intervals and adjusts the timescale and sample duration/offset
	values accordingly, to enable dense packed optimizations and proper display of instrument timebase settings on
	imported waveforms.

	This function doesn't actually generate a uniform waveform, the caller has to take care of that.

	@param wfm	The waveform to attempt normalization on
 */
bool ImportFilter::TryNormalizeTimebase(SparseWaveformBase* wfm)
{
	//Find the min, max, and mean sample interval
	Unit xunit(GetXAxisUnits());
	uint64_t interval_sum = 0;
	uint64_t interval_count = wfm->size();
	uint64_t interval_min = std::numeric_limits<uint64_t>::max();
	uint64_t interval_max = std::numeric_limits<uint64_t>::min();
	for(size_t i=0; i<interval_count; i++)
	{
		uint64_t dur = wfm->m_durations[i];
		if(dur == 0)
			continue;

		interval_sum += dur;
		interval_min = min(interval_min, dur);
		interval_max = max(interval_max, dur);
	}
	uint64_t avg = interval_sum / interval_count;
	LogTrace("Min sample interval:     %s\n", xunit.PrettyPrint(interval_min).c_str());
	LogTrace("Average sample interval: %s\n", xunit.PrettyPrint(avg).c_str());
	LogTrace("Max sample interval:     %s\n", xunit.PrettyPrint(interval_max).c_str());
	if(avg == 0)
		return false;

	//Find the standard deviation of sample intervals
	uint64_t stdev_sum = 0;
	for(size_t i=0; i<interval_count; i++)
	{
		int64_t delta = (wfm->m_durations[i] - avg);
		stdev_sum += delta*delta;
	}
	uint64_t stdev = sqrt(stdev_sum / interval_count);
	LogTrace("Stdev of intervals:      %s\n", xunit.PrettyPrint(stdev).c_str());

	//If the standard deviation is more than 2% of the average sample period, assume the data is sampled irregularly.
	if( (stdev * 50) > avg)
	{
		LogTrace("Deviation is too large, assuming non-uniform sample interval\n");
		return false;
	}

	//If there's a significant delta between min and max, it's nonuniform
	if(interval_max*2 > 3*interval_min)
	{
		LogTrace("Delta between min and max is too large, assuming non-uniform sample interval\n");
		return false;
	}

	//If we get here, assume uniform sampling.
	//Use time zero as the trigger phase.
	wfm->m_timescale = avg;
	wfm->m_triggerPhase = wfm->m_offsets[0];
	size_t len = wfm->m_offsets.size();
	for(size_t j=0; j<len; j++)
	{
		wfm->m_offsets[j] = j;
		wfm->m_durations[j] = 1;
	}
	return true;
}
