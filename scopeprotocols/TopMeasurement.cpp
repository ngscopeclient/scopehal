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
#include "TopMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TopMeasurement::TopMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_minmaxPipeline("shaders/MinMax.spv", 3, sizeof(uint32_t))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput<InputConstraintStreamType>("din", Stream::STREAM_TYPE_ANALOG);

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		m_histogramPipeline =
			make_shared<ComputePipeline>("shaders/Histogram.spv", 2, sizeof(HistogramConstants));

		m_histogramBuf.SetGpuAccessHint(AcceleratorBuffer<uint64_t>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TopMeasurement::GetProtocolName()
{
	return "Top";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TopMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("TopMeasurement::Refresh");
	#endif
	ClearMessages();

	if(!VerifyAllInputsOK())
	{
		AddErrorMessage("Missing input", "Input signal is null or not connected");
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	size_t len = din->size();

	//Make a histogram of the waveform
	float vmin = 0;
	float vmax = 0;

	//Make a histogram of the waveform
	size_t nbins = 64;
	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		if(sdin)
		{
			GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, sdin, vmin, vmax);
			MakeHistogram(cmdBuf, queue, *m_histogramPipeline, sdin, m_histogramBuf, vmin, vmax, nbins);
		}
		else
		{
			GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, udin, vmin, vmax);
			MakeHistogram(cmdBuf, queue, *m_histogramPipeline, udin, m_histogramBuf, vmin, vmax, nbins);
		}
	}

	//CPU fallback
	else
	{
		PrepareForCpuAccess(sdin, udin);
		m_histogramBuf.PrepareForCpuAccess();

		if(sdin)
			GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, sdin, vmin, vmax);
		else
			GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, udin, vmin, vmax);

		auto hist = MakeHistogram(sdin, udin, vmin, vmax, nbins);
		for(size_t i=0; i<nbins; i++)
			m_histogramBuf[i] = hist[i];

		m_histogramBuf.MarkModifiedFromCpu();
	}
	m_histogramBuf.PrepareForCpuAccess();

	//Set temporary midpoint and range
	float range = (vmax - vmin);
	float midpoint = range/2 + vmin;

	//Find the highest peak in the last quarter of the histogram
	//This is the peak for the entire waveform
	size_t binval = 0;
	size_t idx = 0;
	for(size_t i=(nbins*3/4); i<nbins; i++)
	{
		if(m_histogramBuf[i] > binval)
		{
			binval = m_histogramBuf[i];
			idx = i;
		}
	}
	float fbin = (idx + 0.5f)/nbins;
	float global_top = fbin*range + vmin;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	float last = vmin;
	int64_t tedge = 0;
	int64_t count = 0;
	float delta = range * 0.1;

	KahanSummation workingSum;
	for(size_t i=0; i < len; i++)
	{
		//Wait for a rising edge
		float cur = GetValue(sdin, udin, i);
		int64_t tnow = ::GetOffsetScaled(sdin, udin, i);

		if( (cur > midpoint) && (last <= midpoint) )
		{
			//Done, add the sample
			if(count != 0)
			{
				cap->m_offsets.push_back(tedge);
				cap->m_durations.push_back(tnow - tedge);
				cap->m_samples.push_back(workingSum.GetSum() / count);

				//Clear out
				workingSum.Reset();
				count = 0;
			}
			tedge = tnow;
		}

		//If the value is fairly close to the calculated top, average it
		//TODO: discard samples on the rising/falling edges as this will skew the results
		if(fabs(cur - global_top) < delta)
		{
			count ++;
			workingSum += cur;
		}

		last = cur;
	}

	cap->MarkModifiedFromCpu();

	//Compute average
	KahanSummation finalsum;
	for(auto f : cap->m_samples)
		finalsum += f;
	m_streams[1].m_value = finalsum.GetSum() / cap->m_samples.size();
}
