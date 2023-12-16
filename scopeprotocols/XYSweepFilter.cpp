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

#include "../scopehal/scopehal.h"
#include "XYSweepFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

XYSweepFilter::XYSweepFilter(const string& color)
	: Filter(color, CAT_MATH)
//	, m_mode("Mode")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "out", Stream::STREAM_TYPE_ANALOG);

	/*
	m_parameters[m_mode] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_mode].AddEnumValue("XYSweep", MODE_GATE);
	m_parameters[m_mode].AddEnumValue("Latch", MODE_LATCH);
	m_parameters[m_mode].SetIntVal(MODE_LATCH);
	*/

	CreateInput("x");
	CreateInput("y");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool XYSweepFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string XYSweepFilter::GetProtocolName()
{
	return "X-Y Sweep";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void XYSweepFilter::ClearSweeps()
{
	SetData(nullptr, 0);
}

void XYSweepFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, std::shared_ptr<QueueHandle> /*queue*/)
{
	//Make sure we've got valid inputs
	auto wx = GetInput(0);
	auto wy = GetInput(1);
	if(!wx || !wy)
	{
		SetData(nullptr, 0);
		return;
	}

	auto x = wx.GetScalarValue();
	auto y = wy.GetScalarValue();

	//Make an output waveform
	auto cap = dynamic_cast<SparseAnalogWaveform*>(GetData(0));
	if(!cap)
	{
		cap = new SparseAnalogWaveform;
		SetData(cap, 0);

		cap->m_timescale = 1;
		cap->m_triggerPhase = 0;
		cap->m_flags = 0;

		//initial waveform timestamp
		double t = GetTime();
		cap->m_startTimestamp = floor(t);
		cap->m_startFemtoseconds = (t - cap->m_startTimestamp) * FS_PER_SECOND;
	}
	SetData(cap, 0);

	//Copy units from input and set up config
	SetYAxisUnits(wy.GetYAxisUnits(), 0);
	cap->m_revision ++;

	//Rescale: if X axis units don't work directly
	auto xuin = wx.GetYAxisUnits();
	if(xuin == Unit::UNIT_AMPS)
	{
		SetXAxisUnits(Unit::UNIT_MICROAMPS);
		x *= 1e6;
	}
	else
		SetXAxisUnits(xuin);

	//If X axis value is greater than the previous, or if we have no samples yet, append
	cap->PrepareForCpuAccess();
	if(cap->empty() || (x > cap->m_offsets[cap->m_offsets.size()-1]) )
	{
		//Extend previous
		if(!cap->m_durations.empty())
			cap->m_durations[cap->m_durations.size()-1] = x - cap->m_offsets[cap->m_offsets.size()-1];

		cap->m_offsets.push_back(x);
		cap->m_durations.push_back(1);
		cap->m_samples.push_back(y);
	}

	//Before the first point? Insert it
	else if(x < cap->m_offsets[0])
	{
		cap->m_offsets.push_front(x);
		cap->m_durations.push_front(cap->m_offsets[1] - x);
		cap->m_samples.push_back(y);
	}

	//Mid-span
	//Find the first sample with X axis value >= our current value and overwrite it
	else
	{
		for(size_t i=1; i<cap->m_offsets.size(); i++)
		{
			if(cap->m_offsets[i] < x)
				continue;

			cap->m_offsets[i] = x;
			if(i == cap->m_offsets.size()-1)
				cap->m_durations[i] = 1;
			else
				cap->m_durations[i] = cap->m_offsets[i+1] - x;
			cap->m_samples[i] = y;

			//Extend previous
			cap->m_durations[i-1] = x - cap->m_offsets[i-1];
			break;
		}
	}

	cap->MarkModifiedFromCpu();
}
