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
#include "WindowFilter.h"

using namespace std;

size_t GetIndexNearestAfterTimestamp(WaveformBase* wfm, int64_t time_fs);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WindowFilter::WindowFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_startTimeName("Start Time")
	, m_durationName("Duration")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_parameters[m_startTimeName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_startTimeName].SetIntVal(0);

	m_parameters[m_durationName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_durationName].SetFloatVal(FS_PER_SECOND / 10);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool WindowFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetXAxisUnits() == Unit(Unit::UNIT_FS)) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string WindowFilter::GetProtocolName()
{
	return "Window";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

template<class T>
void DoCopy(T* w_in, T* w_out, size_t start_sample, size_t end_sample)
{
	w_out->Resize(end_sample - start_sample);

	memcpy(__builtin_assume_aligned(&w_out->m_samples[0], 16),
		(uint8_t*)__builtin_assume_aligned(&w_in->m_samples[0], 16) + (start_sample*sizeof(w_in->m_samples[0])),
		(end_sample - start_sample) * sizeof(w_in->m_samples[0]));

	w_out->m_triggerPhase = GetOffsetScaled(w_in, start_sample);

	w_out->MarkModifiedFromCpu();
}

void WindowFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto in = GetInputWaveform(0);

	int64_t start_time = m_parameters[m_startTimeName].GetIntVal();
	int64_t end_time = start_time + m_parameters[m_durationName].GetIntVal();

	bool dontcare;
	size_t start_sample = GetIndexNearestAtOrBeforeTimestamp(in, start_time, dontcare);
	size_t end_sample = GetIndexNearestAtOrBeforeTimestamp(in, end_time, dontcare);

	if (start_sample >= in->size())
		start_sample = in->size() - 1;

	if (end_sample >= in->size())
		end_sample = in->size() - 1;

	if(auto uaw = dynamic_cast<UniformAnalogWaveform*>(in))
	{
		m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG; // TODO: I think this races with WaveformArea::MapAllBuffers
		DoCopy(uaw, SetupEmptyUniformAnalogOutputWaveform(uaw, 0), start_sample, end_sample);
	}
	else if (auto saw = dynamic_cast<SparseAnalogWaveform*>(in))
	{
		m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG; // TODO: I think this races with WaveformArea::MapAllBuffers
		DoCopy(saw, SetupSparseOutputWaveform(saw, 0, start_sample, saw->size() - end_sample), start_sample, end_sample);
	}
	else if(auto udw = dynamic_cast<UniformDigitalWaveform*>(in))
	{
		m_streams[0].m_stype = Stream::STREAM_TYPE_DIGITAL; // TODO: I think this races with WaveformArea::MapAllBuffers
		DoCopy(udw, SetupEmptyUniformDigitalOutputWaveform(udw, 0), start_sample, end_sample);
	}
	else if (auto sdw = dynamic_cast<SparseDigitalWaveform*>(in))
	{
		m_streams[0].m_stype = Stream::STREAM_TYPE_DIGITAL; // TODO: I think this races with WaveformArea::MapAllBuffers
		DoCopy(sdw, SetupSparseDigitalOutputWaveform(sdw, 0, start_sample, sdw->size() - end_sample), start_sample, end_sample);
	}
	else
	{
		LogError("Unknown waveform type in WindowFilter");
		return;
	}
}
