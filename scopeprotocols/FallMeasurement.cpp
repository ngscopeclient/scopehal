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
#include "FallMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FallMeasurement::FallMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_start(m_parameters["Start Fraction"])
	, m_end(m_parameters["End Fraction"])
	, m_minmaxPipeline("shaders/MinMax.spv", 3, sizeof(uint32_t))
{
	//Set up channels
	CreateInput("din");
	AddStream(Unit(Unit::UNIT_FS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_FS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	m_start = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_start.SetFloatVal(0.8);

	m_end = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_end.SetFloatVal(0.2);

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		m_histogramPipeline =
			make_shared<ComputePipeline>("shaders/Histogram.spv", 2, sizeof(HistogramConstants));

		m_histogramBuf.SetGpuAccessHint(AcceleratorBuffer<uint64_t>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FallMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string FallMeasurement::GetProtocolName()
{
	return "Fall";
}

Filter::DataLocation FallMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FallMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("FallMeasurement::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearErrors();
	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		m_streams[1].m_value = NAN;
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	size_t len = din->size();

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;

	//Get the base/top (we use these for calculating percentages)
	double a = GetTime();
	float base;
	float top;
	GetBaseAndTopVoltage(
		cmdBuf,
		queue,
		m_minmaxPipeline,
		m_histogramPipeline,
		m_minbuf,
		m_maxbuf,
		m_histogramBuf,
		sdin,
		udin,
		base,
		top);
	double b = GetTime();
	double da = b-a;
	LogDebug("base = %f, top = %f, took %.3f ms\n", base, top, da*1000);

	//TODO: GPU path
	if(false)
	{
	}

	else
	{
		din->PrepareForCpuAccess();
		cap->PrepareForCpuAccess();

		//Find the actual levels we use for our time gate
		float delta = top - base;
		float vstart = base + m_start.GetFloatVal()*delta;
		float vend = base + m_end.GetFloatVal()*delta;

		float last = -1e20;
		int64_t tedge = 0;

		int state = 0;
		int64_t tlast = 0;

		//LogDebug("vstart = %.3f, vend = %.3f\n", vstart, vend);
		KahanSummation sum;
		int64_t num = 0;

		//Sparse path
		if(sdin)
		{
			for(size_t i=0; i < len; i++)
			{
				float cur = sdin->m_samples[i];
				int64_t tnow = sdin->m_offsets[i] * din->m_timescale;

				//Find start of edge
				if(state == 0)
				{
					if( (cur < vstart) && (last >= vstart) )
					{
						int64_t xdelta = InterpolateTime(sdin, i-1, vstart) * din->m_timescale;
						tedge = tnow - din->m_timescale + xdelta;
						state = 1;
					}
				}

				//Find end of edge
				else if(state == 1)
				{
					if( (cur < vend) && (last >= vend) )
					{
						int64_t xdelta = InterpolateTime(sdin, i-1, vend) * din->m_timescale;
						int64_t dt = xdelta + tnow - din->m_timescale - tedge;

						cap->m_offsets.push_back(tlast);
						cap->m_durations.push_back(tnow - tlast);
						cap->m_samples.push_back(dt);
						tlast = tnow;

						sum += dt;
						num ++;

						state = 0;
					}
				}

				last = cur;
			}
		}

		//Uniform path
		else
		{
			for(size_t i=0; i < len; i++)
			{
				float cur = udin->m_samples[i];
				int64_t tnow = i * din->m_timescale;

				//Find start of edge
				if(state == 0)
				{
					if( (cur < vstart) && (last >= vstart) )
					{
						int64_t xdelta = InterpolateTime(udin, i-1, vstart) * din->m_timescale;
						tedge = tnow - din->m_timescale + xdelta;
						state = 1;
					}
				}

				//Find end of edge
				else if(state == 1)
				{
					if( (cur < vend) && (last >= vend) )
					{
						int64_t xdelta = InterpolateTime(udin, i-1, vend) * din->m_timescale;
						int64_t dt = xdelta + tnow - din->m_timescale - tedge;

						cap->m_offsets.push_back(tlast);
						cap->m_durations.push_back(tnow - tlast);
						cap->m_samples.push_back(dt);
						tlast = tnow;

						sum += dt;
						num ++;

						state = 0;
					}
				}

				last = cur;
			}
		}

		cap->MarkModifiedFromCpu();
			m_streams[1].m_value = sum.GetSum() / num;
	}
}
