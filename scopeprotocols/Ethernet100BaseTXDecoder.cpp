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
#include "EthernetProtocolDecoder.h"
#include "Ethernet100BaseTXDecoder.h"

using namespace std;

//Coefficient table for all possible powers of two loop iterations
static const uint16_t g_lfsrTable[][11] =
{
	{ 0x002, 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x400, 0x001 },	//0
	{ 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x402, 0x001, 0x002 },	//1
	{ 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x402, 0x005, 0x00a, 0x004, 0x008 },	//2
	{ 0x100, 0x201, 0x402, 0x005, 0x00a, 0x014, 0x028, 0x050, 0x0a0, 0x040, 0x080 },	//3
	{ 0x0a0, 0x140, 0x281, 0x502, 0x204, 0x408, 0x011, 0x022, 0x044, 0x028, 0x050 },	//4
	{ 0x42a, 0x055, 0x0aa, 0x154, 0x2a9, 0x552, 0x2a4, 0x548, 0x290, 0x10a, 0x215 },	//5
	{ 0x646, 0x48d, 0x11b, 0x237, 0x46e, 0x0dd, 0x1ba, 0x375, 0x6eb, 0x391, 0x723 },	//6
	{ 0x09e, 0x13c, 0x279, 0x4f2, 0x1e5, 0x3cb, 0x797, 0x72e, 0x65c, 0x427, 0x04f },	//7
	{ 0x17c, 0x2f9, 0x5f2, 0x3e4, 0x7c9, 0x792, 0x724, 0x648, 0x491, 0x05f, 0x0be },	//8
	{ 0x5f8, 0x3f0, 0x7e1, 0x7c2, 0x784, 0x708, 0x610, 0x421, 0x043, 0x57e, 0x2fc },	//9
	{ 0x7c0, 0x780, 0x700, 0x600, 0x401, 0x003, 0x006, 0x00c, 0x018, 0x7f0, 0x7e0 },	//10
	{ 0x002, 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x400, 0x001 },	//11
	{ 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x402, 0x001, 0x002 },	//12
	{ 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x402, 0x005, 0x00a, 0x004, 0x008 },	//13
	{ 0x100, 0x201, 0x402, 0x005, 0x00a, 0x014, 0x028, 0x050, 0x0a0, 0x040, 0x080 },	//14
	{ 0x0a0, 0x140, 0x281, 0x502, 0x204, 0x408, 0x011, 0x022, 0x044, 0x028, 0x050 },	//15
	{ 0x42a, 0x055, 0x0aa, 0x154, 0x2a9, 0x552, 0x2a4, 0x548, 0x290, 0x10a, 0x215 },	//16
	{ 0x646, 0x48d, 0x11b, 0x237, 0x46e, 0x0dd, 0x1ba, 0x375, 0x6eb, 0x391, 0x723 },	//17
	{ 0x09e, 0x13c, 0x279, 0x4f2, 0x1e5, 0x3cb, 0x797, 0x72e, 0x65c, 0x427, 0x04f },	//18
	{ 0x17c, 0x2f9, 0x5f2, 0x3e4, 0x7c9, 0x792, 0x724, 0x648, 0x491, 0x05f, 0x0be },	//19
	{ 0x5f8, 0x3f0, 0x7e1, 0x7c2, 0x784, 0x708, 0x610, 0x421, 0x043, 0x57e, 0x2fc },	//20
	{ 0x7c0, 0x780, 0x700, 0x600, 0x401, 0x003, 0x006, 0x00c, 0x018, 0x7f0, 0x7e0 },	//21
	{ 0x002, 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x400, 0x001 },	//22
	{ 0x004, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x402, 0x001, 0x002 },	//23
	{ 0x010, 0x020, 0x040, 0x080, 0x100, 0x201, 0x402, 0x005, 0x00a, 0x004, 0x008 },	//24
	{ 0x100, 0x201, 0x402, 0x005, 0x00a, 0x014, 0x028, 0x050, 0x0a0, 0x040, 0x080 },	//25
	{ 0x0a0, 0x140, 0x281, 0x502, 0x204, 0x408, 0x011, 0x022, 0x044, 0x028, 0x050 },	//26
	{ 0x42a, 0x055, 0x0aa, 0x154, 0x2a9, 0x552, 0x2a4, 0x548, 0x290, 0x10a, 0x215 },	//27
	{ 0x646, 0x48d, 0x11b, 0x237, 0x46e, 0x0dd, 0x1ba, 0x375, 0x6eb, 0x391, 0x723 },	//28
	{ 0x09e, 0x13c, 0x279, 0x4f2, 0x1e5, 0x3cb, 0x797, 0x72e, 0x65c, 0x427, 0x04f }		//29
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet100BaseTXDecoder::Ethernet100BaseTXDecoder(const string& color)
	: EthernetProtocolDecoder(color)
{
	m_signalNames.clear();
	m_inputs.clear();

	CreateInput("sampledData");

	if(g_hasShaderInt8)
	{
		m_mlt3DecodeComputePipeline =
			make_shared<ComputePipeline>("shaders/MLT3Decoder.spv", 2, sizeof(uint32_t));

		m_trySyncComputePipeline =
			make_shared<ComputePipeline>("shaders/Ethernet100BaseTX_TrySync.spv", 2, sizeof(uint32_t));

		m_descrambleComputePipeline =
			make_shared<ComputePipeline>(
				"shaders/Ethernet100BaseTXDescrambler.spv",
				3,
				sizeof(Ethernet100BaseTXDescramblerConstants));

		m_phyBits.SetGpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_LIKELY);
		m_descrambledBits.SetGpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_LIKELY);
		m_lfsrTable.SetGpuAccessHint(AcceleratorBuffer<uint32_t>::HINT_LIKELY);
		m_trySyncOutput.SetGpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string Ethernet100BaseTXDecoder::GetProtocolName()
{
	return "Ethernet - 100baseTX";
}

bool Ethernet100BaseTXDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

Filter::DataLocation Ethernet100BaseTXDecoder::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet100BaseTXDecoder::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}

	//Make transfer helpers if this is the first time
	if(!m_cmdPool)
	{
		m_transferQueue = g_vkQueueManager->GetComputeQueue("Ethernet100BaseTXDecoder.queue");

		vk::CommandPoolCreateInfo poolInfo(
			vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			queue->m_family );
		m_cmdPool = make_unique<vk::raii::CommandPool>(*g_vkComputeDevice, poolInfo);

		vk::CommandBufferAllocateInfo bufinfo(**m_cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		m_transferCmdBuf = make_unique<vk::raii::CommandBuffer>(
			std::move(vk::raii::CommandBuffers(*g_vkComputeDevice, bufinfo).front()));
	}

	//Copy input timestamps to CPU as early as possible, so it can run while other processing is active
	m_transferCmdBuf->begin({});
	din->m_offsets.PrepareForCpuAccessNonblocking(*m_transferCmdBuf);
	din->m_durations.PrepareForCpuAccessNonblocking(*m_transferCmdBuf);
	m_transferCmdBuf->end();
	m_transferQueue->SubmitAndBlock(*m_transferCmdBuf);

	//MLT-3 decode and RX LFSR sync
	//A max-sized Ethernet frame is 1500 bytes (12000 bits, or 15000 after 4b5b coding)
	//TODO: this might occasionally fail to sync if a jumbo frame starts exactly when the trigger starts
	//Do we want to go larger?
	bool synced = false;
	size_t idle_offset = 0;
	const uint32_t maxOffset = 16384;
	if(g_hasShaderInt8)
	{
		const uint32_t threadsPerBlock = 64;
		const uint32_t numBlocks = maxOffset / threadsPerBlock;

		size_t ilen = din->size();
		cmdBuf.begin({});

		//Decode sampled analog voltages to MLT-3 symbols
		uint32_t nthreads = ilen - 1;
		m_phyBits.resize(nthreads);
		const uint32_t compute_block_count = GetComputeBlockCount(nthreads, 64);
		m_mlt3DecodeComputePipeline->BindBufferNonblocking(0, din->m_samples, cmdBuf);
		m_mlt3DecodeComputePipeline->BindBufferNonblocking(1, m_phyBits, cmdBuf, true);
		m_mlt3DecodeComputePipeline->Dispatch(cmdBuf, nthreads,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);
		m_mlt3DecodeComputePipeline->AddComputeMemoryBarrier(cmdBuf);
		m_phyBits.MarkModifiedFromGpu();

		//Then look for LFSR sync
		m_trySyncOutput.resize(maxOffset);
		m_trySyncComputePipeline->BindBufferNonblocking(0, m_phyBits, cmdBuf);
		m_trySyncComputePipeline->BindBufferNonblocking(1, m_trySyncOutput, cmdBuf, true);
		m_trySyncComputePipeline->Dispatch(cmdBuf, (uint32_t)din->size(), numBlocks);

		m_trySyncOutput.MarkModifiedFromGpu();
		m_trySyncOutput.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//Check results
		for(; idle_offset < maxOffset; idle_offset++)
		{
			if(m_trySyncOutput[idle_offset])
			{
				LogTrace("Got good LFSR sync at offset %zu\n", idle_offset);
				synced = true;
				break;
			}
		}
	}
	else
	{
		//MLT-3 decode
		DecodeStates(cmdBuf, queue, din);

		for(; idle_offset < maxOffset; idle_offset++)
		{
			if(TrySync(idle_offset))
			{
				LogTrace("Got good LFSR sync at offset %zu\n", idle_offset);
				synced = true;
				break;
			}
		}
	}

	//Make sure we got a good LFSR sync
	if(!synced)
	{
		LogTrace("Ethernet100BaseTXDecoder: Unable to sync RX LFSR\n");
		SetData(nullptr, 0);
		return;
	}

	//Good sync, descramble it now
	Descramble(cmdBuf, queue, idle_offset);

	//Copy our timestamps from the input. Output has femtosecond resolution since we sampled on clock edges
	//For now, hint the capture to not use GPU memory since none of our Ethernet decodes run on the GPU
	auto cap = SetupEmptyWaveform<EthernetWaveform>(din,0, true);
	cap->SetCpuOnlyHint();
	cap->Reserve(1000000);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	//Search until we find a 1100010001 (J-K, start of stream) sequence
	bool ssd[10] = {1, 1, 0, 0, 0, 1, 0, 0, 0, 1};
	size_t i = 0;
	bool hit = true;
	size_t des10 = m_descrambledBits.size() - 10;
	for(i=0; i<des10; i++)
	{
		hit = true;
		for(int j=0; j<10; j++)
		{
			bool b = m_descrambledBits[i+j];
			if(b != ssd[j])
			{
				hit = false;
				break;
			}
		}

		if(hit)
			break;
	}
	if(!hit)
	{
		LogTrace("No SSD found\n");
		return;
	}
	LogTrace("Found SSD at %zu\n", i);

	//Wait until all of the timestamps are ready
	m_transferQueue->WaitIdle();

	//Skip the J-K as we already parsed it
	i += 10;

	//4b5b decode table
	static const unsigned int code_5to4[]=
	{
		0, //0x00 unused
		0, //0x01 unused
		0, //0x02 unused
		0, //0x03 unused
		0, //0x04 = /H/, tx error
		0, //0x05 unused
		0, //0x06 unused
		0, //0x07 = /R/, second half of ESD
		0, //0x08 unused
		0x1,
		0x4,
		0x5,
		0, //0x0c unused
		0, //0x0d = /T/, first half of ESD
		0x6,
		0x7,
		0, //0x10 unused
		0, //0x11 = /K/, second half of SSD
		0x8,
		0x9,
		0x2,
		0x3,
		0xa,
		0xb,
		0, //0x18 = /J/, first half of SSD
		0, //0x19 unused
		0xc,
		0xd,
		0xe,
		0xf,
		0x0,
		0, //0x1f = idle
	};

	//Set of recovered bytes and timestamps
	vector<uint8_t> bytes;
	vector<uint64_t> starts;
	vector<uint64_t> ends;

	//Grab 5 bits at a time and decode them
	bool first = true;
	uint8_t current_byte = 0;
	uint64_t current_start = 0;
	size_t deslen = m_descrambledBits.size()-5;
	for(; i<deslen; i+=5)
	{
		unsigned int code =
			(m_descrambledBits[i+0] ? 16 : 0) |
			(m_descrambledBits[i+1] ? 8 : 0) |
			(m_descrambledBits[i+2] ? 4 : 0) |
			(m_descrambledBits[i+3] ? 2 : 0) |
			(m_descrambledBits[i+4] ? 1 : 0);

		//Handle special stuff
		if(code == 0x18)
		{
			//This is a /J/. Next code should be 0x11, /K/ - start of frame.
			//Don't check it for now, just jump ahead 5 bits and get ready to read data
			i += 5;
			continue;
		}
		else if(code == 0x04)
		{
			LogTrace("Found TX error at %zu\n", i);

			//TX error
			EthernetFrameSegment segment;
			segment.m_type = EthernetFrameSegment::TYPE_TX_ERROR;
			cap->m_offsets.push_back(current_start * cap->m_timescale);
			uint64_t end = din->m_offsets[idle_offset + i + 4] + din->m_durations[idle_offset + i + 4];
			cap->m_durations.push_back((end - current_start) * cap->m_timescale);
			cap->m_samples.push_back(segment);

			//reset for the next one
			starts.clear();
			ends.clear();
			bytes.clear();
			continue;
		}
		else if(code == 0x0d)
		{
			//This is a /T/. Next code should be 0x07, /R/ - end of frame.
			//Crunch this frame
			BytesToFrames(bytes, starts, ends, cap);

			//Skip the /R/
			i += 5;

			//and reset for the next one
			starts.clear();
			ends.clear();
			bytes.clear();
			continue;
		}

		//TODO: process /H/ - 0x04 (error in the middle of a packet)

		//Ignore idles
		else if(code == 0x1f)
			continue;

		//Nope, normal nibble.
		unsigned int decoded = code_5to4[code];
		if(first)
		{
			current_start = din->m_offsets[idle_offset + i];
			current_byte = decoded;
		}
		else
		{
			current_byte |= decoded << 4;

			bytes.push_back(current_byte);
			starts.push_back(current_start * cap->m_timescale);
			uint64_t end = din->m_offsets[idle_offset + i + 4] + din->m_durations[idle_offset + i + 4];
			ends.push_back(end * cap->m_timescale);
		}

		first = !first;
	}

	cap->MarkModifiedFromCpu();
}

void Ethernet100BaseTXDecoder::DecodeStates(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue,
	SparseAnalogWaveform* samples)
{
	size_t ilen = samples->size();

	samples->PrepareForCpuAccess();

	//TODO: some kind of sanity checking that voltage is changing in the right direction
	int oldstate = GetState(samples->m_samples[0]);
	m_phyBits.PrepareForCpuAccess();
	m_phyBits.resize(ilen-1);
	for(size_t i=1; i<ilen; i++)
	{
		int nstate = GetState(samples->m_samples[i]);

		//No transition? Add a "0" bit
		if(nstate == oldstate)
			m_phyBits[i-1] = false;

		//Transition? Add a "1" bit
		else
			m_phyBits[i-1] = true;

		oldstate = nstate;
	}

	m_phyBits.MarkModifiedFromCpu();

	//Grab the bits onto the CPU for future descrambling
	cmdBuf.begin({});
	m_phyBits.PrepareForCpuAccessNonblocking(cmdBuf);
	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);
}

/**
	@brief Try descrambling the first 64 bits at the requested offset and see if it makes sene
 */
bool Ethernet100BaseTXDecoder::TrySync(size_t idle_offset)
{
	//Bounds check
	const size_t searchWindow = 64;
	if( (idle_offset + searchWindow) >= m_phyBits.size())
		return false;

	//Assume the link is idle at the time we triggered, then see if we got it right
	unsigned int lfsr =
		( (!m_phyBits[idle_offset + 0]) << 10 ) |
		( (!m_phyBits[idle_offset + 1]) << 9 ) |
		( (!m_phyBits[idle_offset + 2]) << 8 ) |
		( (!m_phyBits[idle_offset + 3]) << 7 ) |
		( (!m_phyBits[idle_offset + 4]) << 6 ) |
		( (!m_phyBits[idle_offset + 5]) << 5 ) |
		( (!m_phyBits[idle_offset + 6]) << 4 ) |
		( (!m_phyBits[idle_offset + 7]) << 3 ) |
		( (!m_phyBits[idle_offset + 8]) << 2 ) |
		( (!m_phyBits[idle_offset + 9]) << 1 ) |
		( (!m_phyBits[idle_offset + 10]) << 0 );

	//We should have at least 64 "1" bits in a row once the descrambling is done.
	//The minimum inter-frame gap is a lot bigger than this.
	size_t start = idle_offset + 11;
	size_t stop = start + searchWindow;;
	for(size_t i=start; i < stop; i++)
	{
		bool c = ( (lfsr >> 8) ^ (lfsr >> 10) ) & 1;
		lfsr = (lfsr << 1) ^ c;

		//If it's not a 1 bit (idle character is all 1s), no go
		if( (m_phyBits[i] ^ c) != 1)
			return false;
	}

	//all good if we get here
	return true;
}

/**
	@brief Actually run the descrambler
 */
void Ethernet100BaseTXDecoder::Descramble(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue,
	size_t idle_offset)
{
	//Bounds check
	if( (idle_offset + 64) >= m_phyBits.size())
		return;

	size_t stop = m_phyBits.size();
	size_t start = idle_offset + 11;
	size_t len = stop - start;
	m_descrambledBits.resize(len);

	//GPU accelerated path
	if(g_hasShaderInt8)
	{
		const uint32_t numThreads = 4096;
		const uint32_t threadsPerBlock = 64;
		const uint32_t numBlocks = numThreads / threadsPerBlock;

		//If this is the first time, initialize the constant table
		if(m_lfsrTable.empty())
		{
			const uint32_t cols = 11;
			const uint32_t rows = 30;
			m_lfsrTable.resize(rows * cols);
			m_lfsrTable.PrepareForCpuAccess();
			for(uint32_t row=0; row<rows; row++)
			{
				for(uint32_t col=0; col<cols; col++)
					m_lfsrTable[row*cols + col] = g_lfsrTable[row][col];
			}
			m_lfsrTable.MarkModifiedFromCpu();
		}

		Ethernet100BaseTXDescramblerConstants cfg;
		cfg.len = m_phyBits.size();
		cfg.samplesPerThread = (len | (numThreads-1)) / numThreads;
		cfg.startOffset = start;

		cmdBuf.begin({});

		m_descrambleComputePipeline->BindBufferNonblocking(0, m_phyBits, cmdBuf);
		m_descrambleComputePipeline->BindBufferNonblocking(1, m_lfsrTable, cmdBuf);
		m_descrambleComputePipeline->BindBufferNonblocking(2, m_descrambledBits, cmdBuf, true);
		m_descrambleComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);

		m_descrambledBits.MarkModifiedFromGpu();
		m_descrambledBits.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
	}

	else
	{
		//Do everything CPU side
		m_descrambledBits.PrepareForCpuAccess();
		m_descrambledBits.MarkModifiedFromCpu();

		//Initial descrambler state
		unsigned int lfsr =
			( (!m_phyBits[idle_offset + 0]) << 10 ) |
			( (!m_phyBits[idle_offset + 1]) << 9 ) |
			( (!m_phyBits[idle_offset + 2]) << 8 ) |
			( (!m_phyBits[idle_offset + 3]) << 7 ) |
			( (!m_phyBits[idle_offset + 4]) << 6 ) |
			( (!m_phyBits[idle_offset + 5]) << 5 ) |
			( (!m_phyBits[idle_offset + 6]) << 4 ) |
			( (!m_phyBits[idle_offset + 7]) << 3 ) |
			( (!m_phyBits[idle_offset + 8]) << 2 ) |
			( (!m_phyBits[idle_offset + 9]) << 1 ) |
			( (!m_phyBits[idle_offset + 10]) << 0 );

		//Descramble
		size_t iout = 0;
		for(size_t i=start; i < stop; i++)
		{
			bool c = ( (lfsr >> 8) ^ (lfsr >> 10) ) & 1;
			lfsr = (lfsr << 1) ^ c;
			m_descrambledBits[iout] = m_phyBits[i] ^ c;
			iout ++;
		}
	}
}
