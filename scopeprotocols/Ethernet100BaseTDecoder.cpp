/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
#include "Ethernet100BaseTDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet100BaseTDecoder::Ethernet100BaseTDecoder(string color)
	: EthernetProtocolDecoder(color)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void Ethernet100BaseTDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "100BaseTX(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string Ethernet100BaseTDecoder::GetProtocolName()
{
	return "Ethernet - 100baseTX";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet100BaseTDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);

	//Copy our time scales from the input
	EthernetWaveform* cap = new EthernetWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	const int64_t ui_width 			= 8000;
	const int64_t ui_width_samples	= ui_width / din->m_timescale;
	//const int64_t ui_halfwidth 		= 4000;
	//const int64_t jitter_tol 		= 1500;

	//Logical voltage of each point after some hysteresis
	vector<EmptyConstructorWrapper<int>> voltages;
	size_t ilen = din->m_samples.size();
	voltages.resize(ilen);
	int oldstate = GetState(din->m_samples[0]);
	voltages[0] = 0;
	for(size_t i=1; i<ilen; i++)
	{
		int newstate = oldstate;
		float voltage = din->m_samples[i];
		switch(oldstate)
		{
			//At the middle? Need significant motion either way to change state
			case 0:
				if(voltage > 0.6)
					newstate = 1;
				else if(voltage < -0.6)
					newstate = -1;
				break;

			//High? Move way low to change
			case 1:
				if(voltage < 0.2)
					newstate = 0;
				break;

			//Low? Move way high to change
			case -1:
				if(voltage > -0.2)
					newstate = 0;
				break;
		}

		voltages[i] = newstate;
		oldstate = newstate;
	}

	//MLT-3 decode
	//TODO: some kind of sanity checking that voltage is changing in the right direction
	int old_voltage = voltages[0];
	DigitalWaveform bits;
	bool signal_ok = false;
	vector<size_t> carrier_starts;
	vector<size_t> carrier_stops;
	size_t losslen = 20*ui_width;
	size_t old_offset = 0;
	float ui_inverse = 1.0f / ui_width;
	for(size_t i=0; i<ilen; i++)
	{
		if(voltages[i] != old_voltage)
		{
			if(!signal_ok)
			{
				signal_ok = true;
				LogTrace("Carrier found at index %zu\n", i);
				carrier_starts.push_back(i);
			}

			//Don't actually process the first bit since it's truncated
			if(old_offset != 0)
			{
				//See how long the voltage stayed constant.
				//For each UI, add a "0" bit, then a "1" bit for the current state
				int64_t tchange = din->m_offsets[i];
				int64_t dt = (tchange - old_offset) * din->m_timescale;
				int num_uis = round(dt * ui_inverse);

				//Add zero bits for each UI without a transition
				int64_t tnext;
				for(int j=0; j<(num_uis - 1); j++)
				{
					tnext = old_offset + ui_width_samples*j;
					bits.m_offsets.push_back(tnext);
					bits.m_durations.push_back(ui_width_samples);
					bits.m_samples.push_back(0);
				}
				tnext = old_offset + ui_width_samples*(num_uis - 1);

				//and then a 1 bit
				bits.m_offsets.push_back(tnext);
				bits.m_durations.push_back(ui_width_samples);
				bits.m_samples.push_back(1);
			}

			old_offset = din->m_offsets[i];
			old_voltage = voltages[i];
		}

		//Look for complete loss of signal.
		//We define this as more than 20 "0" symbols in a row.
		if( (old_offset + losslen) < (size_t)din->m_offsets[i])
		{
			if(signal_ok)
			{
				signal_ok = false;
				carrier_stops.push_back(i);
				LogTrace("Carrier lost at index %zu\n", i);
			}
		}
	}

	//carrier stops at end of capture to simplify processing
	bool lost_before_end = true;
	if(carrier_stops.size() < carrier_starts.size())
	{
		lost_before_end = false;
		carrier_stops.push_back(bits.m_offsets[bits.m_offsets.size()-1]);
	}

	//Run all remaining decode steps in blocks of valid signal
	for(size_t nblock=0; nblock<carrier_starts.size(); nblock ++)
	{
		LogTrace("nblock = %zu\n", nblock);
		size_t istart = carrier_starts[nblock];
		size_t istop = carrier_stops[nblock];

		//If we have multiple blocks of valid signal, add a [NO CARRIER] symbol between them
		if(nblock > 0)
		{
			size_t ilost = carrier_stops[nblock-1];
			size_t tstart = din->m_offsets[ilost];
			size_t tend = din->m_offsets[istart];
			LogTrace("No carrier from %zu to %zu\n", tstart, tend);
			EthernetFrameSegment seg;
			seg.m_type = EthernetFrameSegment::TYPE_NO_CARRIER;
			cap->m_offsets.push_back(tstart);
			cap->m_durations.push_back(tend - tstart);
			cap->m_samples.push_back(seg);
		}

		//RX LFSR sync
		DigitalWaveform descrambled_bits;
		bool synced = false;
		for(size_t idle_offset = istart; idle_offset<istart+15000 && idle_offset<istop; idle_offset++)
		{
			if(TrySync(bits, descrambled_bits, idle_offset, istop))
			{
				LogTrace("Got good LFSR sync at offset %zu\n", idle_offset);
				synced = true;
				break;
			}
		}
		if(!synced)
		{
			LogTrace("Ethernet100BaseTDecoder: Unable to sync RX LFSR\n");
			descrambled_bits.clear();
			continue;
		}

		//Search until we find a 1100010001 (J-K, start of stream) sequence
		bool ssd[10] = {1, 1, 0, 0, 0, 1, 0, 0, 0, 1};
		size_t i = 0;
		bool hit = true;
		size_t des10 = descrambled_bits.m_samples.size() - 10;
		for(i=0; i<des10; i++)
		{
			hit = true;
			for(int j=0; j<10; j++)
			{
				bool b = descrambled_bits.m_samples[i+j];
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
			continue;
		}
		LogTrace("Found SSD at %zu\n", i);

		//Skip the J-K as we already parsed it
		i += 10;

		//4b5b decode table
		static const int code_5to4[]=
		{
			-1, //0x00 unused
			-1, //0x01 unused
			-1, //0x02 unused
			-1, //0x03 unused
			-1, //0x04 = /H/, tx error
			-1, //0x05 unused
			-1, //0x06 unused
			0, //0x07 = /R/, second half of ESD
			-1, //0x08 unused
			0x1,
			0x4,
			0x5,
			-1, //0x0c unused
			0, //0x0d = /T/, first half of ESD
			0x6,
			0x7,
			-1, //0x10 unused
			0, //0x11 = /K/, second half of SSD
			0x8,
			0x9,
			0x2,
			0x3,
			0xa,
			0xb,
			0, //0x18 = /J/, first half of SSD
			-1, //0x19 unused
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
			int decoded = code_5to4[code];
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
	}

	LogTrace("%zu samples\n", cap->m_samples.size());

	//If we lost the signal before the end of the capture, add a sample for that
	if(lost_before_end)
	{
		size_t nindex = carrier_stops[carrier_stops.size()-1];
		size_t tstart = din->m_offsets[nindex];
		size_t tend = din->m_offsets[din->m_samples.size()-1];
		LogTrace("No carrier from index %zu (time %zu) to %zu (end of capture)\n",
			nindex, tstart, tend);
		EthernetFrameSegment seg;
		seg.m_type = EthernetFrameSegment::TYPE_NO_CARRIER;
		cap->m_offsets.push_back(tstart);
		cap->m_durations.push_back(tend - tstart);
		cap->m_samples.push_back(seg);
	}

	SetData(cap, 0);
}

bool Ethernet100BaseTDecoder::TrySync(
	DigitalWaveform& bits,
	DigitalWaveform& descrambled_bits,
	size_t idle_offset,
	size_t stop)
{
	if( (idle_offset + 64) >= bits.m_samples.size())
		return false;
	descrambled_bits.clear();

	//For now, assume the link is idle at the time we triggered
	unsigned int lfsr =
		( (!bits.m_samples[idle_offset + 0]) << 10 ) |
		( (!bits.m_samples[idle_offset + 1]) << 9 ) |
		( (!bits.m_samples[idle_offset + 2]) << 8 ) |
		( (!bits.m_samples[idle_offset + 3]) << 7 ) |
		( (!bits.m_samples[idle_offset + 4]) << 6 ) |
		( (!bits.m_samples[idle_offset + 5]) << 5 ) |
		( (!bits.m_samples[idle_offset + 6]) << 4 ) |
		( (!bits.m_samples[idle_offset + 7]) << 3 ) |
		( (!bits.m_samples[idle_offset + 8]) << 2 ) |
		( (!bits.m_samples[idle_offset + 9]) << 1 ) |
		( (!bits.m_samples[idle_offset + 10]) << 0 );

	//Descramble
	size_t len = bits.m_samples.size();
	for(unsigned int i=idle_offset + 11; i<len && i<stop; i++)
	{
		lfsr = (lfsr << 1) ^ ((lfsr >> 8)&1) ^ ((lfsr >> 10)&1);
		descrambled_bits.m_offsets.push_back(bits.m_offsets[i]);
		descrambled_bits.m_durations.push_back(bits.m_durations[i]);
		bool b = bits.m_samples[i] ^ (lfsr & 1);
		descrambled_bits.m_samples.push_back(b);
	}

	//We should have at least 64 "1" bits in a row once the descrambling is done.
	//The minimum inter-frame gap is a lot bigger than this.
	for(int i=0; i<64; i++)
	{
		if(descrambled_bits.m_samples[i + idle_offset + 11] != 1)
			return false;
	}

	//Synced, all good
	return true;
}

int Ethernet100BaseTDecoder::GetState(float voltage)
{
	if(voltage > 0.3)
		return 1;
	else if(voltage < -0.3)
		return -1;
	else
		return 0;
}
