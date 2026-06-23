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
#include "EyePattern.h"
#include "VerticalBathtub.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VerticalBathtub::VerticalBathtub(const string& color)
	: Filter(color, CAT_ANALYSIS)
	, m_time(m_parameters["Time"])
{
	m_xAxisUnit = Unit(Unit::UNIT_MILLIVOLTS);
	AddStream(Unit(Unit::UNIT_LOG_BER), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput<InputConstraintStreamType>("din", Stream::STREAM_TYPE_EYE);

	m_time = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_time.SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string VerticalBathtub::GetProtocolName()
{
	return "Vert Bathtub";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void VerticalBathtub::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue
	)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("VerticalBathtub::Refresh");
	#endif
	ClearErrors();

	//Get the input data
	auto eye = dynamic_cast<EyeWaveform*>(GetInputWaveform(0));
	if(!eye)
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected or null");
		SetData(nullptr, 0);
		return;
	}
	eye->PrepareForCpuAccess();
	int64_t timestamp = m_time.GetIntVal();

	//Find the eye bin for this column
	double fs_per_width = 2*eye->m_uiWidth;
	double fs_per_pixel = fs_per_width / eye->GetWidth();
	size_t xbin = round( (timestamp + eye->m_uiWidth) / fs_per_pixel );

	//Sanity check we're not off the eye
	if(xbin >= eye->GetWidth())
		return;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(eye, 0);
	cap->m_timescale = eye->m_timescale;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	//Eye height config
	auto range = GetInput(0).GetVoltageRange();
	double mv_per_pixel = 1000 * range / eye->GetHeight();
	double mv_off = 1000 * (range/2 - eye->GetCenterVoltage());

	//Extract the single column we're interested in
	//TODO: support a range of times around the midpoint
	//TODO: support non-NRZ waveforms
	size_t len = eye->GetHeight();
	cap->Resize(len);
	for(size_t i=0; i<len; i++)
	{
		auto ber = log10(eye->GetBERAtPoint(xbin, i, eye->GetWidth()/2, eye->GetHeight()/2));
		if(ber < 1e-20)
			cap->m_samples[i] = -20;
		else
			cap->m_samples[i] = log10(ber);

		cap->m_offsets[i] = i*mv_per_pixel - mv_off;
		cap->m_durations[i] = mv_per_pixel;
	}

	cap->MarkModifiedFromCpu();
}
