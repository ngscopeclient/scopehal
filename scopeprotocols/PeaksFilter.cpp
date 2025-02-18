/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
#include "PeaksFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PeaksFilter::PeaksFilter(const string& color)
	: PeakDetectionFilter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "waveform", Stream::STREAM_TYPE_ANALOG);

	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PeaksFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i >= 1)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PeaksFilter::GetProtocolName()
{
	return "Peaks";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PeaksFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Output units track the input
	if(GetInput(0))
	{
		SetXAxisUnits(GetInput(0).GetXAxisUnits());
		SetYAxisUnits(GetInput(0).GetYAxisUnits(), 0);

		m_parameters[m_peakwindowname].SetUnit(GetInput(0).GetXAxisUnits());
	}

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	if(!din)
		return;

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	if(sdin)
	{
		FindPeaks(sdin, cmdBuf, queue);

		auto cap = SetupSparseOutputWaveform(sdin, 0, 0, 0);
		cap->m_offsets.CopyFrom(sdin->m_offsets);
		cap->m_durations.CopyFrom(sdin->m_durations);
		cap->m_samples.CopyFrom(sdin->m_samples);
	}
	else if(udin)
	{
		FindPeaks(udin, cmdBuf, queue);

		auto cap = SetupEmptyUniformAnalogOutputWaveform(udin, 0);
		cap->m_samples.CopyFrom(udin->m_samples);
	}

	//TODO: FWHM output or something
}
