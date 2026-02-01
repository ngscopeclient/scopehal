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
#include "IQSquelchFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IQSquelchFilter::IQSquelchFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_threshold(m_parameters["Threshold"])
	, m_holdtime(m_parameters["Hold time"])
{
	//Set up channels
	CreateInput("I");
	CreateInput("Q");
	ClearStreams();
	AddStream(Unit(Unit::UNIT_VOLTS), "I", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "Q", Stream::STREAM_TYPE_ANALOG);

	m_threshold = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_threshold.SetFloatVal(0.01);

	m_holdtime = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_holdtime.SetIntVal(1e6);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IQSquelchFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string IQSquelchFilter::GetProtocolName()
{
	return "IQ Squelch";
}

Filter::DataLocation IQSquelchFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IQSquelchFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("IQSquelchFilter::Refresh");
	#endif

	//Make sure we've got valid inputs
	auto din_i = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	auto din_q = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(1));
	if(!din_i || !din_q)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No I signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at I input");
		else if(!din_i)
			AddErrorMessage("Invalid input", "Expected uniform analog waveform at I input");

		if(!GetInput(1))
			AddErrorMessage("Missing inputs", "No Q signal input connected");
		else if(!GetInputWaveform(1))
			AddErrorMessage("Missing inputs", "No waveform available at Q input");
		else if(!din_q)
			AddErrorMessage("Invalid input", "Expected uniform analog waveform at Q input");

		SetData(nullptr, 0);
		return;
	}

	din_i->PrepareForCpuAccess();
	din_q->PrepareForCpuAccess();

	size_t len = min(din_i->size(), din_q->size());

	auto threshold = m_threshold.GetFloatVal();
	auto holdtime_fs = m_holdtime.GetIntVal();
	size_t holdtime_samples = holdtime_fs / din_i->m_timescale;

	auto dout_i = SetupEmptyUniformAnalogOutputWaveform(din_i, 0);
	auto dout_q = SetupEmptyUniformAnalogOutputWaveform(din_q, 1);
	dout_i->Resize(len);
	dout_q->Resize(len);
	dout_i->PrepareForCpuAccess();
	dout_q->PrepareForCpuAccess();

	bool open = false;
	float tsq = threshold * threshold;
	size_t topen = 0;
	for(size_t i=0; i<len; i++)
	{
		//Find I/Q magnitude (do comparison on squared mag to avoid a ton of sqrts)
		float vi = din_i->m_samples[i];
		float vq = din_q->m_samples[i];
		float msq = vi*vi + vq*vq;

		//Signal amplitude is above threshold - open squelch immediately
		//TODO: attack time?
		if(msq > tsq)
		{
			open = true;
			topen = i;
		}

		//Signal amplitude below threshold - close squelch after hold time elapses
		else if(open && ( (i - topen) > holdtime_samples) )
			open = false;

		if(open)
		{
			dout_i->m_samples[i] = din_i->m_samples[i];
			dout_q->m_samples[i] = din_q->m_samples[i];
		}
		else
		{
			dout_i->m_samples[i] = 0.0f;
			dout_q->m_samples[i] = 0.0f;
		}
	}

	dout_i->MarkModifiedFromCpu();
	dout_q->MarkModifiedFromCpu();
}
