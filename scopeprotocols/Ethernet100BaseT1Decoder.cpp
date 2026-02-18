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
#include "Ethernet100BaseT1Decoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet100BaseT1Decoder::Ethernet100BaseT1Decoder(const string& color)
	: EthernetProtocolDecoder(color)
	, m_scrambler(m_parameters["Scrambler polynomial"])
	, m_upperThresholdI(m_parameters["Threshold I+"])
	, m_upperThresholdQ(m_parameters["Threshold Q+"])
	, m_lowerThresholdI(m_parameters["Threshold I-"])
	, m_lowerThresholdQ(m_parameters["Threshold Q-"])
{
	m_signalNames.clear();
	m_inputs.clear();

	CreateInput("i");
	CreateInput("q");

	m_scrambler = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_scrambler.AddEnumValue("x^33 + x^13 + 1 (M)", SCRAMBLER_M_B13);
	m_scrambler.AddEnumValue("x^33 + x^20 + 1 (S)", SCRAMBLER_S_B19);
	m_scrambler.SetIntVal(SCRAMBLER_M_B13);

	m_upperThresholdI = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_upperThresholdQ = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_lowerThresholdI = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_lowerThresholdQ = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));

	m_upperThresholdI.SetFloatVal(0.4);
	m_upperThresholdQ.SetFloatVal(0.4);
	m_lowerThresholdI.SetFloatVal(-0.4);
	m_lowerThresholdQ.SetFloatVal(-0.4);

	if(g_hasShaderInt8 && g_hasPushDescriptor)
	{
		m_pointsI.SetGpuAccessHint(AcceleratorBuffer<int8_t>::HINT_LIKELY);
		m_pointsQ.SetGpuAccessHint(AcceleratorBuffer<int8_t>::HINT_LIKELY);

		m_pam3DecodeComputePipeline =
			make_shared<ComputePipeline>("shaders/Ethernet100BaseT1_PAM3Decoder.spv", 2, sizeof(PAM3DecodeConstants));

		if(g_hasShaderInt64)
		{
			m_descrambleComputePipeline =
				make_shared<ComputePipeline>("shaders/Ethernet100BaseT1_Descrambler.spv", 4, sizeof(BaseT1DescrambleConstants));
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string Ethernet100BaseT1Decoder::GetProtocolName()
{
	return "Ethernet - 100baseT1";
}

bool Ethernet100BaseT1Decoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

Filter::DataLocation Ethernet100BaseT1Decoder::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

void Ethernet100BaseT1Decoder::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("Ethernet100BaseT1Decoder::Refresh");
	#endif

	ClearPackets();

	//Get the input data
	auto din_i = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(0));
	auto din_q = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(1));

	//Make sure we've got valid inputs
	ClearErrors();
	if(!din_i || !din_q)
	{
		for(int i=0; i<2; i++)
		{
			if(!GetInput(i))
				AddErrorMessage("Missing inputs", string("No signal input connected to ") + m_signalNames[i] );
			else if(!GetInputWaveform(i))
				AddErrorMessage("Missing inputs", string("No waveform available at input ") + m_signalNames[i] );
			else
				AddErrorMessage("Invalid inputs", string("Expected sparse analog waveform at input ") + m_signalNames[i] );
		}

		SetData(nullptr, 0);
		return;
	}

	size_t ilen = min(din_i->size(), din_q->size());

	enum
	{
		STATE_IDLE,
		STATE_SSD_1,
		STATE_SSD_2,
		STATE_PACKET,
		STATE_ESD_1,
		STATE_ESD_2,
	} state = STATE_IDLE;

	//Decision thresholds
	//TODO: adaptive based on histogram?
	float cutip = m_upperThresholdI.GetFloatVal();
	float cutqp = m_upperThresholdQ.GetFloatVal();
	float cutin = m_lowerThresholdI.GetFloatVal();
	float cutqn = m_lowerThresholdQ.GetFloatVal();
	Unit fs(Unit::UNIT_FS);

	//Copy our timestamps from the input. Output has femtosecond resolution since we sampled on clock edges
	auto cap = SetupEmptyWaveform<EthernetWaveform>(din_i, 0);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	vector<uint8_t> bytes;
	vector<uint64_t> starts;
	vector<uint64_t> ends;

	int64_t bytestart = 0;
	uint16_t curNib = 0;
	uint8_t nbits = 0;
	bool scramblerLocked = false;

	uint64_t scrambler = 0;
	uint64_t idlesMatched = 0;

	size_t scramblerErrors = 0;
	size_t lastScramblerError = 0;

	uint8_t prevNib = 0;
	bool phaseLow = true;

	bool masterMode = (m_scrambler.GetIntVal() == SCRAMBLER_M_B13);

	//Decode raw symbols to 3-level constellation coordinates
	m_pointsI.resize(ilen);
	m_pointsQ.resize(ilen);
	if(g_hasShaderInt8 && g_hasPushDescriptor)
	{
		PAM3DecodeConstants cfgI;
		cfgI.nsamples = ilen;
		cfgI.cuthi = cutip;
		cfgI.cutlo = cutin;

		PAM3DecodeConstants cfgQ;
		cfgQ.nsamples = ilen;
		cfgQ.cuthi = cutqp;
		cfgQ.cutlo = cutqn;

		const uint32_t compute_block_count = GetComputeBlockCount(ilen, 64);

		cmdBuf.begin({});

			m_pam3DecodeComputePipeline->Bind(cmdBuf);

			//Copy I channel timestamps to CPU since we still need those for software decoding for now
			din_i->m_offsets.PrepareForCpuAccessNonblocking(cmdBuf);
			din_i->m_durations.PrepareForCpuAccessNonblocking(cmdBuf);

			//Decode I channel
			m_pam3DecodeComputePipeline->BindBufferNonblocking(0, din_i->m_samples, cmdBuf);
			m_pam3DecodeComputePipeline->BindBufferNonblocking(1, m_pointsI, cmdBuf, true);
			m_pam3DecodeComputePipeline->DispatchNoRebind(
				cmdBuf,
				cfgI,
				min(compute_block_count, 32768u),
				compute_block_count / 32768 + 1);
			m_pointsI.MarkModifiedFromGpu();

			//Decode Q channel
			m_pam3DecodeComputePipeline->BindBufferNonblocking(0, din_q->m_samples, cmdBuf);
			m_pam3DecodeComputePipeline->BindBufferNonblocking(1, m_pointsQ, cmdBuf, true);
			m_pam3DecodeComputePipeline->DispatchNoRebind(
				cmdBuf,
				cfgQ,
				min(compute_block_count, 32768u),
				compute_block_count / 32768 + 1);
			m_pointsQ.MarkModifiedFromGpu();

			//Copy points to CPU
			m_pointsI.PrepareForCpuAccessNonblocking(cmdBuf);
			m_pointsQ.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
	}
	else
	{
		//Set up input and output buffers
		cmdBuf.begin({});
			din_i->PrepareForCpuAccessNonblocking(cmdBuf);
			din_q->PrepareForCpuAccessNonblocking(cmdBuf);
			m_pointsI.PrepareForCpuAccessNonblocking(cmdBuf);
			m_pointsQ.PrepareForCpuAccessNonblocking(cmdBuf);
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//Decode raw symbols to 3-level constellation coordinates
		m_pointsI.PrepareForCpuAccess();
		m_pointsQ.PrepareForCpuAccess();
		for(size_t i=0; i<ilen; i++)
		{
			float fi = din_i->m_samples[i];
			float fq = din_q->m_samples[i];
			int ci = 0;
			int cq = 0;
			if(fi > cutip)
				ci = 1;
			else if(fi < cutin)
				ci = -1;
			if(fq > cutqp)
				cq = 1;
			else if(fq < cutqn)
				cq = -1;

			m_pointsI[i] = ci;
			m_pointsQ[i] = cq;
		}

		m_pointsI.MarkModifiedFromCpu();
		m_pointsQ.MarkModifiedFromCpu();
	}

	if(g_hasShaderInt64 && g_hasShaderInt8 && g_hasPushDescriptor)
	{
		cmdBuf.begin({});

		uint32_t	nthreads = 4096;
		uint32_t	threadsPerBlock = 64;
		uint32_t	compute_block_count = GetComputeBlockCount(nthreads, threadsPerBlock);

		//We have 3 bits per I/Q symbol pair
		//A minimum sized Ethernet frame is 64 bytes (512 bits) or 171 triplets long plus start/stop symbols etc
		uint32_t	maxFramesInCapture = din_i->size() / 171;

		BaseT1DescrambleConstants cfg;
		cfg.len = ilen;
		cfg.samplesPerThread = GetComputeBlockCount(ilen, nthreads);
		cfg.maxOutputPerThread = GetComputeBlockCount(maxFramesInCapture, nthreads) + 1;
		cfg.masterMode = masterMode;

		//Allocate scratch space
		ScratchBuffer_uint32_t tmp_vstarts(ScratchBufferManager::U32_GPU_WAVEFORM);
		ScratchBuffer_int64_t tmp_vscramblers(ScratchBufferManager::I64_GPU_WAVEFORM);
		tmp_vstarts->resize(cfg.maxOutputPerThread * nthreads);
		tmp_vscramblers->resize(cfg.maxOutputPerThread * nthreads);

		//Run the shader
		m_descrambleComputePipeline->BindBufferNonblocking(0, m_pointsI, cmdBuf);
		m_descrambleComputePipeline->BindBufferNonblocking(1, m_pointsQ, cmdBuf);
		m_descrambleComputePipeline->BindBufferNonblocking(2, *tmp_vstarts, cmdBuf, true);
		m_descrambleComputePipeline->BindBufferNonblocking(3, *tmp_vscramblers, cmdBuf, true);

		m_descrambleComputePipeline->Dispatch(
			cmdBuf,
			cfg,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);

		tmp_vstarts->MarkModifiedFromGpu();
		tmp_vscramblers->MarkModifiedFromGpu();

		//Grab the output
		tmp_vstarts->PrepareForCpuAccessNonblocking(cmdBuf);
		tmp_vscramblers->PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//CPU side coalescing
		auto pstart = tmp_vstarts->GetCpuPointer();
		auto pscramble = tmp_vscramblers->GetCpuPointer();
		vector< pair<uint32_t, int64_t> > packets;
		for(size_t i=0; i<nthreads; i++)
		{
			uint32_t outbase = i * cfg.maxOutputPerThread;

			LogIndenter li;
			size_t numEntries = pstart[outbase];
			if(numEntries != 0)
			{
				for(size_t j=0; j<numEntries; j++)
				{
					packets.push_back(pair<uint32_t, int64_t>(
						pstart[outbase + j + 1],
						pscramble[outbase + j + 1]));
				}
			}
		}

		//Decode each packet that the GPU found
		for(auto pinfo : packets)
		{
			scrambler = pinfo.second;
			size_t i = pinfo.first;
			int64_t tnow = din_i->m_offsets[i];
			int64_t tlen = din_i->m_durations[i];

			//Add the fake preamble byte
			bytestart = din_i->m_offsets[i-2];
			bytes.push_back(0x55);
			starts.push_back(bytestart);
			bytestart = tnow + (tlen * 2 / 3);
			ends.push_back(bytestart);

			//We're now 1 bit into the first preamble byte, which is always a 1
			nbits = 1;
			curNib = 1;

			prevNib = 0;
			phaseLow = true;
			state = STATE_PACKET;
			scramblerLocked = true;

			i++;

			//Decode just this one packet
			for(; (i<ilen) && (state != STATE_IDLE); i++)
			{
				tnow = din_i->m_offsets[i];
				tlen = din_i->m_durations[i];

				int ci = m_pointsI[i];
				int cq = m_pointsQ[i];

				//Advance the scrambler for each constellation point
				auto b32 = (scrambler >> 32) & 1;
				auto b19 = (scrambler >> 19) & 1;
				auto b12 = (scrambler >> 12) & 1;
				if(masterMode)
					scrambler = (scrambler << 1) | ( b32 ^ b12 );
				else
					scrambler = (scrambler << 1) | ( b32 ^ b19 );

				//Extract scrambler bits we care about for the data bits
				auto b16 = (scrambler >> 16) & 1;
				bool b8 = (scrambler >> 8) & 1;
				bool b6 = (scrambler >> 6) & 1;
				bool b3 = (scrambler >> 3) & 1;
				bool b0 = (scrambler & 1);

				switch(state)
				{
					case STATE_PACKET:

						//Look for ESD
						//96.3.3.3.5
						if( (ci == 0) && (cq == 0) )
							state = STATE_ESD_1;

						//No, it's a data symbol
						else if(scramblerLocked)
						{
							//Decode to a sequence of 3 scrambled data bits
							uint8_t sd = 0;
							switch(ci)
							{
								//-1 -> 3'b000, 0 = 3'b001, 1 = 3'b010
								case -1:
									sd = cq + 1;
									break;

								//Q=0 is disallowed here
								case 0:
									if(cq == -1)
										sd = 3;
									else //if(cq == 1)
										sd = 4;
									break;

								//-1 -> 3'b101, 0 -> 3'b110, 1 -> 3'b111
								case 1:
									sd = cq + 6;

								default:
									break;
							}

							/*
								Descramble sd per 40.3.1.4.2

								Sy0 = scr0
								sy1 = scr3 ^ 8
								sy2 = scr6 ^ 16
								sy3 = scr9 ^ 14 ^ 19 ^ 24

								sx0 = scr4 ^ scr6
								sx1 = scr7 ^ 9 ^ 12 ^ 14
								sx2 = scr10 ^ 12 ^ 20 ^ 22
								sx3 = scr13 ^ 15 ^ 18 ^ 20 ^ 23 ^ 25 ^ 28 ^ 30

								scrambler for 7:4 is sx
								scrambler for 3:0 is sy
							 */
							bool sy0 = b0;
							bool sy1 = b3 ^ b8;
							bool sy2 = b6 ^ b16;
							sd ^= (sy2 << 2) ^ (sy1 << 1) ^ sy0;

							//Add the 3 descrambled bits into the current nibble
							curNib |= (sd << nbits);
							nbits += 3;

							//At this point we should have 3, 4, 5, or 6 bits in the current nibble-in-progress.
							//If we have at least a whole nibble, process it
							if(nbits >= 4)
							{
								uint8_t nib = curNib & 0xf;

								curNib >>= 4;
								nbits -= 4;

								//Combine nibbles into bytes
								if(!phaseLow)
								{
									uint8_t bval = (nib << 4) | prevNib;

									//Add the byte
									bytes.push_back(bval);
									starts.push_back(bytestart);

									//Byte end time depends on how many bits from this symbol weren't consumed
									bytestart = tnow + (tlen * (2 - nbits) / 3);

									ends.push_back(bytestart);
								}

								prevNib = nib;
								phaseLow = !phaseLow;
							}
						}

						break;

					case STATE_ESD_1:

						//Look for ESD, bail if malformed
						if( (ci == 0) && (cq == 0) )
							state = STATE_ESD_2;
						else
							state = STATE_IDLE;

						break;

					case STATE_ESD_2:

						//Good ESD, decode what we got
						if( (ci == 1) && (cq == 1) )
							BytesToFrames(bytes, starts, ends, cap);

						//ESD with error
						//TODO: how to handle this? for now decode it anyway
						else if( (ci == -1) && (cq == -1) )
						{
							if(scramblerLocked)
								BytesToFrames(bytes, starts, ends, cap);
						}

						//invalid, don't try to decode
						else
						{}

						//Either way, clear frame data and move on
						bytes.clear();
						starts.clear();
						ends.clear();
						state = STATE_IDLE;
						break;

					default:
						break;
				}
			}
		}
	}

	else
	{
		size_t totalErrorsReported = 0;
		for(size_t i=0; i<ilen; i++)
		{
			int64_t tnow = din_i->m_offsets[i];
			int64_t tlen = din_i->m_durations[i];

			int ci = m_pointsI[i];
			int cq = m_pointsQ[i];

			//Advance the scrambler for each constellation point
			auto b32 = (scrambler >> 32) & 1;
			auto b19 = (scrambler >> 19) & 1;
			auto b12 = (scrambler >> 12) & 1;
			if(masterMode)
				scrambler = (scrambler << 1) | ( b32 ^ b12 );
			else
				scrambler = (scrambler << 1) | ( b32 ^ b19 );

			//Extract scrambler bits we care about for the data bits
			auto b16 = (scrambler >> 16) & 1;
			bool b8 = (scrambler >> 8) & 1;
			bool b6 = (scrambler >> 6) & 1;
			bool b3 = (scrambler >> 3) & 1;
			bool b0 = (scrambler & 1);

			switch(state)
			{
				//Look for three (0,0) points in a row to indicate SSD
				//96.3.3.3.5
				case STATE_IDLE:
					if( (ci == 0) && (cq == 0) )
					{
						state = STATE_SSD_1;
						bytestart = tnow;
					}

					//Not a SSD, it's idles.
					else
					{
						/*
							96.3.4.4
							RX scrambler (and optional polarity?) alignment
							Note that 96.3.4.4 says to align assuming Sxn = 0:
								When Sd[0] is 0, I is nonzero
								When Sd[0] is 1, I is zero
							But mid span I don't think we can rely on that being the case since we're not training?
							That seems to be the case if we're in SEND_I, or in SEND_N with Sxn = 0

							96.3.3.3.8 / table 96-3
							(-1, -1), (0, -1), (0, 1), (1, 1) all mean Sd[0] == 1
							else Sd[0] = 1
							Unlike the algorithm in 96.3.4.4 this works for all modes
							(SEND_I, or SEND_N with Sxn = 0 or Sxn=1)

							(-1, -1): Sx = 1, Sd = 3'b1x1
							(-1,  0): Sx = x, Sd = 3'b000
							(-1,  1): Sx = x, Sd = 3'b010

							( 0, -1): Sx = 0, Sd = 3'b1x1
							( 0,  0): SSD, not an idle
							( 0,  1): Sx = 0, Sd = 3'b0x1

							( 1, -1): Sx = x, Sd = 3'b110
							( 1,  0): Sx = x, Sd = 3'b100
							( 1,  1): Sx = 1, Sd = 3'b0x1
						 */
						bool expected_lsb = ( (ci == -1) && (cq == -1) ) || (ci == 0) || ( (ci == 1) && (cq == 1) );

						//See if we already got the expected value out of the scrambler
						bool current_lsb = (b0 == 1);

						//Yes? We got more idles
						if(expected_lsb == current_lsb)
						{
							idlesMatched ++;

							//Clear scrambler error counter after 1K error-free bits
							if(lastScramblerError > 1024)
							{
								lastScramblerError = 0;
								scramblerErrors = 0;
							}
						}

						//Nope, reset idle counter and force this bit into the scrambler
						else
						{
							//Was scrambler locked? We might have lost sync but give some tolerance to bit errors
							if(scramblerLocked)
							{
								lastScramblerError = i;
								scramblerErrors ++;
								totalErrorsReported ++;

								if(totalErrorsReported < 32)
								{
									LogTrace("Scrambler error at %s (%zu recently)\n",
										fs.PrettyPrint(din_i->m_offsets[i]).c_str(), scramblerErrors);
									if(totalErrorsReported == 31)
										LogTrace("Not reporting any more scrambler errors\n");
								}

								if(scramblerErrors > 16)
								{
									LogTrace("Scrambler unlocked\n");
									scramblerLocked = false;
								}
							}

							//No, unlocked. Feed data in and try to get a lock
							else
							{
								idlesMatched = 0;
								scrambler = (scrambler & ~1) | expected_lsb;
							}
						}

						//Declare lock after 256 error-free idles
						if( (idlesMatched > 256) && !scramblerLocked)
						{
							LogTrace("Scrambler locked at %s (%zu)\n",
								fs.PrettyPrint(din_i->m_offsets[i]).c_str(), i);
							scramblerLocked = true;
							scramblerErrors = 0;
							lastScramblerError = i;
						}
					 }

					break;

				case STATE_SSD_1:
					if( (ci == 0) && (cq == 0) )
						state = STATE_SSD_2;
					else
						state = STATE_IDLE;
					break;

				case STATE_SSD_2:
					if( (ci == 0) && (cq == 0) )
					{
						state = STATE_PACKET;

						if(scramblerLocked)
						{
							LogDebug("Found SSD at sample %zu (scrambler = %016lx)\n", i, scrambler);

							LogTrace("Found SSD at %s\n", fs.PrettyPrint(din_i->m_offsets[i]).c_str());

							//Add the fake preamble byte
							bytes.push_back(0x55);
							starts.push_back(bytestart);
							bytestart = tnow + (tlen * 2 / 3);
							ends.push_back(bytestart);

							//We're now 1 bit into the first preamble byte, which is always a 1
							nbits = 1;
							curNib = 1;

							prevNib = 0;
							phaseLow = true;
						}
						else
						{
							//TODO: can we go back in time once we achieve lock
							//and predict what the scrambler value had been to decode from the start of the waveform?
							LogTrace("Found SSD at %s, but can't decode because no scrambler lock\n",
								fs.PrettyPrint(din_i->m_offsets[i]).c_str());
						}
					}
					else
						state = STATE_IDLE;
					break;

				case STATE_PACKET:

					//Look for ESD
					//96.3.3.3.5
					if( (ci == 0) && (cq == 0) )
						state = STATE_ESD_1;

					//No, it's a data symbol
					else if(scramblerLocked)
					{
						//Decode to a sequence of 3 scrambled data bits
						uint8_t sd = 0;
						switch(ci)
						{
							//-1 -> 3'b000, 0 = 3'b001, 1 = 3'b010
							case -1:
								sd = cq + 1;
								break;

							//Q=0 is disallowed here
							case 0:
								if(cq == -1)
									sd = 3;
								else //if(cq == 1)
									sd = 4;
								break;

							//-1 -> 3'b101, 0 -> 3'b110, 1 -> 3'b111
							case 1:
								sd = cq + 6;

							default:
								break;
						}

						/*
							Descramble sd per 40.3.1.4.2

							Sy0 = scr0
							sy1 = scr3 ^ 8
							sy2 = scr6 ^ 16
							sy3 = scr9 ^ 14 ^ 19 ^ 24

							sx0 = scr4 ^ scr6
							sx1 = scr7 ^ 9 ^ 12 ^ 14
							sx2 = scr10 ^ 12 ^ 20 ^ 22
							sx3 = scr13 ^ 15 ^ 18 ^ 20 ^ 23 ^ 25 ^ 28 ^ 30

							scrambler for 7:4 is sx
							scrambler for 3:0 is sy
						 */
						bool sy0 = b0;
						bool sy1 = b3 ^ b8;
						bool sy2 = b6 ^ b16;
						sd ^= (sy2 << 2) ^ (sy1 << 1) ^ sy0;

						//Add the 3 descrambled bits into the current nibble
						curNib |= (sd << nbits);
						nbits += 3;

						//At this point we should have 3, 4, 5, or 6 bits in the current nibble-in-progress.
						//If we have at least a whole nibble, process it
						if(nbits >= 4)
						{
							uint8_t nib = curNib & 0xf;

							curNib >>= 4;
							nbits -= 4;

							//Combine nibbles into bytes
							if(!phaseLow)
							{
								uint8_t bval = (nib << 4) | prevNib;

								//Add the byte
								bytes.push_back(bval);
								starts.push_back(bytestart);

								//Byte end time depends on how many bits from this symbol weren't consumed
								bytestart = tnow + (tlen * (2 - nbits) / 3);

								ends.push_back(bytestart);
							}

							prevNib = nib;
							phaseLow = !phaseLow;
						}
					}

					break;

				case STATE_ESD_1:

					//Look for ESD, bail if malformed
					if( (ci == 0) && (cq == 0) )
						state = STATE_ESD_2;
					else
						state = STATE_IDLE;

					break;

				case STATE_ESD_2:

					//Good ESD, decode what we got
					if( (ci == 1) && (cq == 1) )
						BytesToFrames(bytes, starts, ends, cap);

					//ESD with error
					//TODO: how to handle this? for now decode it anyway
					else if( (ci == -1) && (cq == -1) )
					{
						if(scramblerLocked)
							BytesToFrames(bytes, starts, ends, cap);
					}

					//invalid, don't try to decode
					else
					{}

					//Either way, clear frame data and move on
					bytes.clear();
					starts.clear();
					ends.clear();
					state = STATE_IDLE;
					break;
			}
		}
	}

	cap->MarkModifiedFromCpu();
}
