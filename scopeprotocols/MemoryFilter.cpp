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
#include "MemoryFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MemoryFilter::MemoryFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MemoryFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	//TODO: support digital inputs?
	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string MemoryFilter::GetProtocolName()
{
	return "Memory";
}

bool MemoryFilter::ShouldPersistWaveform()
{
	//Our waveform should be saved since it's not possible to generate from the live waveforms
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MemoryFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
		return;

	//If this is our first refresh after creation, copy the input immediately
	if(GetData(0) == nullptr)
		Update();
}

vector<string> MemoryFilter::EnumActions()
{
	vector<string> ret;
	ret.push_back("Update");
	return ret;
}

bool MemoryFilter::PerformAction(const string& id)
{
	if(id == "Update")
		Update();

	return true;
}

void MemoryFilter::Update()
{
	auto sin = GetInput(0);

	//Copy units even if no data
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetVoltageRange(sin.GetVoltageRange(), 0);
	SetOffset(sin.GetOffset(), 0);
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	auto data = sin.GetData();
	auto sdata = dynamic_cast<SparseAnalogWaveform*>(data);
	auto udata = dynamic_cast<UniformAnalogWaveform*>(data);

	if(sdata)
	{
		auto cap = SetupSparseOutputWaveform(sdata, 0, 0, 0);
		cap->m_offsets.CopyFrom(sdata->m_offsets);
		cap->m_durations.CopyFrom(sdata->m_durations);
		cap->m_samples.CopyFrom(sdata->m_samples);
	}

	else if(udata)
	{
		auto cap = SetupEmptyUniformAnalogOutputWaveform(udata, 0);
		cap->m_samples.CopyFrom(udata->m_samples);
	}

	//TODO: digital path
	else
		SetData(nullptr, 0);
}
