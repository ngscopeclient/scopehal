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

#version 460
#pragma shader_stage(compute)

#extension GL_EXT_shader_8bit_storage : require
#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict readonly buffer buf_dinI
{
	int8_t pointsI[];
};

layout(std430, binding=1) restrict readonly buffer buf_dinQ
{
	int8_t pointsQ[];
};

layout(std430, binding=2) restrict readonly buffer buf_packetStarts
{
	uint packetStarts[];
};

layout(std430, binding=3) restrict readonly buffer buf_packetScramblers
{
	uint64_t packetScramblers[];
};

layout(std430, binding=4) restrict writeonly buffer buf_bytes
{
	uint8_t bytes[];
};

layout(std430, binding=5) restrict writeonly buffer buf_starts
{
	int64_t starts[];
};

layout(std430, binding=6) restrict writeonly buffer buf_ends
{
	int64_t ends[];
};

layout(std430, binding=7) restrict readonly buffer buf_offsetsIn
{
	int64_t offsetsIn[];
};

layout(std430, binding=8) restrict readonly buffer buf_durationsIn
{
	int64_t durationsIn[];
};

layout(std430, push_constant) uniform constants
{
	uint npackets;
	uint maxPacketBytes;
	uint inputLength;
	uint masterMode;
};

layout(local_size_x=32, local_size_y=1, local_size_z=1) in;

//state values because no glsl enums :'(
#define STATE_IDLE 0
#define STATE_SSD_1 1
#define STATE_SSD_2 2
#define STATE_PACKET 3
#define STATE_ESD_1 4
#define STATE_ESD_2 5

void main()
{
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	if(nthread >= npackets)
		return;

	//Get the initial scrambler seed and packet starting position from the previous shader
	//TODO: can we merge this and the other shader or something?
	uint64_t scrambler = packetScramblers[nthread];
	uint i = packetStarts[nthread];

	//Output buffer index starts at 1, index 0 of starts is the output length
	uint ioutBase = nthread * maxPacketBytes;
	uint iout = 1;

	//make sure we have a full symbol worth of data for the preamble, discard if not
	if(i < 2)
	{
		starts[ioutBase] = 0;
		return;
	}

	int64_t tnow = offsetsIn[i];
	int64_t tlen = durationsIn[i];

	//Add the fake preamble byte
	int64_t bytestart = offsetsIn[i-2];
	uint pout = iout + ioutBase;
	bytes[pout] = uint8_t(0x55);
	starts[pout] = bytestart;
	bytestart = tnow + (tlen * 2 / 3);
	ends[pout] = bytestart;
	iout ++;

	//We're now 1 bit into the first preamble byte, which is always a 1
	uint nbits = 1;
	uint curNib = 1;
	uint prevNib = 0;
	bool phaseLow = true;
	uint state = STATE_PACKET;
	i++;

	//Decode just this one packet
	for(; (i<inputLength) && (state != STATE_IDLE); i++)
	{
		tnow = offsetsIn[i];
		tlen = durationsIn[i];

		int ci = int(pointsI[i]);
		int cq = int(pointsQ[i]);

		//Advance the scrambler for each constellation point
		uint b32 = uint((scrambler >> 32) & 1);
		uint b19 = uint((scrambler >> 19) & 1);
		uint b12 = uint((scrambler >> 12) & 1);
		if(masterMode != 0)
			scrambler = (scrambler << 1) | ( b32 ^ b12 );
		else
			scrambler = (scrambler << 1) | ( b32 ^ b19 );

		//Extract scrambler bits we care about for the data bits
		uint b16 = uint((scrambler >> 16) & 1);
		uint b8 = uint((scrambler >> 8) & 1);
		uint b6 = uint((scrambler >> 6) & 1);
		uint b3 = uint((scrambler >> 3) & 1);
		uint b0 = uint(scrambler & 1);

		switch(state)
		{
			case STATE_PACKET:

				//Look for ESD
				//96.3.3.3.5
				if( (ci == 0) && (cq == 0) )
					state = STATE_ESD_1;

				//No, it's a data symbol
				else
				{
					//Decode to a sequence of 3 scrambled data bits
					uint sd = 0;
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

					uint sy0 = b0;
					uint sy1 = b3 ^ b8;
					uint sy2 = b6 ^ b16;
					sd ^= (sy2 << 2) ^ (sy1 << 1) ^ sy0;

					//Add the 3 descrambled bits into the current nibble
					curNib |= (sd << nbits);
					nbits += 3;

					//At this point we should have 3, 4, 5, or 6 bits in the current nibble-in-progress.
					//If we have at least a whole nibble, process it
					if(nbits >= 4)
					{
						uint nib = curNib & 0xf;

						curNib >>= 4;
						nbits -= 4;

						//Combine nibbles into bytes
						if(!phaseLow)
						{
							uint bval = (nib << 4) | prevNib;

							uint pout = iout + ioutBase;
							bytes[pout] = uint8_t(bval);
							starts[pout] = bytestart;

							//Byte end time depends on how many bits from this symbol weren't consumed
							bytestart = tnow + (tlen * (2 - int64_t(nbits)) / 3);

							ends[pout] = bytestart;
							iout ++;
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
				{
					starts[ioutBase] = int64_t(iout - 1);
					return;
				}

				//ESD with error
				//TODO: how to handle this? for now decode it anyway
				else if( (ci == -1) && (cq == -1) )
				{
					starts[ioutBase] = int64_t(iout - 1);
					return;
				}

				//invalid, don't try to decode
				else
					starts[ioutBase] = 0;

				//Done with the packet
				return;

			default:
				break;
		}
	}
}
