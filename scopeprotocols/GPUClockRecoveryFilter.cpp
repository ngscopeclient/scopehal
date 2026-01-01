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
#include "GPUClockRecoveryFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

GPUClockRecoveryFilter::GPUClockRecoveryFilter(const string& color)
	: Filter(color, CAT_CLOCK)
	, m_computePipeline("shaders/GPUClockRecoveryFilter.spv", 3, sizeof(GPUClockRecoveryFilterConstants))
{
	AddDigitalStream("data");
	CreateInput("IN");

	//worry about gating support later
	//CreateInput("Gate");

	m_baudname = "Symbol rate";
	m_parameters[m_baudname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_baudname].SetFloatVal(1250000000);	//1.25 Gbps

	m_threshname = "Threshold";
	m_parameters[m_threshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_threshname].SetFloatVal(0);
}

GPUClockRecoveryFilter::~GPUClockRecoveryFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool GPUClockRecoveryFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	switch(i)
	{
		case 0:
			if(stream.m_channel == nullptr)
				return false;
			return
				(stream.GetType() == Stream::STREAM_TYPE_ANALOG) ||
				(stream.GetType() == Stream::STREAM_TYPE_DIGITAL);

		case 1:
			if(stream.m_channel == nullptr)	//null is legal for gate
				return true;

			return (stream.GetType() == Stream::STREAM_TYPE_DIGITAL);

		default:
			return false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string GPUClockRecoveryFilter::GetProtocolName()
{
	return "Clock Recovery (GPU)";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void GPUClockRecoveryFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Require a data signal, but not necessarily a gate
	if(!VerifyInputOK(0))
	{
		SetData(nullptr, 0);
		return;
	}

	//Require a uniform analog input for now (TODO: fall back to CPU path for anything else? or moar shaders?)
	auto din = GetInputWaveform(0);
	auto uadin = dynamic_cast<UniformAnalogWaveform*>(din);
	if(!uadin)
	{
		SetData(nullptr, 0);
		return;
	}

	/*
		First pass: One thread per chunk. Chunks minimum 1000 UIs long

		Start: loop over samples until we find the first level crossing
			Set phase to that crossing
			Set frequency to starting freq

		Main loop
			Search for edges within the current UI
			If not, run NCO open loop
			If we find an edge, calculate the phase error etc
	 */

	/*
	auto sadin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uddin = dynamic_cast<UniformDigitalWaveform*>(din);
	auto sddin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto gate = GetInputWaveform(1);
	auto sgate = dynamic_cast<SparseDigitalWaveform*>(gate);
	auto ugate = dynamic_cast<UniformDigitalWaveform*>(gate);
	if(gate)
		gate->PrepareForCpuAccess();

	//Timestamps of the edges
	size_t nedges = 0;
	AcceleratorBuffer<int64_t> vedges;
	float threshold = m_parameters[m_threshname].GetFloatVal();
	if(uadin)
		nedges = m_detector.FindZeroCrossings(uadin, threshold, cmdBuf, queue);
	else
	{
		din->PrepareForCpuAccess();

		vector<int64_t> edges;
		if(sadin)
			FindZeroCrossings(sadin, m_parameters[m_threshname].GetFloatVal(), edges);
		else if(uddin)
			FindZeroCrossings(uddin, edges);
		else if(sddin)
			FindZeroCrossings(sddin, edges);
		nedges = edges.size();

		//Inefficient but this is a less frwuently used code path
		vedges.resize(nedges);
		vedges.PrepareForCpuAccess();
		memcpy(vedges.GetCpuPointer(), &edges[0], nedges*sizeof(int64_t));
	}
	if(nedges == 0)
	{
		SetData(nullptr, 0);
		return;
	}

	//Edge array
	auto& edges = uadin ? m_detector.GetResults() : vedges;
	edges.PrepareForCpuAccess();

	//Get nominal period used for the first cycle of the NCO
	int64_t initialPeriod = round(FS_PER_SECOND / m_parameters[m_baudname].GetFloatVal());
	int64_t halfPeriod = initialPeriod / 2;

	//Disallow frequencies higher than Nyquist of the input
	int64_t fnyquist = 2*din->m_timescale;
	if( initialPeriod < fnyquist)
	{
		SetData(nullptr, 0);
		return;
	}

	//Create the output waveform and copy our timescales
	auto cap = SetupEmptySparseDigitalOutputWaveform(din, 0);
	cap->m_triggerPhase = 0;
	cap->m_timescale = 1;		//recovered clock time scale is single femtoseconds
	cap->PrepareForCpuAccess();

	int64_t tend;
	if(sadin || uadin)
		tend = GetOffsetScaled(sadin, uadin, din->size()-1);
	else
		tend = GetOffsetScaled(sddin, uddin, din->size()-1);

	#ifdef PLL_DEBUG_OUTPUTS
	auto debugPeriod = SetupEmptySparseAnalogOutputWaveform(cap, 1);
	auto debugPhase = SetupEmptySparseAnalogOutputWaveform(cap, 2);
	auto debugFreq = SetupEmptySparseAnalogOutputWaveform(cap, 3);
	auto debugDrift = SetupEmptySparseAnalogOutputWaveform(cap, 4);
	#endif

	//The actual PLL NCO
	//TODO: use the real fibre channel PLL.
	cap->m_offsets.reserve(edges.size());
	if(gate)
		InnerLoopWithGating(*cap, edges, nedges, tend, initialPeriod, halfPeriod, fnyquist, gate, sgate, ugate);
	else
		InnerLoopWithNoGating(*cap, edges, nedges, tend, initialPeriod, halfPeriod, fnyquist);

	//Generate the squarewave and duration values to match the calculated timestamps
	//TODO: GPU this?
	//Important to FillDurations() after FillSquarewave() since FillDurations() expects to use sample size
	#ifdef __x86_64__
	if(g_hasAvx2)
	{
		FillSquarewaveAVX2(*cap);
		FillDurationsAVX2(*cap);
	}
	else
	#endif
	{
		FillSquarewaveGeneric(*cap);
		FillDurationsGeneric(*cap);
	}

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
	*/

	/*
	//Set units as early as possible so we can spawn in the same plot as our parent signal when creating a filter
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG;

	float scale = GetInput(iScalar).GetScalarValue();
	auto din = GetInputWaveform(iVector);
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}
	din->PrepareForCpuAccess();
	auto len = din->size();

	auto sparse = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uniform = dynamic_cast<UniformAnalogWaveform*>(din);

	if(sparse)
	{
		//Set up the output waveform
		auto cap = SetupSparseOutputWaveform(sparse, 0, 0, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		//subtract is slightly more complex than adding because we have to keep the order right
		float* fin = (float*)__builtin_assume_aligned(sparse->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		if(iScalar == 1)
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = fin[i] - scale;
		}
		else
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = scale - fin[i];
		}

		cap->MarkModifiedFromCpu();
	}
	else
	{
		//Set up the output waveform
		auto cap = SetupEmptyUniformAnalogOutputWaveform(uniform, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		float* fin = (float*)__builtin_assume_aligned(uniform->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		if(iScalar == 1)
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = fin[i] - scale;
		}
		else
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = scale - fin[i];
		}

		cap->MarkModifiedFromCpu();
	}
	*/
}

Filter::DataLocation GPUClockRecoveryFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}
