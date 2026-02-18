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
	int8_t dinI[];
};

layout(std430, binding=1) restrict readonly buffer buf_dinQ
{
	int8_t dinQ[];
};

layout(std430, binding=2) restrict writeonly buffer buf_vstarts
{
	uint vstarts[];
};

layout(std430, binding=3) restrict writeonly buffer buf_vscramblers
{
	int64_t vscramblers[];
};

layout(std430, push_constant) uniform constants
{
	uint len;
	uint samplesPerThread;
	uint maxOutputPerThread;
	uint masterMode;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

#define STATE_IDLE		0
#define STATE_SSD_1		1
#define STATE_SSD_2 	2
#define STATE_PACKET	3
#define STATE_ESD_1		4
#define STATE_ESD_2		5

void main()
{
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	uint instart = nthread * samplesPerThread;
	uint inend = instart + samplesPerThread;

	//Off buffer entirely? Stop
	if(instart >= len)
	{
		vstarts[nthread] = 0;
		return;
	}

	//Clamp buffer range
	if(inend >= len)
		inend = len - 1;

	//Back up to allow idle start before start of frame
	//TODO: this isn't perfect, max sized frames at a bad time will exceed this!
	uint realstart = instart;
	uint scrollback = 1024;
	if(instart > scrollback)
		realstart -= scrollback;

	//TODO: allow in-progress frame to run off the end of the block

	uint state = STATE_IDLE;
	uint nouts = 0;

	uint outbase = nthread * maxOutputPerThread;

	uint wipStart = 0;
	uint lastScramblerError = 0;
	uint scramblerErrors = 0;
	int64_t wipScrambler = 0;
	int64_t scrambler = 0;
	bool scramblerLocked = false;
	uint idlesMatched = 0;
	for(uint i=realstart; ; i++)
	{
		//Immediately stop if off the buffer
		if(i >= len)
			break;

		//Stop if idle and out of our search area
		if( (i >= inend) && (state == STATE_IDLE) )
			break;

		int ci = int(dinI[i]);
		int cq = int(dinQ[i]);

		//Advance the scrambler for each constellation point
		int64_t b32 = (scrambler >> 32) & 1;
		int64_t b19 = (scrambler >> 19) & 1;
		int64_t b12 = (scrambler >> 12) & 1;
		if(masterMode == 1)
			scrambler = (scrambler << 1) | ( b32 ^ b12 );
		else
			scrambler = (scrambler << 1) | ( b32 ^ b19 );

		//Extract scrambler bits we care about for the data bits
		bool b0 = (scrambler & 1) == 1;

		switch(state)
		{
			//Look for three (0,0) points in a row to indicate SSD
			//96.3.3.3.5
			case STATE_IDLE:
				if( (ci == 0) && (cq == 0) )
					state = STATE_SSD_1;

				//Not a SSD, it's idles.
				else
				{
					bool expected_lsb = ( (ci == -1) && (cq == -1) ) || (ci == 0) || ( (ci == 1) && (cq == 1) );

					//See if we already got the expected value out of the scrambler
					bool current_lsb = b0;

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

							if(scramblerErrors > 16)
								scramblerLocked = false;
						}

						//No, unlocked. Feed data in and try to get a lock
						else
						{
							idlesMatched = 0;
							scrambler = (scrambler & ~1);
							if(expected_lsb)
								scrambler |= 1;
						}
					}

					//Declare lock after 256 error-free idles
					if( (idlesMatched > 256) && !scramblerLocked)
					{
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

					//Save if scrambler was locked and it started within our block
					if(scramblerLocked && (i >= instart) )
					{
						vstarts[outbase + nouts + 1] = i;
						vscramblers[outbase + nouts + 1] = scrambler;
						nouts ++;
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

				break;

			case STATE_ESD_1:

				//Look for ESD, bail if malformed
				if( (ci == 0) && (cq == 0) )
					state = STATE_ESD_2;
				else
					state = STATE_IDLE;

				break;

			case STATE_ESD_2:
				state = STATE_IDLE;
				break;

			default:
				break;
		}
	}

	vstarts[outbase] = nouts;
}
