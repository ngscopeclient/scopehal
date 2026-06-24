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
#include "../scopehal/KahanSummation.h"
#include "PhaseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PhaseMeasurement::PhaseMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_freqMode(m_parameters["Frequency Mode"])
	, m_freq(m_parameters["Center Frequency"])
	, m_minmaxPipeline("shaders/MinMax.spv", 3, sizeof(uint32_t))
{
	AddStream(Unit(Unit::UNIT_DEGREES), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_DEGREES), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	//Set up channels
	CreateInput<InputConstraintStreamType>("din", Stream::STREAM_TYPE_ANALOG);

	m_freq = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_freq.SetIntVal(100e6);

	m_freqMode = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_freqMode.AddEnumValue("Auto", MODE_AUTO);
	m_freqMode.AddEnumValue("Manual", MODE_MANUAL);
	m_freqMode.SetIntVal(MODE_AUTO);

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		m_histogramPipeline =
			make_shared<ComputePipeline>("shaders/Histogram.spv", 2, sizeof(HistogramConstants));

		m_histogramBuf.SetGpuAccessHint(AcceleratorBuffer<uint64_t>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PhaseMeasurement::GetProtocolName()
{
	return "Phase";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PhaseMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("PhaseMeasurement::Refresh");
	#endif
	ClearErrors();

	if(!VerifyAllInputsOK())
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		SetData(nullptr, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	//Find midpoint
	float vmin;
	float vmax;
	Filter::GetBaseAndTopVoltage(
		cmdBuf,
		queue,
		m_minmaxPipeline,
		m_histogramPipeline,
		m_minbuf,
		m_maxbuf,
		m_histogramBuf,
		sdin,
		udin,
		vmin,
		vmax);

	//Find edges
	float vavg = (vmax + vmin) / 2;
	if(sdin)
		m_detector.FindZeroCrossings(sdin, vavg, cmdBuf, queue);
	else
		m_detector.FindZeroCrossings(udin, vavg, cmdBuf, queue);
	auto& edges = m_detector.GetResults();
	edges.PrepareForCpuAccess();
	size_t edgelen = edges.size();

	//Auto: use median of interval between pairs of rising edges
	int64_t period = 0;
	if(m_freqMode.GetIntVal() == MODE_AUTO)
	{
		if(edgelen < 2)
		{
			AddErrorMessage("No edges", "Need at least two level crossings in the input");
			SetData(nullptr, 0);
			return;
		}
		vector<int64_t> durations;
		for(size_t i=0; i<edgelen-2; i++)
			durations.push_back(edges[i+2] - edges[i]);
		std::sort(durations.begin(), durations.end());
		period = durations[durations.size()/2];
	}

	//Manual: use user-selected frequency
	else
		period = round(FS_PER_SECOND / m_freq.GetIntVal());

	//Create the output
	size_t outlen = edgelen/2;
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->PrepareForCpuAccess();
	cap->m_timescale = 1;
	cap->m_triggerPhase = 1;
	cap->Resize(outlen);

	//Main measurement loop, update once per cycle at the rising edge
	//This isn't quite as nice as the original implementation measuring instantaneous phase within a single cycle,
	//but is MUCH more robust in the presence of amplitude noise or variation (e.g. pulse shaping as seen in PSK31)
	KahanSummation sumI;
	KahanSummation sumQ;
	for(size_t i=0; i<outlen; i++)
	{
		//Calculate normalized phase of the LO
		int64_t tnow = edges[i*2];
		float theta = static_cast<float>(tnow % period) / period;
		theta = (theta - 0.5) * 2 * M_PI;
		float finalPhase = -(360 * theta / M_PI);

		if(finalPhase < -180)
			finalPhase += 360;
		if(finalPhase > 180)
			finalPhase -= 360;

		cap->m_offsets[i] = tnow;
		cap->m_durations[i] = 1;
		cap->m_samples[i] = finalPhase;

		//convert to I/Q and sum
		float finalRad = finalPhase * M_PI / 180;
		sumI += sin(finalRad);
		sumQ += cos(finalRad);

		//Resize last sample
		if(i > 0)
			cap->m_durations[i-1] = tnow - cap->m_offsets[i-1];
	}

	//Compute final I/Q vector sum and convert back
	float finalI = sumI.GetSum() / outlen;
	float finalQ = sumQ.GetSum() / outlen;
	float theta = atan2(finalI, finalQ);
	m_streams[1].m_value = 180 * theta / M_PI;

	cap->MarkModifiedFromCpu();
}
