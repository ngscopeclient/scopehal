/***********************************************************************************************************************
*                                                                                                                      *
* LIBSCOPEHAL v0.1                                                                                                      *
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

//local size must equal this
#define BLOCK_SIZE 1024

__kernel void FIRFilter(
	__global const float* din,
	__constant const float* coefficients,
	__global float* dout,
	unsigned long filterlen,
	unsigned long end,
	__global float* minmaxbuf
	)
{
	__local float temp[BLOCK_SIZE];
	unsigned long i = get_global_id(0);
	unsigned long nblock = i / BLOCK_SIZE;

	//Make sure we're actually in the block before executing
	if(i < end)
	{
		//FIR filter core
		float v = 0;
		for(unsigned long j=0; j<filterlen; j++)
			v += din[i+j] * coefficients[j];

		//Save in shared memory for the reduction, then global memory for output
		temp[get_local_id(0)] = v;
		dout[i] = v;
	}

	//Min/max reduction in first thread of the block
	barrier(CLK_LOCAL_MEM_FENCE);
	if(get_local_id(0) == 0)
	{
		float vmin = FLT_MAX;
		float vmax = -FLT_MAX;

		for(unsigned long j=0; j<BLOCK_SIZE; j++)
		{
			unsigned long off = i+j;
			if(off > end)
				break;

			float f = temp[j];
			vmin = min(vmin, f);
			vmax = max(vmax, f);
		}

		minmaxbuf[nblock*2 + 0] = vmin;
		minmaxbuf[nblock*2 + 1] = vmax;
	}
}
