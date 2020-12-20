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

/*
	All kernels in this file apply a window function, then zero-pad to a specified length.

	Outlen must be >= inlen.

	Most filters take several constant arguments which are precomputed host side.
 */

/**
	@brief Blackman-Harris window
 */
__kernel void BlackmanHarrisWindow(
	__global const float* din,
	__global float* dout,
	unsigned long inlen,
	unsigned long outlen,
	float scale)				//2 * M_PI / inlen
{
	unsigned long i = get_global_id(0);
	if(i >= inlen)
	{
		dout[i] = 0;
		return;
	}

	const float alpha0 = 0.35875;
	const float alpha1 = 0.48829;
	const float alpha2 = 0.14128;
	const float alpha3 = 0.01168;

	float num = i * scale;
	float w =
		alpha0 -
		alpha1 * cos(num) +
		alpha2 * cos(2*num) -
		alpha3 * cos(6*num);

	dout[i] = w * din[i];
}

/**
	@brief Cosine-sum window (Hann, Hanning, and similar)
 */
__kernel void CosineSumWindow(
	__global const float* din,
	__global float* dout,
	unsigned long inlen,
	unsigned long outlen,
	float scale,					//2 * M_PI / inlen
	float alpha0,					//0.5 for Hann, 25/46 for Hamming
	float alpha1)					//1 - alpha0
{
	unsigned long i = get_global_id(0);
	if(i >= inlen)
	{
		dout[i] = 0;
		return;
	}

	float w = alpha0 - alpha1*cos(i*scale);
	dout[i] = w * din[i];
}

/**
	@brief Rectangular window (glorified memcpy)
 */
__kernel void RectangularWindow(
	__global const float* din,
	__global float* dout,
	unsigned long inlen,
	unsigned long outlen)
{
	unsigned long i = get_global_id(0);

	if(i >= inlen)
		dout[i] = 0;
	else
		dout[i] = din[i];
}
