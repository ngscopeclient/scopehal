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
#include "GroupDelayFilter.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

GroupDelayFilter::GroupDelayFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	CreateInput("Phase");

	m_min = FLT_MAX;
	m_max = -FLT_MAX;
	m_range = 1;
	m_offset = 0;

	m_xAxisUnit = Unit(Unit::UNIT_HZ);
	SetYAxisUnits(Unit(Unit::UNIT_FS), 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool GroupDelayFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;
	if(stream.m_channel->GetType() != OscilloscopeChannel::CHANNEL_TYPE_ANALOG)
		return false;
	if(stream.m_channel->GetXAxisUnits().GetType() != Unit::UNIT_HZ)
		return false;
	if(i == 0)
		return (stream.GetYAxisUnits().GetType() == Unit::UNIT_DEGREES);

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string GroupDelayFilter::GetProtocolName()
{
	return "Group Delay";
}

bool GroupDelayFilter::NeedsConfig()
{
	return false;
}

void GroupDelayFilter::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

float GroupDelayFilter::GetOffset(size_t /*stream*/)
{
	return m_offset;
}

float GroupDelayFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

void GroupDelayFilter::SetVoltageRange(float range, size_t /*stream*/)
{
	m_range = range;
}

void GroupDelayFilter::SetOffset(float offset, size_t /*stream*/)
{
	m_offset = offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void GroupDelayFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto ang = GetAnalogInputWaveform(0);

	//We need meaningful data
	size_t len = ang->m_samples.size();
	if(len == 0)
	{
		SetData(NULL, 0);
		return;
	}
	else
		len --;

	//Create the output and copy timestamps
	auto cap = SetupOutputWaveform(ang, 0, 1, 0);
	int64_t* vfreq = (int64_t*)&ang->m_offsets[0];
	float* vang = (float*)&ang->m_samples[0];

	//Main output loop
	float vmin = FLT_MAX;
	float vmax = -FLT_MAX;
	for(size_t i=0; i<len; i++)
	{
		//Subtract phase angles, wrapping correctly around singularities
		//(assume +/- 180 deg range)
		float phase_hi = vang[i+1];
		float phase_lo = vang[i];
		if(fabs(phase_lo - phase_hi) > 180)
		{
			if(phase_lo < phase_hi)
				phase_lo += 360;
			else
				phase_hi += 360;
		}
		float dphase = phase_hi - phase_lo;

		//convert frequency to degrees/sec since input channel angles are in degrees
		float dfreq = (vfreq[i+1] - vfreq[i]) * ang->m_timescale;
		dfreq *= 360;

		//Calculate final group delay
		float delay = (-dphase / dfreq) * FS_PER_SECOND;
		cap->m_samples[i] = delay;

		vmin = min(delay, vmin);
		vmax = max(delay, vmax);
	}

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );
}
