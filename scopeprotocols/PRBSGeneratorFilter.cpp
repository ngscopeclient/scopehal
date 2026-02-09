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
#include "PRBSGeneratorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PRBSGeneratorFilter::PRBSGeneratorFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_baud(m_parameters["Data Rate"])
	, m_poly(m_parameters["Polynomial"])
	, m_depth(m_parameters["Depth"])
{
	AddStream(Unit(Unit::UNIT_COUNTS), "Data", Stream::STREAM_TYPE_DIGITAL);
	AddStream(Unit(Unit::UNIT_COUNTS), "Clock", Stream::STREAM_TYPE_DIGITAL);

	m_baud = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_baud.SetIntVal(103125LL * 100LL * 1000LL);

	m_poly = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_poly.AddEnumValue("PRBS-7", POLY_PRBS7);
	m_poly.AddEnumValue("PRBS-9", POLY_PRBS9);
	m_poly.AddEnumValue("PRBS-11", POLY_PRBS11);
	m_poly.AddEnumValue("PRBS-15", POLY_PRBS15);
	m_poly.AddEnumValue("PRBS-23", POLY_PRBS23);
	m_poly.AddEnumValue("PRBS-31", POLY_PRBS31);
	m_poly.SetIntVal(POLY_PRBS7);

	m_depth = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_depth.SetIntVal(100 * 1000);

	if(g_hasShaderInt8)
	{
		m_prbs7Pipeline = make_unique<ComputePipeline>(
			"shaders/PRBS7.spv",
			1,
			sizeof(PRBSGeneratorConstants));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PRBSGeneratorFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PRBSGeneratorFilter::GetProtocolName()
{
	return "PRBS";
}

void PRBSGeneratorFilter::SetDefaultName()
{
	Unit rate(Unit::UNIT_BITRATE);

	string prefix = "";
	switch(m_poly.GetIntVal())
	{
		case POLY_PRBS7:
			prefix = "PRBS7";
			break;

		case POLY_PRBS9:
			prefix = "PRBS9";
			break;

		case POLY_PRBS11:
			prefix = "PRBS11";
			break;

		case POLY_PRBS15:
			prefix = "PRBS15";
			break;

		case POLY_PRBS23:
			prefix = "PRBS23";
			break;

		case POLY_PRBS31:
		default:
			prefix = "PRBS31";
			break;
	}

	m_hwname = prefix + "(" + rate.PrettyPrint(m_baud.GetIntVal()).c_str() + ")";
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

bool PRBSGeneratorFilter::RunPRBS(uint32_t& state, Polynomials poly)
{
	uint32_t next;
	switch(poly)
	{
		case POLY_PRBS7:
			next = ( (state >> 6) ^ (state >> 5) ) & 1;
			break;

		case POLY_PRBS9:
			next = ( (state >> 8) ^ (state >> 4) ) & 1;
			break;

		case POLY_PRBS11:
			next = ( (state >> 10) ^ (state >> 8) ) & 1;
			break;

		case POLY_PRBS15:
			next = ( (state >> 14) ^ (state >> 13) ) & 1;
			break;

		case POLY_PRBS23:
			next = ( (state >> 22) ^ (state >> 17) ) & 1;
			break;

		case POLY_PRBS31:
		default:
			next = ( (state >> 30) ^ (state >> 27) ) & 1;
			break;
	}
	state = (state << 1) | next;
	return (bool)next;
}

Filter::DataLocation PRBSGeneratorFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

void PRBSGeneratorFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("PRBSGeneratorFilter::Refresh");
	#endif

	size_t depth = m_depth.GetIntVal();
	int64_t baudrate = m_baud.GetIntVal();
	auto poly = static_cast<Polynomials>(m_poly.GetIntVal());
	size_t samplePeriod = FS_PER_SECOND / baudrate;

	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Create the two output waveforms
	auto dat = dynamic_cast<UniformDigitalWaveform*>(GetData(0));
	if(!dat)
	{
		dat = new UniformDigitalWaveform;
		SetData(dat, 0);
	}
	dat->m_timescale = samplePeriod;
	dat->m_triggerPhase = 0;
	dat->m_startTimestamp = floor(t);
	dat->m_startFemtoseconds = fs;
	dat->Resize(depth);

	//Set up the clock waveform
	auto clk = dynamic_cast<UniformDigitalWaveform*>(GetData(1));
	if(!clk)
	{
		clk = new UniformDigitalWaveform;
		SetData(clk, 1);
	}
	clk->m_timescale = samplePeriod;
	clk->m_triggerPhase = samplePeriod / 2;
	clk->m_startTimestamp = floor(t);
	clk->m_startFemtoseconds = fs;
	size_t oldClockSize = clk->size();
	clk->Resize(depth);

	//Only generate the clock waveform if we changed length
	if(oldClockSize != depth)
	{
		clk->PrepareForCpuAccess();

		bool lastclk = false;
		for(size_t i=0; i<depth; i++)
		{
			clk->m_samples[i] = lastclk;
			lastclk = !lastclk;
		}

		clk->MarkModifiedFromCpu();
	}

	//GPU path
	if(g_hasShaderInt8)
	{
		PRBSGeneratorConstants cfg;
		cfg.count = depth;
		cfg.seed = rand();

		switch(poly)
		{
			case POLY_PRBS7:
				{
					//PRBS7 path: each thread generates a full PRBS cycle (127 bits) from the chosen offset
					uint32_t numThreads = GetComputeBlockCount(depth, 127);
					const uint32_t compute_block_count = GetComputeBlockCount(depth, 64);

					cmdBuf.begin({});

					m_prbs7Pipeline->BindBufferNonblocking(0, dat->m_samples, cmdBuf, true);
					m_prbs7Pipeline->Dispatch(cmdBuf, cfg,
						min(compute_block_count, 32768u),
						compute_block_count / 32768 + 1);

					cmdBuf.end();
					queue->SubmitAndBlock(cmdBuf);

					dat->m_samples.MarkModifiedFromGpu();
				}
				break;

			default:
				{
					//Always generate the PRBS
					dat->PrepareForCpuAccess();

					uint32_t prbs = cfg.seed;
					for(size_t i=0; i<depth; i++)
						dat->m_samples[i] = RunPRBS(prbs, poly);

					dat->MarkModifiedFromCpu();
				}
				break;
		}
	}

	else
	{
		//Always generate the PRBS
		dat->PrepareForCpuAccess();

		uint32_t prbs = rand();
		for(size_t i=0; i<depth; i++)
			dat->m_samples[i] = RunPRBS(prbs, poly);

		dat->MarkModifiedFromCpu();
	}
}
