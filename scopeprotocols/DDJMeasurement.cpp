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
#include "DDJMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DDJMeasurement::DDJMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);

	//Set up channels
	CreateInput("TIE");
	CreateInput("sampledThreshold");

	for(int i=0; i<256; i++)
		m_table[i] = 0;

	m_numTable.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
	m_sumTable.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64 && g_hasShaderAtomicFloat && g_hasShaderInt8)
	{
		m_computePipeline =
			make_shared<ComputePipeline>("shaders/DDJMeasurement.spv", 7, sizeof(DDJConstants));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DDJMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) &&
		(stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
		(stream.GetYAxisUnits() == Unit::UNIT_FS)
		)
	{
		return true;
	}
	if( (i == 1) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DDJMeasurement::GetProtocolName()
{
	return "DDJ";
}

Filter::DataLocation DDJMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DDJMeasurement::Refresh(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range range("DDJMeasurement::Refresh");
	#endif

	ClearErrors();
	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal connected to TIE input");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at TIE input");

		if(!GetInput(1))
			AddErrorMessage("Missing inputs", "No signal connected to threshold input");
		else if(!GetInputWaveform(1))
			AddErrorMessage("Missing inputs", "No waveform available at threshold input");

		m_streams[0].m_value = NAN;
		return;
	}

	//Get the input data
	auto tie = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(0));
	auto sampledData = dynamic_cast<SparseDigitalWaveform*>(GetInputWaveform(1));
	if(!tie || !sampledData)
	{
		AddErrorMessage("Missing inputs", "Invalid or missing waveform input");

		m_streams[0].m_value = NAN;
		return;
	}

	size_t tielen = tie->size();
	size_t samplen = sampledData->size();

	//DDJ history (8 UIs)
	uint8_t window = 0;

	//Table of jitter indexed by history
	//TODO: we can probably use a shader for this instead to avoid unnecessary round trips
	size_t num_bins = 256;
	m_numTable.resize(num_bins);
	m_sumTable.resize(num_bins);
	m_numTable.PrepareForCpuAccessIgnoringGpuData();
	m_sumTable.PrepareForCpuAccessIgnoringGpuData();
	for(size_t i=0; i<num_bins; i++)
	{
		m_numTable[i] = 0;
		m_sumTable[i] = 0;
	}
	m_numTable.MarkModifiedFromCpu();
	m_sumTable.MarkModifiedFromCpu();

	//Loop over the TIE and threshold waveform and assign jitter to bins
	//We know the TIE is 1fs resolution so avoid needless scaling
	if(g_hasShaderInt64 && g_hasShaderAtomicInt64 && g_hasShaderAtomicFloat && g_hasShaderInt8)
	{
		cmdBuf.begin({});

		uint64_t numThreads = 4096;
		const uint64_t blockSize = 64;
		const uint64_t numBlocks = numThreads / blockSize;

		DDJConstants cfg;
		cfg.numDataSamples = sampledData->size();
		cfg.numTieSamples = tie->size();

		m_computePipeline->BindBufferNonblocking(0, tie->m_offsets, cmdBuf);
		m_computePipeline->BindBufferNonblocking(1, tie->m_samples, cmdBuf);
		m_computePipeline->BindBufferNonblocking(2, sampledData->m_offsets, cmdBuf);
		m_computePipeline->BindBufferNonblocking(3, sampledData->m_durations, cmdBuf);
		m_computePipeline->BindBufferNonblocking(4, sampledData->m_samples, cmdBuf);
		m_computePipeline->BindBufferNonblocking(5, m_numTable, cmdBuf);
		m_computePipeline->BindBufferNonblocking(6, m_sumTable, cmdBuf);
		m_computePipeline->Dispatch(cmdBuf, cfg, numBlocks);
		m_computePipeline->AddComputeMemoryBarrier(cmdBuf);

		m_numTable.MarkModifiedFromGpu();
		m_sumTable.MarkModifiedFromGpu();

		m_numTable.PrepareForCpuAccessNonblocking(cmdBuf);
		m_sumTable.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
	}
	else
	{
		//Get all of the input on the CPU
		cmdBuf.begin({});
		tie->m_offsets.PrepareForCpuAccessNonblocking(cmdBuf);
		tie->m_samples.PrepareForCpuAccessNonblocking(cmdBuf);
		sampledData->m_offsets.PrepareForCpuAccessNonblocking(cmdBuf);
		sampledData->m_durations.PrepareForCpuAccessNonblocking(cmdBuf);
		sampledData->m_samples.PrepareForCpuAccessNonblocking(cmdBuf);
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		size_t nbits = 0;
		int64_t tfirst = tie->m_offsets[0];
		size_t itie = 0;
		size_t tielast = tielen - 1;
		for(size_t idata=0; idata < samplen; idata ++)
		{
			//Sample the next bit in the thresholded waveform
			window = (window >> 1);
			if(sampledData->m_samples[idata])
				window |= 0x80;
			nbits ++;

			//need 8 in last_window, plus one more for the current bit
			if(nbits < 9)
				continue;

			//If we're still before the first TIE sample, nothing to do
			int64_t tstart = sampledData->m_offsets[idata];
			if(tstart < tfirst)
				continue;

			//Advance TIE samples if needed
			int64_t target = tie->m_offsets[itie];
			while( (target < tstart) && (itie < tielast) )
			{
				itie ++;
				target = tie->m_offsets[itie];
			}
			if(itie >= tielen)
				break;

			//If the TIE sample is not in this bit, don't do anything.
			//We need edges within this UI.
			int64_t tend = tstart + sampledData->m_durations[idata];
			if(target > tend)
				continue;

			//Save the info in the DDJ table
			m_numTable[window] ++;
			m_sumTable[window] += tie->m_samples[itie];
		}

		m_numTable.PrepareForCpuAccess();
		m_sumTable.PrepareForCpuAccess();
	}

	//Calculate DDJ
	float ddjmin =  FLT_MAX;
	float ddjmax = 0;
	for(size_t i=0; i<num_bins; i++)
	{
		if(m_numTable[i] != 0)
		{
			float jitter = m_sumTable[i] * 1.0 / m_numTable[i];
			m_table[i] = jitter;
			ddjmin = min(ddjmin, jitter);
			ddjmax = max(ddjmax, jitter);
		}
		else
			m_table[i] = 0;
	}

	m_streams[0].m_value = ddjmax - ddjmin;
}
