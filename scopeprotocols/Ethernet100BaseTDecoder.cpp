/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "EthernetRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet100BaseTDecoder::Ethernet100BaseTDecoder(
	string hwname, string color)
	: EthernetProtocolDecoder(hwname, color)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string Ethernet100BaseTDecoder::GetProtocolName()
{
	return "Ethernet - 100baseTX";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet100BaseTDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	AnalogCapture* din = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//Can't do much if we have no samples to work with
	if(din->GetDepth() == 0)
	{
		SetData(NULL);
		return;
	}

	//Copy our time scales from the input
	EthernetCapture* cap = new EthernetCapture;
	m_timescale = m_channels[0]->m_timescale;
	cap->m_timescale = din->m_timescale;

	const int64_t ui_width 			= 8000;
	const int64_t ui_width_samples	= ui_width / din->m_timescale;
	//const int64_t ui_halfwidth 		= 4000;
	//const int64_t jitter_tol 		= 1500;

	//Logical voltage of each point after some hysteresis
	vector<int> voltages;
	int oldstate = GetState(din->m_samples[0]);
	for(auto s : din->m_samples)
	{
		int newstate = oldstate;
		float voltage = s;
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

		voltages.push_back(newstate);
		oldstate = newstate;
	}

	//MLT-3 decode
	//TODO: some kind of sanity checking that voltage is changing in the right direction
	int old_voltage = voltages[0];
	int old_index = 0;
	vector<DigitalSample> bits;
	for(size_t i=0; i<voltages.size(); i++)
	{
		if(voltages[i] != old_voltage)
		{
			//Don't actually process the first bit since it's truncated
			if(old_index != 0)
			{
				//See how long the voltage stayed constant.
				//For each UI, add a "0" bit, then a "1" bit for the current state
				int64_t tstart = din->m_samples[old_index].m_offset;
				int64_t tchange = din->m_samples[i].m_offset;
				int64_t dt = (tchange - tstart) * din->m_timescale;
				int num_uis = round(dt * 1.0f / ui_width);

				/*LogDebug("Voltage changed to %2d at %10.6f us (%d UIs): ",
					voltages[i],
					tchange * din->m_timescale * 1e-6f,
					num_uis);*/

				//Add zero bits for each UI without a transition
				int64_t tnext;
				for(int j=0; j<(num_uis - 1); j++)
				{
					tnext = tstart + ui_width_samples*j;
					bits.push_back(DigitalSample(
						tnext,
						ui_width_samples,
						0));
				}
				tnext = tstart + ui_width_samples*(num_uis - 1);

				//and then a 1 bit
				bits.push_back(DigitalSample(
					tnext,
					(tchange + din->m_samples[i].m_duration) - tnext,
					1));
			}

			old_index = i;
			old_voltage = voltages[i];
		}
	}

	//RX LFSR sync
	vector<DigitalSample> descrambled_bits;
	bool synced = false;
	for(unsigned int idle_offset = 0; idle_offset<15000 && idle_offset<bits.size(); idle_offset++)
	{
		if(TrySync(bits, descrambled_bits, idle_offset))
		{
			LogDebug("Got good LFSR sync at offset %u\n", idle_offset);
			synced = true;
			break;
		}
	}
	if(!synced)
	{
		LogError("Ethernet100BaseTDecoder: Unable to sync RX LFSR\n");
		descrambled_bits.clear();

		//this is a fatal error, stop
		delete cap;
		SetData(NULL);
		return;
	}

	//Search until we find a 1100010001 (J-K, start of stream) sequence
	bool ssd[10] = {1, 1, 0, 0, 0, 1, 0, 0, 0, 1};
	unsigned int i = 0;
	bool hit = true;
	for(i=0; i<descrambled_bits.size() - 10; i++)
	{
		hit = true;
		for(int j=0; j<10; j++)
		{
			if(descrambled_bits[i+j].m_sample != ssd[j])
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
		LogWarning("No SSD found\n");
		delete cap;
		SetData(NULL);
		return;
	}
	LogDebug("Found SSD at %u\n", i);

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
	for(; i<descrambled_bits.size()-5; i+=5)
	{
		unsigned int code =
			(descrambled_bits[i+0].m_sample << 4) |
			(descrambled_bits[i+1].m_sample << 3) |
			(descrambled_bits[i+2].m_sample << 2) |
			(descrambled_bits[i+3].m_sample << 1) |
			(descrambled_bits[i+4].m_sample << 0);

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
			current_start = descrambled_bits[i].m_offset;
			current_byte = decoded;
		}
		else
		{
			current_byte |= decoded << 4;

			bytes.push_back(current_byte);
			starts.push_back(current_start * cap->m_timescale);
			uint64_t end = descrambled_bits[i+4].m_offset + descrambled_bits[i+4].m_duration;
			ends.push_back(end * cap->m_timescale);
		}

		first = !first;
	}

	SetData(cap);
}

bool Ethernet100BaseTDecoder::TrySync(
	vector<DigitalSample>& bits,
	vector<DigitalSample>& descrambled_bits,
	unsigned int idle_offset)
{
	if( (idle_offset + 64) >= bits.size())
		return false;
	descrambled_bits.clear();

	//For now, assume the link is idle at the time we triggered
	unsigned int lfsr =
		( (!bits[idle_offset + 0]) << 10 ) |
		( (!bits[idle_offset + 1]) << 9 ) |
		( (!bits[idle_offset + 2]) << 8 ) |
		( (!bits[idle_offset + 3]) << 7 ) |
		( (!bits[idle_offset + 4]) << 6 ) |
		( (!bits[idle_offset + 5]) << 5 ) |
		( (!bits[idle_offset + 6]) << 4 ) |
		( (!bits[idle_offset + 7]) << 3 ) |
		( (!bits[idle_offset + 8]) << 2 ) |
		( (!bits[idle_offset + 9]) << 1 ) |
		( (!bits[idle_offset + 10]) << 0 );

	//Descramble
	for(unsigned int i=idle_offset + 11; i<bits.size(); i++)
	{
		auto b = bits[i];
		lfsr = (lfsr << 1) ^ ((lfsr >> 8)&1) ^ ((lfsr >> 10)&1);

		descrambled_bits.push_back(DigitalSample(
			b.m_offset,
			b.m_duration,
			b.m_sample ^ (lfsr & 1)));
	}

	//We should have at least 64 "1" bits in a row once the descrambling is done.
	//The minimum inter-frame gap is a lot bigger than this.
	for(int i=0; i<64; i++)
	{
		if(descrambled_bits[i + idle_offset + 11].m_sample != 1)
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
