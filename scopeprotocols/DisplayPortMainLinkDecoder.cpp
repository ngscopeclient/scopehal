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
#include "DisplayPortMainLinkDecoder.h"
#include "IBM8b10bWaveform.h"

using namespace std;

const uint8_t g_bitswapTable[] =
{
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DisplayPortMainLinkDecoder::DisplayPortMainLinkDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
{
	AddProtocolStream("data");

	//Add inputs. We take a single 8b10b coded stream
	CreateInput<InputConstraintWaveformType<IBM8b10bWaveform> >("data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DisplayPortMainLinkDecoder::GetProtocolName()
{
	return "DisplayPort - Main Link";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

//TODO table driven/faster impl
uint8_t DisplayPortMainLinkDecoder::RunScrambler(uint16_t& state)
{
	uint8_t ret = 0;

	for(int j=0; j<8; j++)
	{
		bool b = (state & 0x8000) ? true : false;
		ret >>= 1;

		if(b)
		{
			ret |= 0x80;
			state ^= 0x1c;
		}
		state = (state << 1) | b;
	}

	return ret;
}

/**
	@brief Advances the scrambler by a single bit. This is the same scrambler polynomial as PCIe.
 */
bool DisplayPortMainLinkDecoder::RunScramblerSingle(uint16_t& state)
{
	LogDebug("RunScramblerSingle state=%04x\n", state);
	LogIndenter li;

	bool b = (state & 0x8000) ? true : false;
	if(b)
		state ^= 0x1c;
	state = (state << 1) | b;

	LogDebug("final state = %04x, b = %d\n", state, b);
	return b;
}

void DisplayPortMainLinkDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("DisplayPortMainLinkDecoder::Refresh");
	#endif

	auto data = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(0));

	//Make sure we've got valid inputs
	ClearMessages();
	if(!data)
	{
		AddErrorMessage("Missing inputs", "No waveform available at input");
		SetData(nullptr, 0);
		return;
	}

	//Create output waveform
	auto cap = SetupEmptyWaveform<DPMainLinkWaveform>(data, 0);

	//Handle the actual decoding
	//TODO: multi lane support
	size_t len = data->size();
	bool scramblerLocked = false;
	bool frameLocked = false;
	uint16_t scrambleState = 1;
	uint32_t debugCount = 0;

	for(size_t i=0; i<len; i++)
	{
		auto sym = data->m_samples[i];

		auto tbase = data->m_offsets[i];
		auto tstart = (data->m_timescale * tbase) + data->m_triggerPhase;

		//Scrambler advances on all symbols even K characters
		uint8_t scram = 0;
		if(scramblerLocked)
			RunScrambler(scrambleState);

		//Descramble it, if applicable
		uint8_t descrambled = sym.m_data ^ scram;
		/*if(scramblerLocked)
		{
			LogDebug("%02x\n", descrambled);
			debugCount ++;
			if(debugCount > 30)
				break;
		}*/

		//Look for control characters
		bool isControl = (sym.m_flags & IBM8b10bSymbol::FLAG_CONTROL) == IBM8b10bSymbol::FLAG_CONTROL;

		if(isControl)
		{
			//K28.0 is start of a scrambler reset
			if(sym.m_data == 0x1c)
			{
				//Expect SR BF BF SR = K28.0 K28.3 K28.3 K28.0 = 1c 7c 7c 1c
				/*
				LogDebug("Found SR at %s\n", Unit(Unit::UNIT_FS).PrettyPrint(tstart).c_str());
				i += 4;
				scrambleState = 0xffff;
				scramblerLocked = true;*/

				//Descramble the first few values
				//i++;
				/*
				for(size_t j=0; j<32; j++)
				{
					auto b = data->m_samples[i+j].m_data;
					auto d = RunScrambler(scrambleState);
					auto s = b ^ d;
					LogDebug("%02x, %02x, %02x\n", b, d, s);
				}
				return;
				*/

				frameLocked = true;
			}

			//BS (start of blanking period)
			//BS BF BF BS = K28.5 K28.3 K28.3 K28.5 = bc 7c 7c bc
			else if(sym.m_data == 0xbc)
			{
				if(!scramblerLocked)
					continue;

				//If framing isn't locked, add a filler symbol
				if(!frameLocked)
				{
					//scrambler locked implies we have something in the capture already
					//so no check needed
					auto nend = cap->m_offsets.size() - 1;
					auto tend = cap->m_offsets[nend] + cap->m_durations[nend];

					cap->m_offsets.push_back(tend);
					cap->m_durations.push_back(tbase - tend);
					cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_FRAME_DESYNC));
				}

				//TODO: verify next 3 symbols are BF BF BS

				LogDebug("Found BS at %s\n", Unit(Unit::UNIT_FS).PrettyPrint(tstart).c_str());
				frameLocked = true;

				//Add blanking-start symbol
				cap->m_offsets.push_back(tbase);
				cap->m_durations.push_back(data->m_offsets[i+3] + data->m_durations[i+3] - tbase);
				cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_BS));

				//Run scrambler and skip the next 3 symbols
				for(size_t j=0; j<3; j++)
					RunScrambler(scrambleState);
				i += 3;

				//TODO: decode the blanking interval data
			}

			//BE (end of blanking period, start pixel data)
			//0xfb = k27.7 = BE
			else if(sym.m_data == 0xfb)
			{
				if(!scramblerLocked)
					continue;

				LogDebug("Found BE at %s\n", Unit(Unit::UNIT_FS).PrettyPrint(tstart).c_str());

				//i++;
				/*
				for(size_t j=0; j<64; j++)
				{
					auto b = data->m_samples[i+j].m_data;
					auto d = RunScrambler(scrambleState);
					auto s = b ^ d;
					LogDebug("%02x, %02x, %02x\n", b, d, s);
				}
				return;*/
			}

			//Fill
			//Expect FS ... FE = K30.7 .... K23.7 = fe .... f7
			else if(sym.m_data == 0xfe)
			{
				//See how many fill characters we have before the FE
				size_t iFirstFill = i+1;
				size_t iLastFill = iFirstFill;
				for(size_t j=iFirstFill; j < len; j++)
				{
					auto nsym = data->m_samples[j];
					if(nsym.m_flags & IBM8b10bSymbol::FLAG_CONTROL)
					{
						//TODO: throw error if the end of the fill is not a K23.7
						break;
					}
					else
						iLastFill = j;
				}
				size_t numFillSymbols = iLastFill - iFirstFill;

				//If scrambler was not locked, add a filler symbol
				if(!scramblerLocked)
				{
					cap->m_offsets.push_back(data->m_offsets[0]);
					cap->m_durations.push_back(tbase - data->m_offsets[0]);
					cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_SCRAMBLER_DESYNC));
				}

				//If scrambler was locked, but framing was not, add a filler symbol
				else if(!frameLocked)
				{
					//scrambler locked implies we have something in the capture already
					//so no check needed
					auto nend = cap->m_offsets.size() - 1;
					auto tend = cap->m_offsets[nend] + cap->m_durations[nend];

					cap->m_offsets.push_back(tend);
					cap->m_durations.push_back(tbase - tend);
					cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_FRAME_DESYNC));
				}

				//Add fill symbol to the timeline
				size_t iFE = iLastFill + 1;
				if(iFE >= len)
					iFE = len - 1;
				cap->m_offsets.push_back(tbase);
				cap->m_durations.push_back(data->m_offsets[iFE] + data->m_durations[iFE] - tbase);
				cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_FILL));

				//If we are synchronized, just skip the fill block and move on
				if(scramblerLocked)
				{
					//TODO: verify the fill is in fact zeroes and show error or resync if not?
					i = iFE;
					continue;
				}

				//Log it
				LogDebug("Found FS at %s (%zu symbols) with scrambler unlocked\n",
					Unit(Unit::UNIT_FS).PrettyPrint(tstart).c_str(), numFillSymbols);
				LogIndenter li;

				//We need at least four fill symbols to get and verify a lock
				if(numFillSymbols < 4)
				{
					LogDebug("Not enough filler symbols to lock scrambler to, skipping\n");
					i = iFE;
					continue;
				}

				//Debug
				vector<uint8_t> target;
				for(size_t j=0; j<numFillSymbols; j++)
				{
					auto b = data->m_samples[iFirstFill+j].m_data;
					LogDebug("fill[%zu] = %02x\n", j, b);
					target.push_back(b);
				}

				//Scrambled data is XOR'd with the most significant 8 bits in reverse order
				//So we can bitswap the filler (known a priori to be all zeroes) and use that as the LFSR state
				scrambleState = 0xffff;
				scrambleState = (scrambleState & 0xff) | (g_bitswapTable[data->m_samples[iFirstFill+0].m_data] << 8);
				RunScrambler(scrambleState);
				scrambleState = (scrambleState & 0xff) | (g_bitswapTable[data->m_samples[iFirstFill+1].m_data] << 8);

				//Run the scrambler through the filler block and verify we get matching values for every symbol
				//(this also advances the scrambler for later)
				scramblerLocked = true;
				for(size_t j=0; j<(numFillSymbols-1); j++)
				{
					auto expected = RunScrambler(scrambleState);
					auto observed = data->m_samples[iFirstFill+j+1].m_data;
					if(expected != observed)
					{
						LogDebug("expected %02x got %02x\n", expected, observed);
						scramblerLocked = false;
						break;
					}
				}

				i = iFE;
			}
		}

		/*
			Scrambler not synced? Look for one of the two possible sync points:
			1) Scrambler reset for normal and content protection
			SR BF BF SR
			K28.0 K28.3 K28.3 K28.0

			2) Fill sequence
			FS (zero or more data characters) FE
			K30.7 (payload) K23.7
			Payload is all zeroes before scrambling
			Payload length is variable, we need at least 3 to ensure good lock (TODO more?)
		 */
	}

	/*
	enum
	{
		STATE_UNKNOWN,
		STATE_IDLE,
		STATE_HS_REQUEST,
		STATE_HS_SYNC_0,
		STATE_HS_SYNC_1,
		STATE_HS_SYNC_2,
		STATE_HS_SYNC_3,
		STATE_HS_SYNC_4,
		STATE_HS_DATA
	} state = STATE_UNKNOWN;

	//If our data is a single-ended decode, we have to infer some states we can't see.
	auto data_decoder = dynamic_cast<DPhySymbolDecoder*>(GetInput(1).m_channel);
	bool single_ended_data = data_decoder->GetInput(1).m_channel == nullptr;

	//Process the data
	DPMainLinkDataSymbol samp;
	size_t clklen = clk->m_samples.size();
	size_t datalen = data->m_samples.size();
	size_t iclk = 0;
	size_t idata = 0;
	int64_t timestamp	= 0;
	bool last_clk = 0;
	int count = 0;
	uint8_t cur_byte = 0;
	int64_t tstart = 0;
	while(true)
	{
		//Get the current samples
		auto cur_clk = clk->m_samples[iclk];
		auto cur_data = data->m_samples[idata];

		//Get timestamps of next event on each channel
		int64_t next_data = GetNextEventTimestamp(data, idata, datalen, timestamp);
		int64_t next_clk = GetNextEventTimestamp(clk, iclk, clklen, timestamp);
		int64_t next_timestamp = min(next_clk, next_data);
		if(next_timestamp == timestamp)
			break;

		size_t nlast = cap->m_samples.size()-1;
		int64_t tend = data->m_offsets[idata] + data->m_durations[idata];
		int64_t tclkstart = clk->m_offsets[iclk];

		//Look for clock edges
		bool clock_rising = false;
		bool clock_falling = false;
		if(cur_clk.m_type == DPhySymbol::STATE_HS1)
		{
			if(!last_clk)
				clock_rising = true;
			last_clk = true;
		}
		else if(cur_clk.m_type == DPhySymbol::STATE_HS0)
		{
			if(last_clk)
				clock_falling = true;
			last_clk = false;
		}
		bool clock_toggling = clock_rising || clock_falling;

		switch(state)
		{
			//Just started decoding. We don't know what's going on.
			//Wait for the link to go idle.
			case STATE_UNKNOWN:

				//LP-11 is a STOP sequence. The partial packet before this point can be safely discarded.
				//Emit an "IDLE" state for the duration of the LP-11.
				if(cur_data.m_type == DPhySymbol::STATE_LP11)
					state = STATE_IDLE;
				break;	//end STATE_UNKNOWN

			//Link is idle, wait for a start-of-transmission or escape sequence
			case STATE_IDLE:

				//LP-01 is a HS-REQUEST.
				//If doing a single-ended decode, we can't see the LP-01. We seem to jump straight to LP-00.
				if( (cur_data.m_type == DPhySymbol::STATE_LP01) ||
					(single_ended_data && (cur_data.m_type == DPhySymbol::STATE_LP00) ) )
				{
					state = STATE_HS_REQUEST;

					cap->m_offsets.push_back(data->m_offsets[idata]);
					cap->m_durations.push_back(data->m_durations[idata]);
					cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_SOT));
				}

				break;	//end STATE_IDLE

			//Starting a start-of-transmission sequence
			case STATE_HS_REQUEST:

				switch(cur_data.m_type)
				{
					//Ignore any LP states other than LP-11 which resets us
					case DPhySymbol::STATE_LP11:
						state = STATE_IDLE;
						break;

					//If we see HS-0, we're in the sync stage
					case DPhySymbol::STATE_HS0:
						state = STATE_HS_SYNC_0;
						break;

					default:
						break;
				}

				break;	//end STATE_HS_REQUEST

			//Wait for a HS-1 state on a rising clock edge to continue the sync
			case STATE_HS_SYNC_0:

				//Reset on LP-11
				if(cur_data.m_type == DPhySymbol::STATE_LP11)
				{
					state = STATE_IDLE;
					break;
				}

				//We got the HS-1. Extend the sample.
				if(clock_falling && cur_data.m_type == DPhySymbol::STATE_HS1)
				{
					state = STATE_HS_SYNC_1;
					count = 1;

					cap->m_durations[nlast] = tclkstart - cap->m_offsets[nlast];
				}

				break;	//end STATE_HS_SYNC_0

			//Expect three HS-1's in a row.
			case STATE_HS_SYNC_1:
				if(clock_toggling)
				{
					if(cur_data.m_type == DPhySymbol::STATE_HS1)
					{
						count ++;
						cap->m_durations[nlast] = tend - cap->m_offsets[nlast];

						if(count == 3)
							state = STATE_HS_SYNC_2;
					}

					else
						state = STATE_HS_SYNC_0;
				}
				break;	//end STATE_HS_SYNC_1

			//Expect a single HS-0
			case STATE_HS_SYNC_2:
				if(clock_toggling)
				{
					if(cur_data.m_type == DPhySymbol::STATE_HS0)
					{
						cap->m_durations[nlast] = tend - cap->m_offsets[nlast];
						state = STATE_HS_SYNC_3;
					}

					else
						state = STATE_HS_SYNC_0;
				}
				break;	//end STATE_HS_SYNC_2

			//Expect a single HS-1
			case STATE_HS_SYNC_3:
				if(clock_toggling)
				{
					if(cur_data.m_type == DPhySymbol::STATE_HS1)
					{
						cap->m_durations[nlast] = tclkstart - cap->m_offsets[nlast];
						count = 0;
						tstart = tclkstart;
						cur_byte = 0;
						state = STATE_HS_DATA;
					}

					else
						state = STATE_HS_SYNC_0;
				}
				break;	//end STATE_HS_SYNC_2

			//Read data bytes, LSB first
			case STATE_HS_DATA:

				if(clock_toggling)
				{
					//HS data bit
					if( (cur_data.m_type == DPhySymbol::STATE_HS0) || (cur_data.m_type == DPhySymbol::STATE_HS1) )
					{
						cur_byte >>= 1;
						if(cur_data.m_type == DPhySymbol::STATE_HS1)
							cur_byte |= 0x80;

						count ++;

						if(count == 8)
						{
							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(tclkstart - tstart);
							cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_HS_DATA, cur_byte));
							tstart = tclkstart;
							cur_byte = 0;
							count = 0;
						}
					}

					//End of packet
					else if(cur_data.m_type == DPhySymbol::STATE_LP11)
					{
						//Trim garbage at end of packet
						if(cap->m_offsets.size() >= 4)
						{
							//Discard last 3 bytes of data
							for(size_t i=0; i<3; i++)
							{
								cap->m_offsets.pop_back();
								cap->m_durations.pop_back();
								cap->m_samples.pop_back();
							}

							//Discard all bytes with the same value
							size_t endlen = cap->m_samples.size()-1;
							uint8_t last = cap->m_samples[endlen].m_data;
							while(cap->m_samples[endlen].m_data == last)
							{
								cap->m_offsets.pop_back();
								cap->m_durations.pop_back();
								cap->m_samples.pop_back();

								endlen --;
								if(endlen == 0)
									break;
							}

							//Add a new "end" sample
							endlen = cap->m_samples.size()-1;
							tstart = cap->m_offsets[endlen] + cap->m_durations[endlen];
							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(tclkstart - tstart);
							cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_EOT));
						}

						state = STATE_IDLE;
					}

					//Something illegal
					else
					{
						cap->m_offsets.push_back(data->m_offsets[idata]);
						cap->m_durations.push_back(data->m_durations[idata]);
						cap->m_samples.push_back(DPMainLinkDataSymbol(DPMainLinkDataSymbol::TYPE_ERROR));
						state = STATE_UNKNOWN;
					}

				}

				break;	//end STATE_HS_DATA

			default:
				break;
		}

		//All good, move on
		timestamp = next_timestamp;
		AdvanceToTimestamp(clk, iclk, clklen, timestamp);
		AdvanceToTimestamp(data, idata, datalen, timestamp);
	}
	*/
}

string DPMainLinkWaveform::GetColor(size_t i)
{
	const DPMainLinkDataSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case DPMainLinkDataSymbol::TYPE_SCRAMBLER_DESYNC:
		case DPMainLinkDataSymbol::TYPE_FRAME_DESYNC:
		case DPMainLinkDataSymbol::TYPE_FILL:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case DPMainLinkDataSymbol::TYPE_BS:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		/*
		case DPMainLinkDataSymbol::TYPE_EOT:
			return StandardColors::colors[StandardColors::COLOR_IDLE];

		case DPMainLinkDataSymbol::TYPE_HS_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];
		*/

		case DPMainLinkDataSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string DPMainLinkWaveform::GetText(size_t i)
{
	char tmp[32];
	const DPMainLinkDataSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case DPMainLinkDataSymbol::TYPE_SCRAMBLER_DESYNC:
			return "(scrambler desynced)";

		case DPMainLinkDataSymbol::TYPE_FRAME_DESYNC:
			return "(framing desynced)";

		case DPMainLinkDataSymbol::TYPE_FILL:
			return "(filler)";

		case DPMainLinkDataSymbol::TYPE_BS:
			return "Blanking Start";

		/*
		case DPMainLinkDataSymbol::TYPE_EOT:
			return "EOT";

		case DPMainLinkDataSymbol::TYPE_HS_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return tmp;
		*/

		case DPMainLinkDataSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}

