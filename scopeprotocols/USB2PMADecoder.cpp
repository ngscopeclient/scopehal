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
#include "USB2PMADecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

USB2PMADecoder::USB2PMADecoder(const string& color)
	: Filter(color, CAT_SERIAL)
{
	AddProtocolStream("data");
	CreateInput("D+");
	CreateInput("D-");

	m_speedname = "Speed";

	m_parameters[m_speedname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_speedname].AddEnumValue("Low (1.5 Mbps)", SPEED_LOW);
	m_parameters[m_speedname].AddEnumValue("Full (12 Mbps)", SPEED_FULL);
	m_parameters[m_speedname].AddEnumValue("High (480 Mbps)", SPEED_HIGH);
	m_parameters[m_speedname].SetIntVal(SPEED_FULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool USB2PMADecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string USB2PMADecoder::GetProtocolName()
{
	return "USB 1.x/2.0 PMA";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void USB2PMADecoder::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din_p = GetInputWaveform(0);
	auto din_n = GetInputWaveform(1);
	din_p->PrepareForCpuAccess();
	din_n->PrepareForCpuAccess();
	size_t len = min(din_p->size(), din_n->size());

	auto sdin_p = dynamic_cast<SparseAnalogWaveform*>(din_p);
	auto sdin_n = dynamic_cast<SparseAnalogWaveform*>(din_n);
	auto udin_p = dynamic_cast<UniformAnalogWaveform*>(din_p);
	auto udin_n = dynamic_cast<UniformAnalogWaveform*>(din_n);

	//Figure out our speed so we know what's going on
	auto speed = static_cast<Speed>(m_parameters[m_speedname].GetIntVal());

	//Set appropriate thresholds for different speeds
	auto threshold_diff = (speed == SPEED_HIGH) ? 0.15 : 0.2;
	auto threshold_se   = 0.8;
	int64_t transition_time = 0;
	switch(speed)
	{
	case SPEED_HIGH:
		// 1 UI width
		transition_time = 2083000;
		break;
	case SPEED_FULL:
		// TFST = 14ns (Section 7.1.4.1)
		transition_time = 14000000;
		break;
	case SPEED_LOW:
		// TLST = 210ns (Section 7.1.4.1)
		transition_time = 210000000;
		break;
	}

	//Figure out the line state for each input (no clock recovery yet)
	auto cap = new USB2PMAWaveform;
	cap->PrepareForCpuAccess();
	for(size_t i=0; i<len; i++)
	{
		auto vp = GetValue(sdin_p, udin_p, i);
		auto vn = GetValue(sdin_n, udin_n, i);
		bool bp = (vp > threshold_se);
		bool bn = (vn > threshold_se);
		float vdiff = vp - vn;

		USB2PMASymbol::SegmentType type = USB2PMASymbol::TYPE_SE1;
		if(fabs(vdiff) > threshold_diff)
		{
			if( (speed == SPEED_FULL) || (speed == SPEED_HIGH) )
			{
				if(vdiff > 0)
					type = USB2PMASymbol::TYPE_J;
				else
					type = USB2PMASymbol::TYPE_K;
			}
			else
			{
				if(vdiff > 0)
					type = USB2PMASymbol::TYPE_K;
				else
					type = USB2PMASymbol::TYPE_J;
			}
		}
		else if(bp && bn)
			type = USB2PMASymbol::TYPE_SE1;
		else
			type = USB2PMASymbol::TYPE_SE0;

		//First sample goes as-is
		if(cap->m_samples.empty())
		{
			cap->m_offsets.push_back(::GetOffset(sdin_p, udin_p, i));
			cap->m_durations.push_back(GetDuration(sdin_p, udin_p, i));
			cap->m_samples.push_back(type);
			continue;
		}

		//Type match? Extend the existing sample
		size_t iold = cap->size()-1;
		auto oldtype = cap->m_samples[iold];
		if(oldtype == type)
		{
			cap->m_durations[iold] += GetDuration(sdin_p, udin_p, i);
			continue;
		}

		//Ignore SE0/SE1 states during transitions.
		int64_t last_fs = cap->m_durations[iold] * din_p->m_timescale;
		if(
			( (oldtype == USB2PMASymbol::TYPE_SE0) || (oldtype == USB2PMASymbol::TYPE_SE1) ) &&
			(last_fs < transition_time))
		{
			cap->m_samples[iold].m_type = type;
			cap->m_durations[iold] += GetDuration(sdin_p, udin_p, i);
			continue;
		}

		//Not a match. Add a new sample.
		cap->m_offsets.push_back(::GetOffset(sdin_p, udin_p, i));
		cap->m_durations.push_back(GetDuration(sdin_p, udin_p, i));
		cap->m_samples.push_back(type);
	}

	SetData(cap, 0);

	//Copy our time scales from the input
	//Use the first trace's timestamp as our start time if they differ
	cap->m_timescale = din_p->m_timescale;
	cap->m_startTimestamp = din_p->m_startTimestamp;
	cap->m_startFemtoseconds = din_p->m_startFemtoseconds;
	cap->m_triggerPhase = din_p->m_triggerPhase;
	cap->MarkModifiedFromCpu();
}

std::string USB2PMAWaveform::GetColor(size_t i)
{
	auto sample = m_samples[i];
	switch(sample.m_type)
	{
		case USB2PMASymbol::TYPE_J:
		case USB2PMASymbol::TYPE_K:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case USB2PMASymbol::TYPE_SE0:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		//invalid state, should never happen
		case USB2PMASymbol::TYPE_SE1:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string USB2PMAWaveform::GetText(size_t i)
{
	auto sample = m_samples[i];
	switch(sample.m_type)
	{
		case USB2PMASymbol::TYPE_J:
			return "J";
		case USB2PMASymbol::TYPE_K:
			return "K";
		case USB2PMASymbol::TYPE_SE0:
			return "SE0";
		case USB2PMASymbol::TYPE_SE1:
			return "SE1";
	}

	return "";
}
