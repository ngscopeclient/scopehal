/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MovingAverageFilter::MovingAverageFilter(string color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	CreateInput("din");

	m_depthname = "Depth";
	m_parameters[m_depthname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_depthname].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MovingAverageFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double MovingAverageFilter::GetVoltageRange()
{
	return m_inputs[0].m_channel->GetVoltageRange();
}

double MovingAverageFilter::GetOffset()
{
	return m_inputs[0].m_channel->GetOffset();
}

string MovingAverageFilter::GetProtocolName()
{
	return "Moving average";
}

bool MovingAverageFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool MovingAverageFilter::NeedsConfig()
{
	//we need the depth to be specified, duh
	return true;
}

void MovingAverageFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "MovingAvg(%s, %s)",
		GetInputDisplayName(0).c_str(),
		m_parameters[m_depthname].ToString().c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MovingAverageFilter::Refresh()
{
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();
	size_t depth = m_parameters[m_depthname].GetIntVal();

	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits();

	//Do the average
	auto cap = new AnalogWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(din);
	#pragma omp parallel for
	for(size_t i=0; i<len; i++)
	{
		float v = 0;
		size_t navg = 0;
		for(size_t j=0; j<depth; j++)
		{
			if(j > i)
				break;

			v += din->m_samples[i-j];
			navg ++;
		}
		v /= navg;

		cap->m_samples[i] = v;
	}
	SetData(cap, 0);

	//Copy our time scales from the input
	cap->m_timescale = din->m_timescale;
}
