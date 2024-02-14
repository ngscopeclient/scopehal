/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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
#include "ConstellationWaveform.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ConstellationWaveform::ConstellationWaveform(size_t width, size_t height)
	: DensityFunctionWaveform(width, height)
	, m_saturationLevel(1)
	, m_totalSymbols(0)
{
	size_t npix = width*height;
	m_accumdata = new int64_t[npix];
	for(size_t i=0; i<npix; i++)
		m_accumdata[i] = 0;
}

ConstellationWaveform::~ConstellationWaveform()
{
	delete[] m_accumdata;
	m_accumdata = NULL;
}

void ConstellationWaveform::Normalize()
{
	//Preprocessing
	int64_t nmax = 0;
	for(size_t y=0; y<m_height; y++)
	{
		int64_t* row = m_accumdata + y*m_width;

		//Find peak amplitude
		for(size_t x=0; x<m_width; x++)
			nmax = max(row[x], nmax);
	}
	if(nmax == 0)
		nmax = 1;
	float norm = 2.0f / nmax;

	/*
		Normalize with saturation
		Saturation level of 1.0 means mapping all values to [0, 1].
		2.0 means mapping values to [0, 2] and saturating anything above 1.

		TODO: do this in a shader?
	 */
	norm *= m_saturationLevel;
	size_t len = m_width * m_height;
	m_outdata.PrepareForCpuAccess();
	for(size_t i=0; i<len; i++)
		m_outdata[i] = min(1.0f, m_accumdata[i] * norm);
	m_outdata.MarkModifiedFromCpu();
}
