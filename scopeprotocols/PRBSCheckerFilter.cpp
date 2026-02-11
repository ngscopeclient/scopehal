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
#include "PRBSCheckerFilter.h"
#include "PRBSGeneratorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PRBSCheckerFilter::PRBSCheckerFilter(const string& color)
	: Filter(color, CAT_ANALYSIS)
	, m_poly(m_parameters["Polynomial"])
	, m_lastSize(0)
	, m_prbs23Table("PRBSCheckerFilter.m_prbs23Table")
{
	AddDigitalStream("data");

	CreateInput("sampledData");

	m_poly = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_poly.AddEnumValue("PRBS-7", PRBSGeneratorFilter::POLY_PRBS7);
	m_poly.AddEnumValue("PRBS-9", PRBSGeneratorFilter::POLY_PRBS9);
	m_poly.AddEnumValue("PRBS-11", PRBSGeneratorFilter::POLY_PRBS11);
	m_poly.AddEnumValue("PRBS-15", PRBSGeneratorFilter::POLY_PRBS15);
	m_poly.AddEnumValue("PRBS-23", PRBSGeneratorFilter::POLY_PRBS23);
	m_poly.AddEnumValue("PRBS-31", PRBSGeneratorFilter::POLY_PRBS31);
	m_poly.SetIntVal(PRBSGeneratorFilter::POLY_PRBS7);

	if(g_hasShaderInt8)
	{
		m_prbs7Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS7Checker.spv",
			2,
			sizeof(PRBSCheckerConstants));

		m_prbs9Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS9Checker.spv",
			2,
			sizeof(PRBSCheckerConstants));

		m_prbs11Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS11Checker.spv",
			2,
			sizeof(PRBSCheckerConstants));

		m_prbs15Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS15Checker.spv",
			2,
			sizeof(PRBSCheckerConstants));

		//PRBS-23 and up need table for lookahead since they don't run an entire LFSR cycle per thread
		m_prbs23Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS23Checker.spv",
			3,
			sizeof(PRBSCheckerBlockConstants));

		//Fill lookahead table for PRBS-23
		uint32_t rows = 23;
		uint32_t cols = rows;
		m_prbs23Table.resize(rows * cols);
		m_prbs23Table.PrepareForCpuAccess();
		m_prbs23Table.SetGpuAccessHint(AcceleratorBuffer<uint32_t>::HINT_LIKELY);
		for(uint32_t row=0; row<rows; row++)
		{
			for(uint32_t col=0; col<cols; col++)
				m_prbs23Table[row*cols + col] = g_prbs23Table[row][col];
		}
		m_prbs23Table.MarkModifiedFromCpu();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PRBSCheckerFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

Filter::DataLocation PRBSCheckerFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PRBSCheckerFilter::GetProtocolName()
{
	return "PRBS Checker";
}

void PRBSCheckerFilter::SetDefaultName()
{
	Unit rate(Unit::UNIT_BITRATE);

	string prefix = "";
	switch(m_poly.GetIntVal())
	{
		case PRBSGeneratorFilter::POLY_PRBS7:
			prefix = "PRBS7";
			break;

		case PRBSGeneratorFilter::POLY_PRBS9:
			prefix = "PRBS9";
			break;

		case PRBSGeneratorFilter::POLY_PRBS11:
			prefix = "PRBS11";
			break;

		case PRBSGeneratorFilter::POLY_PRBS15:
			prefix = "PRBS15";
			break;

		case PRBSGeneratorFilter::POLY_PRBS23:
			prefix = "PRBS23";
			break;

		case PRBSGeneratorFilter::POLY_PRBS31:
		default:
			prefix = "PRBS31";
			break;
	}

	m_hwname = prefix + "Check" + "_" + to_string(m_instanceNum + 1);
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PRBSCheckerFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("PRBSCheckerFilter::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearErrors();
	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto udin = dynamic_cast<UniformDigitalWaveform*>(din);
	if(!sdin && !udin)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");
		else
			AddErrorMessage("Invalid input", "Expected a digital waveform");

		SetData(nullptr, 0);
		return;
	}

	//Figure out how many bits of state we need
	auto poly = static_cast<PRBSGeneratorFilter::Polynomials>(m_poly.GetIntVal());
	size_t statesize = poly;

	//Need at least the state size worth of data bits to do a meaningful check
	auto len = din->size();
	if(len < statesize)
	{
		AddErrorMessage("Input too short", "Cannot verify a PRBS with input shorter than the polynomial length");
		SetData(nullptr, 0);
		return;
	}

	//Create the output "error found" waveform
	auto dout = SetupEmptySparseDigitalOutputWaveform(din, 0);
	dout->Resize(len);

	//Sparse path: Copy offsets and durations (TODO make this nonblocking?)
	if(sdin)
	{
		dout->m_offsets.CopyFrom(sdin->m_offsets);
		dout->m_durations.CopyFrom(sdin->m_durations);
	}

	//Uniform path
	//(only write if size changed)
	else if(len != m_lastSize)
	{
		dout->m_offsets.PrepareForCpuAccess();
		dout->m_durations.PrepareForCpuAccess();

		for(size_t i=0; i<len; i++)
		{
			dout->m_offsets[i] = i;
			dout->m_durations[i] = 1;
		}

		dout->m_offsets.MarkModifiedFromCpu();
		dout->m_durations.MarkModifiedFromCpu();

		m_lastSize = len;
	}

	//GPU path
	if(g_hasShaderInt8)
	{
		PRBSCheckerConstants cfg;
		cfg.count = len;

		uint32_t numBlockThreads = 524288;
		PRBSCheckerBlockConstants blockcfg;
		blockcfg.count = len;
		blockcfg.samplesPerThread = GetComputeBlockCount(len, numBlockThreads);

		//Figure out the shader and thread block count to use
		uint32_t numThreads = 0;
		shared_ptr<ComputePipeline> pipe;
		switch(poly)
		{
			case PRBSGeneratorFilter::POLY_PRBS7:
				numThreads = GetComputeBlockCount(len, 127);
				pipe = m_prbs7Pipeline;
				break;

			case PRBSGeneratorFilter::POLY_PRBS9:
				numThreads = GetComputeBlockCount(len, 511);
				pipe = m_prbs9Pipeline;
				break;

			case PRBSGeneratorFilter::POLY_PRBS11:
				numThreads = GetComputeBlockCount(len, 2047);
				pipe = m_prbs11Pipeline;
				break;

			case PRBSGeneratorFilter::POLY_PRBS15:
				numThreads = GetComputeBlockCount(len, 32767);
				pipe = m_prbs15Pipeline;
				break;

			case PRBSGeneratorFilter::POLY_PRBS23:
				numThreads = numBlockThreads;
				pipe = m_prbs23Pipeline;
				break;

			default:
				break;
		}
		const uint32_t threadsPerBlock = 64;
		const uint32_t compute_block_count = GetComputeBlockCount(numThreads, threadsPerBlock);

		switch(poly)
		{
			//Each thread checks a full PRBS cycle from the chosen offset
			case PRBSGeneratorFilter::POLY_PRBS7:
			case PRBSGeneratorFilter::POLY_PRBS9:
			case PRBSGeneratorFilter::POLY_PRBS11:
			case PRBSGeneratorFilter::POLY_PRBS15:
				{
					cmdBuf.begin({});

					if(sdin)
						pipe->BindBufferNonblocking(0, sdin->m_samples, cmdBuf);
					else
						pipe->BindBufferNonblocking(0, udin->m_samples, cmdBuf);

					pipe->BindBufferNonblocking(1, dout->m_samples, cmdBuf, true);
					pipe->Dispatch(cmdBuf, cfg,
						min(compute_block_count, 32768u),
						compute_block_count / 32768 + 1);

					cmdBuf.end();
					queue->SubmitAndBlock(cmdBuf);

					dout->m_samples.MarkModifiedFromGpu();
				}
				return;

			//Larger sequences have separate structure with lookahead
			case PRBSGeneratorFilter::POLY_PRBS23:
				{
					cmdBuf.begin({});

					if(sdin)
						pipe->BindBufferNonblocking(0, sdin->m_samples, cmdBuf);
					else
						pipe->BindBufferNonblocking(0, udin->m_samples, cmdBuf);

					pipe->BindBufferNonblocking(1, dout->m_samples, cmdBuf, true);
					pipe->BindBufferNonblocking(2, m_prbs23Table, cmdBuf);

					pipe->Dispatch(cmdBuf, blockcfg,
						min(compute_block_count, 32768u),
						compute_block_count / 32768 + 1);

					cmdBuf.end();
					queue->SubmitAndBlock(cmdBuf);

					dout->m_samples.MarkModifiedFromGpu();
				}
				return;

			default:
				break;
		}
	}

	//CPU fallback if we get to this point

	//Sparse path
	uint32_t prbs = 0;
	if(sdin)
	{
		dout->m_samples.PrepareForCpuAccess();
		sdin->m_samples.PrepareForCpuAccess();

		//Read the first N bits of state into the seed
		for(size_t i=0; i<statesize; i++)
		{
			prbs = (prbs << 1) | sdin->m_samples[i];
			dout->m_samples[i] = 0;
		}

		//Start checking actual data bits
		for(size_t i=statesize; i<len; i++)
		{
			bool value = PRBSGeneratorFilter::RunPRBS(prbs, poly);
			dout->m_samples[i] = (value != sdin->m_samples[i]);
		}

		dout->m_samples.MarkModifiedFromCpu();
	}

	//Uniform path
	else
	{
		dout->m_samples.PrepareForCpuAccess();
		udin->m_samples.PrepareForCpuAccess();

		//Read the first N bits of state into the seed
		for(size_t i=0; i<statesize; i++)
		{
			prbs = (prbs << 1) | udin->m_samples[i];
			dout->m_samples[i] = 0;
		}

		//Start checking actual data bits
		for(size_t i=statesize; i<len; i++)
		{
			bool value = PRBSGeneratorFilter::RunPRBS(prbs, poly);
			dout->m_samples[i] = (value != udin->m_samples[i]);
		}

		dout->m_samples.MarkModifiedFromCpu();
	}
}
