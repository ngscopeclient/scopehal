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
#include "Ethernet100BaseT1LinkTrainingDecoder.h"
#include "SPIDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet100BaseT1LinkTrainingDecoder::Ethernet100BaseT1LinkTrainingDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
	, m_scrambler("Scrambler polynomial")
{
	CreateInput("i");
	CreateInput("q");
	CreateInput("clk");

	AddProtocolStream("data");

	m_parameters[m_scrambler] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_scrambler].AddEnumValue("x^33 + x^13 + 1 (M)", Ethernet100BaseT1Decoder::SCRAMBLER_M_B13);
	m_parameters[m_scrambler].AddEnumValue("x^33 + x^20 + 1 (S)", Ethernet100BaseT1Decoder::SCRAMBLER_S_B19);
	m_parameters[m_scrambler].SetIntVal(Ethernet100BaseT1Decoder::SCRAMBLER_M_B13);
}

Ethernet100BaseT1LinkTrainingDecoder::~Ethernet100BaseT1LinkTrainingDecoder()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool Ethernet100BaseT1LinkTrainingDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string Ethernet100BaseT1LinkTrainingDecoder::GetProtocolName()
{
	return "Ethernet - 100baseT1 Link Training";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet100BaseT1LinkTrainingDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
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
		STATE_SEND_Z,
		STATE_SEND_I_UNLOCKED,
		STATE_SEND_I_LOCKED,
		STATE_SEND_N
	} state = STATE_SEND_Z;

	//Decision thresholds
	//TODO: adaptive based on histogram?
	float cutp = 0.35;
	float cutn = -0.35;

	//Copy our timestamps from the input. Output has femtosecond resolution since we sampled on clock edges
	auto cap = new Ethernet100BaseT1LinkTrainingWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = isamples.m_startTimestamp;
	cap->m_startFemtoseconds = isamples.m_startFemtoseconds;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	bool masterMode = (m_parameters[m_scrambler].GetIntVal() == Ethernet100BaseT1Decoder::SCRAMBLER_M_B13);

	uint64_t scrambler = 0;
	uint64_t idlesMatched = 0;
	size_t lastScramblerError = 0;

	//Add initial sample assuming we're in SEND_Z mode
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(0);
	cap->m_samples.push_back(Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_Z));

	size_t numZeroes = 0;

	for(size_t i=0; i<ilen; i++)
	{
		int64_t tnow = isamples.m_offsets[i];
		int64_t tlen = isamples.m_durations[i];
		size_t nlast = cap->size() - 1;

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
		auto b32 = (scrambler >> 32) & 1;
		auto b19 = (scrambler >> 19) & 1;
		auto b12 = (scrambler >> 12) & 1;
		if(masterMode)
			scrambler = (scrambler << 1) | ( b32 ^ b12 );
		else
			scrambler = (scrambler << 1) | ( b32 ^ b19 );

		bool b0 = (scrambler & 1);

		//Extract Sd[0] from the I value in SEND_I mode
		//I=0 means Sd[0] = 1
		//I=+1 or -1 means Sd[0] = 0
		bool expected_lsb_sendi = (ci == 0);

		//Expected LSB in SEND-N mode (assuming no frames are showing up)
		bool expected_lsb_sendn = ( (ci == -1) && (cq == -1) ) || (ci == 0) || ( (ci == 1) && (cq == 1) );

		//See if we already got the expected value out of the scrambler
		bool current_lsb = (b0 == 1);

		const int minIdlesForLock = 256;

		switch(state)
		{
			//Sending zeroes
			case STATE_SEND_Z:

				numZeroes = 0;

				//(0,0) in SEND_Z state means we're still in SEND_Z
				if( (ci == 0) && (cq == 0) )
				{
					//Assume for now that the previous sample is SEND_Z so we can just extend it
					cap->m_durations[nlast] = (tnow + tlen) - cap->m_offsets[nlast];
				}

				//Anything else means we are probably transitioning to SEND_I
				else
				{
					//Extend the SEND_Z sample to the start of this one
					//Assume for now that the previous sample is SEND_Z so we can just extend it
					cap->m_durations[nlast] = tnow - cap->m_offsets[nlast];

					cap->m_offsets.push_back(tnow);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(
						Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_I_UNLOCKED));

					state = STATE_SEND_I_UNLOCKED;
					idlesMatched = 0;
				}

				break;	//STATE_SEND_Z

			//SEND_I but decode isn't yet locked to scrambler
			case STATE_SEND_I_UNLOCKED:

				//Yes? We got more idles
				if(expected_lsb_sendi == current_lsb)
				{
					idlesMatched ++;

					//Clear scrambler error counter after 1K error-free bits
					if(lastScramblerError > 1024)
						lastScramblerError = 0;
				}

				//Nope, reset idle counter and force this bit into the scrambler
				else
				{
					idlesMatched = 0;
					scrambler = (scrambler & ~1) | expected_lsb_sendi;
				}

				//Declare lock after 256 error-free idles
				//But we can back up and declare the lock as beginning at that point.
				if(idlesMatched >= minIdlesForLock)
				{
					//LogTrace("Scrambler locked at %s\n", fs.PrettyPrint(isamples.m_offsets[i]).c_str());

					//Retcon the SEND_I_UNLOCKED to end when we got our first good idle
					int64_t tlock = isamples.m_offsets[i - idlesMatched];
					cap->m_durations[nlast] = tlock - cap->m_offsets[nlast];

					//We're now locked
					cap->m_offsets.push_back(tlock);
					cap->m_durations.push_back(tnow - tlock);
					cap->m_samples.push_back(
						Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_I_LOCKED));
					state = STATE_SEND_I_LOCKED;

					lastScramblerError = i;
				}
				break;	//STATE_SEND_I_UNLOCKED

			//SEND_I and in locked state
			case STATE_SEND_I_LOCKED:

				//If we get the expected result for SEND_I, extend the SEND_I state
				if(expected_lsb_sendi == current_lsb)
					cap->m_durations[nlast] = (tnow + tlen) - cap->m_offsets[nlast];

				//If we get the expected result for SEND_N, jump to SEND_N
				else if(expected_lsb_sendn == current_lsb)
				{
					//End the SEND_I symbol here
					cap->m_durations[nlast] = tnow - cap->m_offsets[nlast];

					//Add the SEND_N symbol
					cap->m_offsets.push_back(tnow);
					cap->m_durations.push_back(tlen);
					cap->m_samples.push_back(
						Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_N));

					state = STATE_SEND_N;
				}

				//If we get neither, add an error symbol
				else
				{
					//End the SEND_I symbol here
					cap->m_durations[nlast] = tnow - cap->m_offsets[nlast];

					//Add the error symbol
					cap->m_offsets.push_back(tnow);
					cap->m_durations.push_back(tlen);
					cap->m_samples.push_back(
						Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_ERROR));

					//Add a new SEND_I symbol
					cap->m_offsets.push_back(tnow + tlen);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(
						Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_I_LOCKED));
				}

				break;

			//SEND_N: TODO handle packets showing up
			case STATE_SEND_N:

				if(expected_lsb_sendn == current_lsb)
					cap->m_durations[nlast] = (tnow + tlen) - cap->m_offsets[nlast];

				else
				{
					//End the SEND_N symbol here
					cap->m_durations[nlast] = tnow - cap->m_offsets[nlast];

					//Add the error symbol
					cap->m_offsets.push_back(tnow);
					cap->m_durations.push_back(tlen);
					cap->m_samples.push_back(
						Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_ERROR));

					//Add a new SEND_I symbol
					cap->m_offsets.push_back(tnow + tlen);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(
						Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_N));
				}

				break;

			default:
				break;
		}

		//Reset to SEND_Z after a bunch of zeroes in a row
		if(state != STATE_SEND_Z)
		{
			if( (ci == 0) && (cq == 0) )
				numZeroes ++;

			if(numZeroes >= 10)
			{
				//Add a new SEND_I symbol
				cap->m_offsets.push_back(tnow + tlen);
				cap->m_durations.push_back(0);
				cap->m_samples.push_back(
					Ethernet100BaseT1LinkTrainingSymbol(Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_Z));

				state = STATE_SEND_Z;
			}
		}
	}

	cap->MarkModifiedFromCpu();
}

string Ethernet100BaseT1LinkTrainingWaveform::GetColor(size_t i)
{
	const Ethernet100BaseT1LinkTrainingSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_Z:
			return StandardColors::colors[StandardColors::COLOR_IDLE];

		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_I_UNLOCKED:
		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_I_LOCKED:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_N:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string Ethernet100BaseT1LinkTrainingWaveform::GetText(size_t i)
{
	const Ethernet100BaseT1LinkTrainingSymbol& s = m_samples[i];
	char tmp[128];

	switch(s.m_type)
	{
		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_Z:
			return "SEND_Z";

		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_I_UNLOCKED:
			return "SEND_I (scrambler unlocked)";
		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_I_LOCKED:
			return "SEND_I";

		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_SEND_N:
			return "SEND_N";

		case Ethernet100BaseT1LinkTrainingSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}

	return string(tmp);
}
