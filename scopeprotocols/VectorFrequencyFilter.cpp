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
#include "VectorFrequencyFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VectorFrequencyFilter::VectorFrequencyFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_computePipeline("shaders/VectorFrequency.spv", 3, sizeof(VectorFrequencyConstants))
{
	AddStream(Unit(Unit::UNIT_HZ), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("I");
	CreateInput("Q");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool VectorFrequencyFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string VectorFrequencyFilter::GetProtocolName()
{
	return "Vector Frequency";
}

Filter::DataLocation VectorFrequencyFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void VectorFrequencyFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("VectorFrequencyFilter::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearErrors();
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		for(int i=0; i<2; i++)
		{
			if(!GetInput(i))
				AddErrorMessage("Missing inputs", string("No signal input connected to ") + m_signalNames[i] );
			else
			{
				auto w = GetInputWaveform(i);
				if(!w)
					AddErrorMessage("Missing inputs", string("No waveform available at input ") + m_signalNames[i] );
				else if(!dynamic_cast<UniformAnalogWaveform*>(w))
					AddErrorMessage("Invalid input", string("Expected uniform analog waveform at input ") + m_signalNames[i] );
			}
		}

		SetData(nullptr, 0);
		return;
	}

	auto din_i = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	auto din_q = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(1));
	size_t len = min(din_i->size(), din_q->size());

	auto dout = SetupEmptyUniformAnalogOutputWaveform(din_i, 0);
	dout->Resize(len);

	//Calculate scaling factor from rad/sample to Hz
	float sample_hz = FS_PER_SECOND / din_i->m_timescale;
	VectorFrequencyConstants cfg;
	cfg.len = len - 1;	//need two samples for deltas
	cfg.scale = sample_hz / (2 * M_PI);

	cmdBuf.begin({});

	m_computePipeline.BindBufferNonblocking(0, din_i->m_samples, cmdBuf);
	m_computePipeline.BindBufferNonblocking(1, din_q->m_samples, cmdBuf);
	m_computePipeline.BindBufferNonblocking(2, dout->m_samples, cmdBuf, true);
	const uint32_t compute_block_count = GetComputeBlockCount(len, 64);
	m_computePipeline.Dispatch(cmdBuf, cfg,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	dout->m_samples.MarkModifiedFromGpu();
}
