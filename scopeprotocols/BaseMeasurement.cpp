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
#include "BaseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BaseMeasurement::BaseMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_minmaxPipeline("shaders/MinMax.spv", 3, sizeof(uint32_t))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("din");

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		m_histogramPipeline =
			make_shared<ComputePipeline>("shaders/Histogram.spv", 2, sizeof(HistogramConstants));

		m_histogramBuf.SetGpuAccessHint(AcceleratorBuffer<uint64_t>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool BaseMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string BaseMeasurement::GetProtocolName()
{
	return "Base";
}

Filter::DataLocation BaseMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void BaseMeasurement::Refresh(
	vk::raii::CommandBuffer& cmdBuf,
	std::shared_ptr<QueueHandle> queue)
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

	//Copy input unit to output
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 1);

	//Make a histogram of the waveform
	float vmin;
	float vmax;
	if(sin)
		GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, sin, vmin, vmax);
	else
		GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, uin, vmin, vmax);

	//GPU side histogram calculation
	size_t nbins = 128;
	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		if(sin)
			MakeHistogram(cmdBuf, queue, *m_histogramPipeline, sin, m_histogramBuf, vmin, vmax, nbins);
		else
			MakeHistogram(cmdBuf, queue, *m_histogramPipeline, uin, m_histogramBuf, vmin, vmax, nbins);
	}

	//CPU fallback
	else
	{
		m_histogramBuf.PrepareForCpuAccess();

		auto hist = MakeHistogram(sin, uin, vmin, vmax, nbins);
		for(size_t i=0; i<nbins; i++)
			m_histogramBuf[i] = hist[i];

		m_histogramBuf.MarkModifiedFromCpu();
	}

	m_histogramBuf.PrepareForCpuAccess();

	//Find the highest peak in the first quarter of the histogram
	//This is the expected base for the entire waveform
	size_t binval = 0;
	size_t idx = 0;
	for(size_t i=0; i<(nbins/4); i++)
	{
		if(m_histogramBuf[i] > binval)
		{
			binval = m_histogramBuf[i];
			idx = i;
		}
	}
	float fbin = (idx + 0.5f)/nbins;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(in, 0, true);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	//CPU side inner loop
	if(sin)
		InnerLoop(sin, cap, len, vmin, vmax, fbin);
	else
		InnerLoop(uin, cap, len, vmin, vmax, fbin);

	//Compute average of all
	double sum = 0;
	for(auto f : cap->m_samples)
		sum += f;
	m_streams[1].m_value = sum / cap->m_samples.size();
}
