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
#include "ACRMSMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ACRMSMeasurement::ACRMSMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	m_rmsComputePipeline = make_unique<ComputePipeline>(
		"shaders/ACRMS.spv",
		2,
		sizeof(ACRMSPushConstants));

	//we need this readable from the CPU to do the final summation
	m_temporaryResults.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_temporaryResults.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	if(g_hasShaderInt64)
	{
		m_trendComputePipeline = make_unique<ComputePipeline>(
			"shaders/ACRMS_Trend.spv",
			5,
			sizeof(ACRMSTrendPushConstants));
	}

	//Set up channels
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ACRMSMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ACRMSMeasurement::GetProtocolName()
{
	return "AC RMS";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ACRMSMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();

	auto uadin = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sadin = dynamic_cast<SparseAnalogWaveform*>(din);

	//Copy input unit to output
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 1);

	if(uadin)
		DoRefreshUniform(uadin, cmdBuf, queue);
	else
		DoRefreshSparse(sadin);
}

void ACRMSMeasurement::DoRefreshSparse(SparseAnalogWaveform* wfm)
{
	float average = GetAvgVoltage(wfm);
	auto length = wfm->size();

	//Calculate the global RMS value
	//Sum the squares of all values after subtracting the DC value
	//Kahan summation for improved accuracy
	float temp = 0;
	float c = 0;
	for (size_t i = 0; i < length; i++)
	{
		float delta = wfm->m_samples[i] - average;
		float deltaSquared = delta * delta;
		float y = deltaSquared - c;
		float t = temp + y;
		c = (t - temp) - y;
		temp = t;
	}

	//Divide by total number of samples and take the square root to get the final AC RMS
	m_streams[1].m_value = sqrt(temp / length);

	//Now we can do the cycle-by-cycle value
	temp = 0;
	vector<int64_t> edges;

	//Auto-threshold analog signals at average of the full scale range
	FindZeroCrossings(wfm, average, edges);

	//We need at least one full cycle of the waveform to have a meaningful AC RMS Measurement
	if(edges.size() < 2)
	{
		SetData(nullptr, 0);
		return;
	}

	//Create the output as a sparse waveform
	auto cap = SetupEmptySparseAnalogOutputWaveform(wfm, 0, true);
	cap->PrepareForCpuAccess();

	size_t elen = edges.size();

	for(size_t i = 0; i < (elen - 2); i += 2)
	{
		//Measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
		int64_t start = edges[i] / wfm->m_timescale;
		int64_t end = edges[i + 2] / wfm->m_timescale;
		int64_t j = 0;

		//Simply sum the squares of all values in a cycle after subtracting the DC value
		for(j = start; (j <= end) && (j < (int64_t)length); j++)
			temp += ((wfm->m_samples[j] - average) * (wfm->m_samples[j] - average));

		//Get the difference between the end and start of cycle. This would be the number of samples
		//on which AC RMS calculation was performed
		int64_t delta = j - start - 1;

		if (delta != 0)
		{
			//Divide by total number of samples for one cycle
			temp /= delta;

			//Take square root to get the final AC RMS Value of one cycle
			temp = sqrt(temp);

			//Push values to the waveform
			cap->m_offsets.push_back(start);
			cap->m_durations.push_back(delta);
			cap->m_samples.push_back(temp);
		}
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

void ACRMSMeasurement::DoRefreshUniform(
	UniformAnalogWaveform* wfm,
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	float average = m_averager.Average(wfm, cmdBuf, queue);
	auto length = wfm->size();

	//This value experimentally gives the best speedup for an NVIDIA 2080 Ti vs an Intel Xeon Gold 6144
	//Maybe consider dynamic tuning in the future at initialization?
	const uint64_t numThreads = 16384;

	//Do the bulk RMS calculation on the GPU
	ACRMSPushConstants push;
	push.numSamples = length;
	push.numThreads = numThreads;
	push.samplesPerThread = (length + numThreads) / numThreads;
	push.dcBias = average;
	m_temporaryResults.resize(numThreads);
	cmdBuf.begin({});
	m_rmsComputePipeline->BindBufferNonblocking(0, m_temporaryResults, cmdBuf, true);
	m_rmsComputePipeline->BindBufferNonblocking(1, wfm->m_samples, cmdBuf);
	m_rmsComputePipeline->Dispatch(cmdBuf, push, numThreads, 1);
	m_temporaryResults.MarkModifiedFromGpu();
	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	//Do the final summation of the temporary results
	//These should all be roughly equal in value (famous last words) so don't both with Kahan here
	m_temporaryResults.PrepareForCpuAccess();
	float temp = 0;
	for(uint64_t i=0; i<numThreads; i++)
		temp += m_temporaryResults[i];

	//Divide by total number of samples and take the square root to get the final AC RMS
	m_streams[1].m_value = sqrt(temp / length);

	//Auto-threshold analog signals at average of the full scale range
	size_t elen = m_detector.FindZeroCrossings(wfm, average, cmdBuf, queue);
	auto& edges = m_detector.GetResults();

	//We need at least one full cycle of the waveform to have a meaningful AC RMS Measurement
	if(elen < 2)
	{
		SetData(nullptr, 0);
		return;
	}

	//Create the output as a sparse waveform
	auto cap = SetupEmptySparseAnalogOutputWaveform(wfm, 0, true);
	cap->Resize((elen-1)/2);

	//GPU path needs native int64, no bignum fallback for now
	if(g_hasShaderInt64)
	{
		cmdBuf.begin({});

		ACRMSTrendPushConstants tpush;
		tpush.timescale		= wfm->m_timescale;
		tpush.numSamples	= wfm->m_samples.size();
		tpush.numEdgePairs	= (elen-1) / 2;
		tpush.dcBias		= average;

		m_trendComputePipeline->BindBufferNonblocking(0, cap->m_samples, cmdBuf, true);
		m_trendComputePipeline->BindBufferNonblocking(1, cap->m_offsets, cmdBuf, true);
		m_trendComputePipeline->BindBufferNonblocking(2, cap->m_durations, cmdBuf, true);
		m_trendComputePipeline->BindBufferNonblocking(3, wfm->m_samples, cmdBuf);
		m_trendComputePipeline->BindBufferNonblocking(4, edges, cmdBuf);

		const uint32_t compute_block_count = GetComputeBlockCount(tpush.numEdgePairs, 64);
		m_trendComputePipeline->Dispatch(cmdBuf, tpush,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		cap->MarkModifiedFromGpu();
	}

	//CPU fallback if no int64 capability
	else
	{
		cap->PrepareForCpuAccess();
		edges.PrepareForCpuAccess();
		temp = 0;
		for(size_t i = 0; i < (elen - 2); i += 2)
		{
			//Measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
			int64_t start = edges[i] / wfm->m_timescale;
			int64_t end = edges[i + 2] / wfm->m_timescale;
			int64_t j = 0;

			//Simply sum the squares of all values in a cycle after subtracting the DC value
			temp = 0;
			for(j = start; (j <= end) && (j < (int64_t)length); j++)
				temp += ((wfm->m_samples[j] - average) * (wfm->m_samples[j] - average));

			//Get the difference between the end and start of cycle. This would be the number of samples
			//on which AC RMS calculation was performed
			int64_t delta = j - start - 1;

			//Divide by total number of samples for one cycle (with divide-by-zero check for garbage input)
			if (delta == 0)
				temp = 0;
			else
				temp /= delta;

			//Take square root to get the final AC RMS Value of one cycle
			temp = sqrt(temp);

			//Push values to the waveform
			size_t nout = i/2;
			cap->m_offsets[nout] = start;
			cap->m_durations[nout] = delta;
			cap->m_samples[nout] = temp;
		}
		cap->MarkModifiedFromCpu();
	}

	SetData(cap, 0);
}

Filter::DataLocation ACRMSMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}
