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
#include "EyeHeightMeasurementDecoder.h"
#include "EyeDecoder2.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeHeightMeasurementDecoder::EyeHeightMeasurementDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	m_xAxisUnit = Unit(Unit::UNIT_PS);
	m_yAxisUnit = Unit(Unit::UNIT_VOLTS);

	//Set up channels
	m_signalNames.push_back("Eye");
	m_channels.push_back(NULL);

	m_startname = "Begin Time";
	m_parameters[m_startname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_startname].SetFloatVal(0);

	m_endname = "End Time";
	m_parameters[m_endname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_endname].SetFloatVal(0);

	m_posname = "Midpoint Voltage";
	m_parameters[m_posname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_posname].SetFloatVal(0);

	m_min = 0;
	m_max = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EyeHeightMeasurementDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_EYE) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void EyeHeightMeasurementDecoder::SetDefaultName()
{
	float vstart = m_parameters[m_startname].GetFloatVal();
	float vend = m_parameters[m_endname].GetFloatVal();

	char hwname[256];
	snprintf(hwname, sizeof(hwname), "EyeHeight(%s, %.2f, %.2f)",
		m_channels[0]->m_displayname.c_str(),
		vstart,
		vend);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string EyeHeightMeasurementDecoder::GetProtocolName()
{
	return "Eye Height";
}

bool EyeHeightMeasurementDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool EyeHeightMeasurementDecoder::NeedsConfig()
{
	//need manual config
	return true;
}

double EyeHeightMeasurementDecoder::GetVoltageRange()
{
	return m_max - m_min;
}

double EyeHeightMeasurementDecoder::GetOffset()
{
	return - (m_min + m_max)/2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EyeHeightMeasurementDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
		return;
	auto din = dynamic_cast<EyeWaveform*>(m_channels[0]->GetData());
	if(din == NULL)
		return;

	//Create the output
	auto cap = new AnalogWaveform;

	//Make sure times are in the right order, and convert from seconds to picoseconds
	float tstart = m_parameters[m_startname].GetFloatVal() * 1e12;
	float tend = m_parameters[m_endname].GetFloatVal() * 1e12;
	if(tstart > tend)
	{
		float tmp = tstart;
		tstart = tend;
		tend = tmp;
	}

	//Convert times to bins
	size_t width_bins = din->GetWidth();
	float width_ps = din->m_uiWidth * 2;
	float ps_per_bin = width_ps / width_bins;

	//Find start/end time bins
	size_t start_bin = round((tstart + din->m_uiWidth) / ps_per_bin);
	size_t end_bin = round((tend + din->m_uiWidth) / ps_per_bin);
	start_bin = min(start_bin, din->GetWidth());
	end_bin = min(end_bin, din->GetWidth());

	//Approximate center of the eye opening
	float vrange = m_channels[0]->GetVoltageRange();
	size_t height = din->GetHeight();
	float volts_per_row = vrange / height;
	float volts_at_bottom = din->GetCenterVoltage() - vrange/2;
	float vmid = m_parameters[m_posname].GetFloatVal();
	size_t mid_bin = round( (vmid - volts_at_bottom) / volts_per_row);
	mid_bin = min(mid_bin, din->GetHeight()-1);

	m_min = FLT_MAX;
	m_max = 0;

	float* data = din->GetData();
	int64_t w = din->GetWidth();
	float ber_max = FLT_EPSILON;
	for(size_t x = start_bin; x <= end_bin; x ++)
	{
		//Search up and down from the midpoint to find the edges of the eye opening
		size_t top_bin = mid_bin;
		for(; top_bin < height; top_bin ++)
		{
			if(data[top_bin*w + x] > ber_max)
				break;
		}

		size_t bot_bin = mid_bin;
		for(; bot_bin > 0; bot_bin --)
		{
			if(data[bot_bin*w + x] > ber_max)
				break;
		}

		//Convert from eye bins to volts
		size_t height_bins = top_bin - bot_bin;
		float height_volts = volts_per_row * height_bins;

		//Output waveform generation
		cap->m_offsets.push_back(round( (x*ps_per_bin) - din->m_uiWidth ));
		cap->m_durations.push_back(round(ps_per_bin));
		cap->m_samples.push_back(height_volts);
		m_min = min(height_volts, m_min);
		m_max = max(height_volts, m_max);
	}

	//Add some margin to the graph
	m_min -= 0.025;
	m_max += 0.025;

	SetData(cap);

	//Copy start time etc from the input. Timestamps are in picoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
