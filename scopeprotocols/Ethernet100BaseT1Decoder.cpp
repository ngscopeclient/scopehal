/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
{
	m_signalNames.clear();
	m_inputs.clear();

	CreateInput("i");
	CreateInput("q");
	CreateInput("clk");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string Ethernet100BaseT1Decoder::GetProtocolName()
{
	return "Ethernet - 100baseT1";
}

bool Ethernet100BaseT1Decoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet100BaseT1Decoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din_i = GetInputWaveform(0);
	auto din_q = GetInputWaveform(1);
	auto clk = GetInputWaveform(2);
	din_i->PrepareForCpuAccess();
	din_q->PrepareForCpuAccess();
	clk->PrepareForCpuAccess();

	//Sample the input on the edges of the recovered clock
	//TODO: if this is always coming from the IQDemuxFilter we can probably optimize this part out
	//and just iterate over i/q direct?
	SparseAnalogWaveform isamples;
	SparseAnalogWaveform qsamples;
	isamples.PrepareForCpuAccess();
	qsamples.PrepareForCpuAccess();
	SampleOnAnyEdgesBase(din_i, clk, isamples);
	SampleOnAnyEdgesBase(din_q, clk, qsamples);
	size_t ilen = min(isamples.size(), qsamples.size());

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
	float cutp = 0.35;
	float cutn = -0.35;
	Unit fs(Unit::UNIT_FS);

	//Copy our timestamps from the input. Output has femtosecond resolution since we sampled on clock edges
	auto cap = new EthernetWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = isamples.m_startTimestamp;
	cap->m_startFemtoseconds = isamples.m_startFemtoseconds;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	vector<uint8_t> bytes;
	vector<uint64_t> starts;
	vector<uint64_t> ends;

	int64_t bytestart = 0;
	uint8_t curByte = 0;
	uint8_t nbits = 0;
	bool scramblerLocked = false;

	uint64_t scrambler = 0;
	uint64_t idlesMatched = 0;

	uint64_t scramblerErrors = 0;
	size_t lastScramblerError = 0;

	for(size_t i=0; i<ilen; i++)
	{
		int64_t tnow = isamples.m_offsets[i];
		int64_t tlen = isamples.m_durations[i];

		//Decode raw symbols to 3-level constellation coordinates
		float fi = isamples.m_samples[i];
		float fq = qsamples.m_samples[i];
		int ci = 0;
		int cq = 0;
		if(fi > cutp)
			ci = 1;
		else if(fi < cutn)
			ci = -1;
		if(fq > cutp)
			cq = 1;
		else if(fq < cutn)
			cq = -1;

		//Advance the scrambler for each constellation point
		//Master: x^33 + x^13 + 1 (xor bits 12 and 32 then feed back into 0)
		//Slave: x^33 + x^20 + 1 (xor bits 19 and 32 then feed back into 0)
		//for now assume master mode
		auto b32 = (scrambler >> 32) & 1;
		auto b19 = (scrambler >> 19) & 1;
		auto b12 = (scrambler >> 12) & 1;
		bool masterMode = true;
		if(masterMode)
			scrambler = (scrambler << 1) | ( b32 ^ b12 );
		else
			scrambler = (scrambler << 1) | ( b32 ^ b19 );

		/*
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
					//96.3.4.4
					//RX scrambler (and optional polarity?) alignment
					//Note that 96.3.4.4 says to align assuming Sxn = 0:
					//    When Sd[0] is 0, I is nonzero
					//    When Sd[0] is 1, I is zero
					//But mid span I don't think we can rely on that being the case since we're not training?
					//That seems to be the case if we're in SEND_I, or in SEND_N with Sxn = 0

					//96.3.3.3.8 / table 96-3
					//(-1, -1), (0, -1), (0, 1), (1, 1) all mean Sd[0] == 1
					//else Sd[0] = 1
					//Unlike the algorithm in 96.3.4.4 this works for all modes
					//(SEND_I, or SEND_N with Sxn = 0 or Sxn=1)
					bool expected_lsb = ( (ci == -1) && (cq == -1) ) || (ci == 0) || ( (ci == 1) && (cq == 1) );

					//See if we already got the expected value out of the scrambler
					bool current_lsb = (scrambler & 1) == 1;

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

							LogTrace("Scrambler error at %s (%zu recently)\n",
								fs.PrettyPrint(isamples.m_offsets[i]).c_str(), scramblerErrors);

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
						LogTrace("Scrambler locked at %s\n", fs.PrettyPrint(isamples.m_offsets[i]).c_str());
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
						LogTrace("Found SSD at %s\n", fs.PrettyPrint(isamples.m_offsets[i]).c_str());

						//Add the fake preamble byte
						bytes.push_back(0x55);
						starts.push_back(bytestart);
						bytestart = tnow + (tlen * 2 / 3);
						ends.push_back(bytestart);

						//We're now 1 bit into the first preamble byte, which is always a 1
						nbits = 1;
						curByte = 1;
					}
					else
					{
						//TODO: can we go back in time once we achieve lock
						//and predict what the scrambler value had been to decode from the start of the waveform?
						LogTrace("Found SSD at %s, but can't decode because no scrambler lock\n",
							fs.PrettyPrint(isamples.m_offsets[i]).c_str());
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
					LogTrace("sd = %d curByte = %d nbits=%d\n", sd, curByte, nbits);

					//Descramble sd
					//Descrambler follows 40.3.1.4.2

					//Master and slave use separate 33-bit scrambler polynomials
					//Side-stream scrambler
					//Master: x^33 + x^13 + 1 (xor bits 12 and 32 then feed back into 0)
					//Slave: x^33 + x^20 + 1 (xor bits 19 and 32 then feed back into 0)

					//Each cycle generate 8 bits Sxn[3:0] and Syn[3:0]
					//Xn = xor of scrambler bits 4 and 6
					//Yn = xor of scrambler bits 1 and 5
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


		//figure 96-6a 4b -> 3b conversion
	}

	/*

	//Grab 5 bits at a time and decode them
	bool first = true;
	uint8_t current_byte = 0;
	uint64_t current_start = 0;
	size_t deslen = descrambled_bits.m_samples.size()-5;
	for(; i<deslen; i+=5)
	{
		unsigned int code =
			(descrambled_bits.m_samples[i+0] ? 16 : 0) |
			(descrambled_bits.m_samples[i+1] ? 8 : 0) |
			(descrambled_bits.m_samples[i+2] ? 4 : 0) |
			(descrambled_bits.m_samples[i+3] ? 2 : 0) |
			(descrambled_bits.m_samples[i+4] ? 1 : 0);

		//Handle special stuff
		if(code == 0x18)
		{
			//This is a /J/. Next code should be 0x11, /K/ - start of frame.
			//Don't check it for now, just jump ahead 5 bits and get ready to read data
			i += 5;
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
			current_start = descrambled_bits.m_offsets[i];
			current_byte = decoded;
		}
		else
		{
			current_byte |= decoded << 4;

			bytes.push_back(current_byte);
			starts.push_back(current_start * cap->m_timescale);
			uint64_t end = descrambled_bits.m_offsets[i+4] + descrambled_bits.m_durations[i+4];
			ends.push_back(end * cap->m_timescale);
		}

		first = !first;
	}
	*/
	cap->MarkModifiedFromCpu();
}
