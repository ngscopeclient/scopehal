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
#include "EnvelopeFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EnvelopeFilter::EnvelopeFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "min", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "max", Stream::STREAM_TYPE_ANALOG);

	CreateInput("in");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EnvelopeFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EnvelopeFilter::GetProtocolName()
{
	return "Envelope";
}

void EnvelopeFilter::ClearSweeps()
{
	SetData(nullptr, 0);
	SetData(nullptr, 1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EnvelopeFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		ClearSweeps();
		return;
	}

	auto data = GetInput(0).GetData();
	auto sdata = dynamic_cast<SparseAnalogWaveform*>(data);
	auto udata = dynamic_cast<UniformAnalogWaveform*>(data);

	//TODO: detect if input signal has changed timebase etc and auto-clear
	//TODO: detect major trigger phase shifts and auto clear

	//Set up output waveforms
	if(sdata)
	{
		//TODO: handle sparse path iff points are the same (or nearly same) timestamps
		ClearSweeps();
		return;
	}

	else if(udata)
	{
		size_t len = udata->size();
		udata->PrepareForCpuAccess();

		//Make output waveforms
		auto umin = dynamic_cast<UniformAnalogWaveform*>(GetData(0));
		if(!umin)
		{
			umin = new UniformAnalogWaveform;
			umin->m_triggerPhase = data->m_triggerPhase;
			SetData(umin, 0);
		}

		auto umax = dynamic_cast<UniformAnalogWaveform*>(GetData(1));
		if(!umax)
		{
			umax = new UniformAnalogWaveform;
			umax->m_triggerPhase = data->m_triggerPhase;
			SetData(umax, 1);
		}
		size_t oldlen = min(umin->size(), umax->size());

		//Set up timestamps
		umax->m_timescale = data->m_timescale;
		umax->m_startTimestamp = data->m_startTimestamp;
		umax->m_startFemtoseconds = data->m_startFemtoseconds;
		umax->m_revision ++;

		umin->m_timescale = data->m_timescale;
		umin->m_startTimestamp = data->m_startTimestamp;
		umin->m_startFemtoseconds = data->m_startFemtoseconds;
		umin->m_revision ++;

		//Extend and truncate any extra
		umax->Resize(len);
		umin->Resize(len);

		//Process overlap of old and new waveforms
		size_t i = 0;
		for(; i<oldlen; i++)
		{
			umax->m_samples[i] = max(umax->m_samples[i], udata->m_samples[i]);
			umin->m_samples[i] = min(umin->m_samples[i], udata->m_samples[i]);
		}

		//Copy input verbatim to new offsets
		for(; i<len; i++)
		{
			umax->m_samples[i] = udata->m_samples[i];
			umin->m_samples[i] = udata->m_samples[i];
		}

		umax->MarkModifiedFromCpu();
		umin->MarkModifiedFromCpu();
	}
}
