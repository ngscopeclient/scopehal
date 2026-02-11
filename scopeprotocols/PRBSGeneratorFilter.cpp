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

//LFSR lookahead table for PRBS-23 polynomail
const uint32_t g_prbs23Table[23][23] =
{
	{ 0x000002, 0x000004, 0x000008, 0x000010, 0x000020, 0x000040, 0x000080, 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080000, 0x100000, 0x200000, 0x400000, 0x000001 },	//0
	{ 0x000004, 0x000008, 0x000010, 0x000020, 0x000040, 0x000080, 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080002, 0x100000, 0x200000, 0x400000, 0x000001, 0x000002 },	//1
	{ 0x000010, 0x000020, 0x000040, 0x000080, 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080002, 0x100004, 0x200008, 0x400000, 0x000001, 0x000002, 0x000004, 0x000008 },	//2
	{ 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080002, 0x100004, 0x200008, 0x400010, 0x000021, 0x000042, 0x000084, 0x000008, 0x000010, 0x000020, 0x000040, 0x000080 },	//3
	{ 0x010000, 0x020000, 0x040001, 0x080002, 0x100004, 0x200008, 0x400010, 0x000021, 0x000042, 0x000084, 0x000108, 0x000210, 0x000420, 0x000840, 0x001080, 0x002100, 0x004200, 0x008400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000 },	//4
	{ 0x004200, 0x008400, 0x010800, 0x021000, 0x042001, 0x084002, 0x108004, 0x210008, 0x420010, 0x040020, 0x080040, 0x100080, 0x200100, 0x400200, 0x000401, 0x000802, 0x001004, 0x002008, 0x000210, 0x000420, 0x000840, 0x001080, 0x002100 },	//5
	{ 0x040421, 0x080842, 0x101084, 0x202108, 0x404210, 0x008421, 0x010842, 0x021084, 0x042109, 0x084212, 0x108424, 0x210848, 0x421090, 0x042120, 0x084240, 0x108480, 0x210900, 0x421200, 0x002021, 0x004042, 0x008084, 0x010108, 0x020210 },	//6
	{ 0x142405, 0x28480a, 0x509014, 0x212029, 0x424052, 0x0480a4, 0x090148, 0x120290, 0x240521, 0x480a42, 0x101485, 0x20290a, 0x405214, 0x00a429, 0x014852, 0x0290a4, 0x052149, 0x0a4292, 0x00a120, 0x014240, 0x028480, 0x050901, 0x0a1202 },	//7
	{ 0x56211d, 0x2c423a, 0x588474, 0x3108e9, 0x6211d2, 0x4423a4, 0x084749, 0x108e92, 0x211d24, 0x423a48, 0x047490, 0x08e920, 0x11d240, 0x23a480, 0x474901, 0x0e9202, 0x1d2405, 0x3a480a, 0x22b108, 0x456211, 0x0ac423, 0x158847, 0x2b108e },	//8
	{ 0x662859, 0x4c50b2, 0x18a165, 0x3142ca, 0x628594, 0x450b28, 0x0a1651, 0x142ca3, 0x285946, 0x50b28c, 0x216519, 0x42ca32, 0x059464, 0x0b28c8, 0x165191, 0x2ca323, 0x594646, 0x328c8d, 0x033142, 0x066285, 0x0cc50b, 0x198a16, 0x33142c },	//9
	{ 0x6d3859, 0x5a70b3, 0x34e166, 0x69c2cc, 0x538599, 0x270b32, 0x4e1665, 0x1c2cca, 0x385994, 0x70b328, 0x616651, 0x42cca3, 0x059946, 0x0b328c, 0x166519, 0x2cca33, 0x599466, 0x3328cd, 0x0b69c2, 0x16d385, 0x2da70b, 0x5b4e16, 0x369c2c },	//10
	{ 0x7cf21b, 0x79e437, 0x73c86f, 0x6790de, 0x4f21bc, 0x1e4378, 0x3c86f1, 0x790de2, 0x721bc5, 0x64378a, 0x486f15, 0x10de2b, 0x21bc56, 0x4378ac, 0x06f158, 0x0de2b1, 0x1bc562, 0x378ac5, 0x13e790, 0x27cf21, 0x4f9e43, 0x1f3c86, 0x3e790d },	//11
	{ 0x7ab4ae, 0x75695c, 0x6ad2b9, 0x55a572, 0x2b4ae5, 0x5695cb, 0x2d2b96, 0x5a572c, 0x34ae58, 0x695cb0, 0x52b961, 0x2572c2, 0x4ae584, 0x15cb08, 0x2b9610, 0x572c21, 0x2e5842, 0x5cb085, 0x43d5a5, 0x07ab4a, 0x0f5695, 0x1ead2b, 0x3d5a57 },	//12
	{ 0x6bdd9a, 0x57bb34, 0x2f7668, 0x5eecd1, 0x3dd9a2, 0x7bb344, 0x776688, 0x6ecd10, 0x5d9a20, 0x3b3441, 0x766883, 0x6cd106, 0x59a20d, 0x33441b, 0x668837, 0x4d106e, 0x1a20dd, 0x3441bb, 0x035eec, 0x06bdd9, 0x0d7bb3, 0x1af766, 0x35eecd },	//13
	{ 0x689fb2, 0x513f65, 0x227ecb, 0x44fd97, 0x09fb2f, 0x13f65e, 0x27ecbd, 0x4fd97b, 0x1fb2f6, 0x3f65ed, 0x7ecbdb, 0x7d97b6, 0x7b2f6d, 0x765eda, 0x6cbdb4, 0x597b69, 0x32f6d3, 0x65eda7, 0x2344fd, 0x4689fb, 0x0d13f6, 0x1a27ec, 0x344fd9 },	//14
	{ 0x6dd5d3, 0x5baba7, 0x37574e, 0x6eae9d, 0x5d5d3a, 0x3aba75, 0x7574eb, 0x6ae9d7, 0x55d3ae, 0x2ba75d, 0x574ebb, 0x2e9d76, 0x5d3aed, 0x3a75db, 0x74ebb7, 0x69d76f, 0x53aedf, 0x275dbe, 0x236eae, 0x46dd5d, 0x0dbaba, 0x1b7574, 0x36eae9 },	//15
	{ 0x2da7e3, 0x5b4fc6, 0x369f8c, 0x6d3f19, 0x5a7e33, 0x34fc66, 0x69f8cc, 0x53f199, 0x27e332, 0x4fc665, 0x1f8cca, 0x3f1995, 0x7e332b, 0x7c6656, 0x78ccad, 0x71995b, 0x6332b7, 0x46656e, 0x216d3f, 0x42da7e, 0x05b4fc, 0x0b69f8, 0x16d3f1 },	//16
	{ 0x09a788, 0x134f10, 0x269e21, 0x4d3c43, 0x1a7887, 0x34f10f, 0x69e21e, 0x53c43d, 0x27887a, 0x4f10f5, 0x1e21ea, 0x3c43d5, 0x7887aa, 0x710f55, 0x621eab, 0x443d56, 0x087aad, 0x10f55a, 0x284d3c, 0x509a78, 0x2134f1, 0x4269e2, 0x04d3c4 },	//17
	{ 0x0593cd, 0x0b279a, 0x164f35, 0x2c9e6b, 0x593cd6, 0x3279ad, 0x64f35b, 0x49e6b7, 0x13cd6f, 0x279adf, 0x4f35bf, 0x1e6b7e, 0x3cd6fd, 0x79adfa, 0x735bf5, 0x66b7ea, 0x4d6fd4, 0x1adfa9, 0x302c9e, 0x60593c, 0x40b279, 0x0164f3, 0x02c9e6 },	//18
	{ 0x012292, 0x024524, 0x048a49, 0x091492, 0x122924, 0x245249, 0x48a492, 0x114925, 0x22924a, 0x452495, 0x0a492b, 0x149257, 0x2924ae, 0x52495c, 0x2492b8, 0x492570, 0x124ae1, 0x2495c3, 0x480914, 0x101229, 0x202452, 0x4048a4, 0x009149 },	//19
	{ 0x04020d, 0x08041a, 0x100834, 0x201068, 0x4020d0, 0x0041a1, 0x008342, 0x010684, 0x020d08, 0x041a11, 0x083422, 0x106844, 0x20d088, 0x41a110, 0x034221, 0x068443, 0x0d0887, 0x1a110e, 0x302010, 0x604020, 0x408041, 0x010083, 0x020106 },	//20
	{ 0x002050, 0x0040a0, 0x008140, 0x010280, 0x020500, 0x040a01, 0x081402, 0x102804, 0x205008, 0x40a010, 0x014021, 0x028042, 0x050085, 0x0a010a, 0x140215, 0x28042a, 0x500854, 0x2010a9, 0x400102, 0x000205, 0x00040a, 0x000814, 0x001028 },	//21
	{ 0x001008, 0x002010, 0x004020, 0x008040, 0x010080, 0x020100, 0x040201, 0x080402, 0x100804, 0x201008, 0x402010, 0x004021, 0x008042, 0x010084, 0x020108, 0x040211, 0x080422, 0x100844, 0x200080, 0x400100, 0x000201, 0x000402, 0x000804 },	//22
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PRBSGeneratorFilter::PRBSGeneratorFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_baud(m_parameters["Data Rate"])
	, m_poly(m_parameters["Polynomial"])
	, m_depth(m_parameters["Depth"])
	, m_prbs23Table("PRBSGeneratorFilter.m_prbs23Table")
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
		m_prbs7Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS7.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		m_prbs9Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS9.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		m_prbs11Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS11.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		m_prbs15Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS15.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		//PRBS-23 and up need table for lookahead since they don't run an entire LFSR cycle per thread
		m_prbs23Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS23.spv",
			2,
			sizeof(PRBSGeneratorBlockConstants));

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
		//Config for short sequences
		PRBSGeneratorConstants cfg;
		cfg.count = depth;
		cfg.seed = rand();

		//Config for long sequences
		uint32_t numBlockThreads = 131072;
		PRBSGeneratorBlockConstants blockcfg;
		blockcfg.count = depth;
		blockcfg.seed = rand();
		blockcfg.samplesPerThread = GetComputeBlockCount(depth, numBlockThreads);

		//Figure out the shader and thread block count to use
		uint32_t numThreads = 0;
		shared_ptr<ComputePipeline> pipe;
		switch(poly)
		{
			case POLY_PRBS7:
				numThreads = GetComputeBlockCount(depth, 127);
				pipe = m_prbs7Pipeline;
				break;

			case POLY_PRBS9:
				numThreads = GetComputeBlockCount(depth, 511);
				pipe = m_prbs9Pipeline;
				break;

			case POLY_PRBS11:
				numThreads = GetComputeBlockCount(depth, 2047);
				pipe = m_prbs11Pipeline;
				break;

			case POLY_PRBS15:
				numThreads = GetComputeBlockCount(depth, 32767);
				pipe = m_prbs15Pipeline;
				break;

			case POLY_PRBS23:
				numThreads = numBlockThreads;
				pipe = m_prbs23Pipeline;

			default:
				break;
		}
		const uint32_t threadsPerBlock = 64;
		const uint32_t compute_block_count = GetComputeBlockCount(numThreads, threadsPerBlock);

		switch(poly)
		{
			//Each thread generates a full PRBS cycle from the chosen offset
			case POLY_PRBS7:
			case POLY_PRBS9:
			case POLY_PRBS11:
			case POLY_PRBS15:
				{
					cmdBuf.begin({});

					pipe->BindBufferNonblocking(0, dat->m_samples, cmdBuf, true);
					pipe->Dispatch(cmdBuf, cfg,
						min(compute_block_count, 32768u),
						compute_block_count / 32768 + 1);

					cmdBuf.end();
					queue->SubmitAndBlock(cmdBuf);

					dat->m_samples.MarkModifiedFromGpu();
				}
				break;

			//Larger sequences have separate structure
			case POLY_PRBS23:
				{
					cmdBuf.begin({});

					pipe->BindBufferNonblocking(0, dat->m_samples, cmdBuf, true);
					pipe->BindBufferNonblocking(1, m_prbs23Table, cmdBuf);

					pipe->Dispatch(cmdBuf, blockcfg,
						min(compute_block_count, 32768u),
						compute_block_count / 32768 + 1);

					cmdBuf.end();
					queue->SubmitAndBlock(cmdBuf);

					dat->m_samples.MarkModifiedFromGpu();
				}
				break;

			//Software fallback
			default:
				{
					//Always generate the PRBS
					dat->m_samples.PrepareForCpuAccess();

					uint32_t prbs = cfg.seed;
					for(size_t i=0; i<depth; i++)
						dat->m_samples[i] = RunPRBS(prbs, poly);

					dat->m_samples.MarkModifiedFromCpu();
				}
				break;
		}
	}

	else
	{
		//Always generate the PRBS
		dat->m_samples.PrepareForCpuAccess();

		uint32_t prbs = rand();
		for(size_t i=0; i<depth; i++)
			dat->m_samples[i] = RunPRBS(prbs, poly);

		dat->m_samples.MarkModifiedFromCpu();
	}
}
