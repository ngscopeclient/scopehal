/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "BandwidthMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BandwidthMeasurement::BandwidthMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_reference("Reference Level")
{
	AddStream(Unit(Unit::UNIT_HZ), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);
	CreateInput("din");

	m_parameters[m_reference] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_parameters[m_reference].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool BandwidthMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) && (stream.GetYAxisUnits() == Unit(Unit::UNIT_DB)) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string BandwidthMeasurement::GetProtocolName()
{
	return "Bandwidth";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void BandwidthMeasurement::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	ClearErrors();

	//Set up input
	auto in = GetInput(0).GetData();
	auto uin = dynamic_cast<UniformAnalogWaveform*>(in);
	auto sin = dynamic_cast<SparseAnalogWaveform*>(in);
	if(!uin && !sin)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}
	size_t len = in->size();
	PrepareForCpuAccess(sin, uin);

	int64_t bw = 0;
	float threshold = m_parameters[m_reference].GetFloatVal() - 3;
	for(size_t i=0; i < len; i++)
	{
		auto cur = GetValue(sin, uin, i);

		//Update live each iteration
		//This way, if BW is off the end of the measurement we will report the upper bound
		bw = GetOffsetScaled(sin, uin, i);

		//TODO: interpolate
		if(cur < threshold)
			break;
	}

	m_streams[0].m_value = bw;
}
