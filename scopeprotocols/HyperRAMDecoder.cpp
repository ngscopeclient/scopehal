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

/**
	@file
	@author Mike Walters
	@brief Implementation of HyperRAMDecoder
 */

#include "../scopehal/scopehal.h"
#include "HyperRAMDecoder.h"
#include <algorithm>

using namespace std;

HyperRAMDecoder::HyperRAMDecoder(const string& color)
	: Filter(color, CAT_BUS)
{
	AddProtocolStream("data");
	CreateInput("clk");
	CreateInput("cs#");
	CreateInput("rwds");
	CreateInput("dq0");
	CreateInput("dq1");
	CreateInput("dq2");
	CreateInput("dq3");
	CreateInput("dq4");
	CreateInput("dq5");
	CreateInput("dq6");
	CreateInput("dq7");

	m_latencyname = "Initial Latency";
	m_parameters[m_latencyname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_latencyname].SetIntVal(3);
}

bool HyperRAMDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i < 11) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string HyperRAMDecoder::GetProtocolName()
{
	return "HyperRAM";
}

Filter::DataLocation HyperRAMDecoder::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

void HyperRAMDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("HyperRAMDecoder::Refresh");
	#endif

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		for(size_t i=0; i<m_inputs.size(); i++)
		{
			if(!GetInput(i))
				AddErrorMessage("Missing inputs", string("No signal input connected to ") + m_signalNames[i] );
			else if(!GetInputWaveform(i))
				AddErrorMessage("Missing inputs", string("No waveform available at input ") + m_signalNames[i] );
		}

		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto clk = GetInputWaveform(0);
	auto csn = GetInputWaveform(1);
	auto rwds = GetInputWaveform(2);
	vector<WaveformBase*> data;
	for (int i = 0; i < 8; i++)
		data.push_back(GetInputWaveform(i + 3));

	//Prepare inputs
	clk->PrepareForCpuAccess();
	csn->PrepareForCpuAccess();
	rwds->PrepareForCpuAccess();
	for (int i = 0; i < 8; i++)
		data[i]->PrepareForCpuAccess();

	//Inputs can be sparse or uniform
	auto sclk = dynamic_cast<SparseDigitalWaveform*>(clk);
	auto scsn = dynamic_cast<SparseDigitalWaveform*>(csn);
	auto srwds = dynamic_cast<SparseDigitalWaveform*>(rwds);
	vector<SparseDigitalWaveform*> sdata;
	for (int i = 0; i < 8; i++)
		sdata.push_back(dynamic_cast<SparseDigitalWaveform*>(data[i]));

	auto uclk = dynamic_cast<UniformDigitalWaveform*>(clk);
	auto ucsn = dynamic_cast<UniformDigitalWaveform*>(csn);
	auto urwds = dynamic_cast<UniformDigitalWaveform*>(rwds);
	vector<UniformDigitalWaveform*> udata;
	for (int i = 0; i < 8; i++)
		udata.push_back(dynamic_cast<UniformDigitalWaveform*>(data[i]));

	//Create the capture
	auto cap = SetupEmptyWaveform<HyperRAMWaveform>(clk, 0);
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_DESELECTED,
		STATE_CA,
		STATE_READ_WAIT,
		STATE_READ,
		STATE_WRITE_WAIT,
		STATE_WRITE,
	} state = STATE_IDLE;

	enum
	{
		EVENT_CS,
		EVENT_CLK,
		EVENT_RWDS,
		EVENT_NONE
	} event_type = EVENT_CLK;	//initialization not required for control flow, since we always write it before using
								//but some gcc versions can't tell that and give a false warning

	int64_t sym_start   = 0;
	bool first			= false;
	int latency         = 0;

	size_t ics			= 0;
	size_t iclk			= 0;
	size_t irwds        = 0;
	vector<size_t> idata(8, 0);

	int64_t timestamp	= 0;
	int64_t ca_data     = 0;
	int64_t ca_byte     = 0;
	int64_t clk_time    = 0;
	int64_t last_clk    = 0;
	bool last_clkval	= GetValue(sclk, uclk, 0);

	size_t clklen = clk->size();
	size_t cslen = csn->size();
	size_t rwdslen = rwds->size();

	while(true)
	{
		//Get the current samples
		bool cur_cs = GetValue(scsn, ucsn, ics);
		bool cur_rwds = GetValue(srwds, urwds, irwds);
		uint8_t cur_data = 0;
		for (int i = 0; i < 8; i++)
			cur_data |= GetValue(sdata[i], udata[i], idata[i]) << i;

		auto deselect = [&]()
		{
			cap->m_offsets.push_back(sym_start);
			cap->m_durations.push_back(timestamp - sym_start);
			cap->m_samples.push_back(HyperRAMSymbol(HyperRAMSymbol::TYPE_DESELECT, 0));

			sym_start = timestamp;
			state = STATE_DESELECTED;
		};

		switch(state)
		{
			//Just started the decode, wait for CS# to go high (and don't attempt to decode a partial packet)
			case STATE_IDLE:
				if(cur_cs)
					state = STATE_DESELECTED;
				break;

			//wait for falling edge of CS#
			case STATE_DESELECTED:
				if(!cur_cs)
				{
					state = STATE_CA;
					ca_data = 0;
					ca_byte = 0;
					sym_start = timestamp;
					first = true;
				}
				break;

			case STATE_CA:
				//end of packet
				if(cur_cs)
				{
					deselect();
				}
				else if (event_type == EVENT_CLK)
				{
					// On first clk, output SELECT symbol
					if (first)
					{
						first = false;
						cap->m_offsets.push_back(sym_start);
						cap->m_durations.push_back(timestamp - sym_start);
						cap->m_samples.push_back(HyperRAMSymbol(HyperRAMSymbol::TYPE_SELECT, 0));
					}
					ca_data = (ca_data << 8) | cur_data;
					ca_byte++;
					if (ca_byte == 6)
					{
						cap->m_offsets.push_back(sym_start);
						cap->m_durations.push_back(timestamp - sym_start);
						cap->m_samples.push_back(HyperRAMSymbol(HyperRAMSymbol::TYPE_CA, ca_data));

						sym_start = timestamp;

						// Load initial latency setting
						// (multiply by 2 since we count edges, not cycles)
						latency = m_parameters[m_latencyname].GetIntVal() * 2;
						// If RWDS is high, additional latency is added
						if (cur_rwds)
							latency *= 2;
						// Subtract 1 cycle (2 edges) since the latency starts during the CA word
						latency -= 2;

						auto ca = DecodeCA(ca_data);
						if (ca.read)
						{
							state = STATE_READ_WAIT;
						}
						else
						{
							state = (ca.register_space) ? STATE_WRITE : STATE_WRITE_WAIT;
						}


						//state = STATE_READ;
					}
					else if (ca_byte == 1)
					{
						sym_start = timestamp;
					}
				}
				break;

			case STATE_READ_WAIT:
			case STATE_WRITE_WAIT:
				if (cur_cs)
				{
					deselect();
				}
				else if (event_type == EVENT_CLK)
				{
					latency--;
					if (latency == 0)
					{
						cap->m_offsets.push_back(sym_start);
						cap->m_durations.push_back(timestamp - sym_start);
						cap->m_samples.push_back(HyperRAMSymbol(HyperRAMSymbol::TYPE_WAIT, 0));
						state = (state == STATE_READ_WAIT) ? STATE_READ : STATE_WRITE;

						sym_start = timestamp;
					}
				}
				break;

			case STATE_READ:
				//end of packet
				if(cur_cs)
				{
					deselect();
				}
				else if (event_type == EVENT_RWDS)
				{
					// The symbol should continue until the next RWDS edge in this transaction, if available.
					// The final symbol may not have an RWDS edge after it, so use clk_time in that case.
					auto next_rwds = GetNextEventTimestampScaled(srwds, urwds, irwds, rwdslen, timestamp);
					auto next_cs = GetNextEventTimestampScaled(scsn, ucsn, ics, cslen, timestamp);
					auto duration = next_rwds - timestamp;
					if (next_rwds == timestamp || next_rwds > next_cs)
						duration = clk_time;
					cap->m_offsets.push_back(timestamp);
					cap->m_durations.push_back(duration);
					cap->m_samples.push_back(HyperRAMSymbol(HyperRAMSymbol::TYPE_DATA, cur_data));

					sym_start = timestamp + duration;
				}

				break;

			case STATE_WRITE:
				//end of packet
				if(cur_cs)
				{
					deselect();
				}
				else if (event_type == EVENT_CLK)
				{
					auto next_clk = GetNextEventTimestampScaled(sclk, uclk, iclk, clklen, timestamp);
					auto next_cs = GetNextEventTimestampScaled(scsn, ucsn, ics, cslen, timestamp);
					auto sym_end = timestamp + (next_clk - timestamp) / 2;
					if (next_clk == timestamp || next_clk > next_cs)
						sym_end = timestamp + clk_time/2;
					cap->m_offsets.push_back(sym_start);
					cap->m_durations.push_back(sym_end - sym_start);
					cap->m_samples.push_back(HyperRAMSymbol(HyperRAMSymbol::TYPE_DATA, cur_data));

					sym_start = sym_end;
				}

				break;
		}

		//Get timestamps of next event on each channel
		auto next_cs = GetNextEventTimestampScaled(scsn, ucsn, ics, cslen, timestamp);
		auto next_clk = GetNextEventTimestampScaled(sclk, uclk, iclk, clklen, timestamp);
		auto next_rwds = GetNextEventTimestampScaled(srwds, urwds, irwds, rwdslen, timestamp);

		// Find soonest event
		auto next_timestamp = next_cs;
		event_type = EVENT_CS;
		if (next_clk < next_timestamp)
		{
			next_timestamp = next_clk;
			event_type = EVENT_CLK;
		}
		if (next_rwds < next_timestamp)
		{
			next_timestamp = next_rwds;
			event_type = EVENT_RWDS;
		}

		//If we can't move forward, stop
		if(next_timestamp == timestamp)
			break;

		timestamp = next_timestamp;
		AdvanceToTimestampScaled(scsn, ucsn, ics, cslen, timestamp);
		AdvanceToTimestampScaled(sclk, uclk, iclk, clklen, timestamp);
		AdvanceToTimestampScaled(srwds, urwds, irwds, rwdslen, timestamp);

		// Keep track of the time between clock edges
		if (event_type == EVENT_CLK)
		{
			//See if we actually have a toggle. If not, this is a no-op event
			bool clkval = GetValue(sclk, uclk, iclk);
			if(clkval == last_clkval)
				event_type = EVENT_NONE;
			else
			{
				clk_time = next_clk - last_clk;
				last_clk = next_clk;
			}
			last_clkval = clkval;
		}

		auto data_timestamp = timestamp;

		// When doing a read we trigger off the RWDS edges,
		// but they are aligned with the data transitions.
		// So, sample the data ahead by half a clock time.
		if (state == STATE_READ && event_type == EVENT_RWDS)
			data_timestamp += clk_time / 2;
		for (int i = 0; i < 8; i++)
			AdvanceToTimestampScaled(sdata[i], udata[i], idata[i], data[i]->size(), data_timestamp);
	}

	cap->MarkModifiedFromCpu();
}

struct HyperRAMDecoder::CA HyperRAMDecoder::DecodeCA(uint64_t data)
{
	return {
		/*.address        = */(uint32_t)((data & 3) | ((data >> 13) & 0xFFFFFFF8)),
		/*.read           = */(bool)(data & (INT64_C(1) << 47)),
		/*.register_space = */(bool)(data & (INT64_C(1) << 46)),
		/*.linear         = */(bool)(data & (INT64_C(1) << 45)),
	};
}

std::string HyperRAMWaveform::GetColor(size_t i)
{
	const HyperRAMSymbol& s = m_samples[i];
	switch(s.m_stype)
	{
		case HyperRAMSymbol::TYPE_SELECT:
		case HyperRAMSymbol::TYPE_DESELECT:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case HyperRAMSymbol::TYPE_CA:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case HyperRAMSymbol::TYPE_WAIT:
			return StandardColors::colors[StandardColors::COLOR_IDLE];

		case HyperRAMSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case HyperRAMSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string HyperRAMWaveform::GetText(size_t i)
{
	const HyperRAMSymbol& s = m_samples[i];
	char tmp[32];
	struct HyperRAMDecoder::CA ca;
	const char* rw;
	const char* space;
	uint32_t addr;
	const char* burst;
	switch(s.m_stype)
	{
		case HyperRAMSymbol::TYPE_SELECT:
			return "SELECT";
		case HyperRAMSymbol::TYPE_DESELECT:
			return "DESELECT";
		case HyperRAMSymbol::TYPE_CA:
			ca    = HyperRAMDecoder::DecodeCA(s.m_data);
			rw    = ca.read ? "Read" : "Write";
			space = ca.register_space ? "reg" : "mem";
			addr  = ca.address;
			burst = ca.linear ? "linear" : "wrapped";
			snprintf(tmp, sizeof(tmp), "%s %s %08x %s", rw, space, addr, burst);
			return string(tmp);
		case HyperRAMSymbol::TYPE_WAIT:
			return "WAIT";
		case HyperRAMSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", (uint8_t)s.m_data);
			return string(tmp);
		case HyperRAMSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}
