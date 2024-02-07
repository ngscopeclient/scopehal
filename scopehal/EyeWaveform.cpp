/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "EyeWaveform.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeWaveform::EyeWaveform(size_t width, size_t height, float center, EyeType etype)
	: DensityFunctionWaveform(width, height)
	, m_uiWidth(0)
	, m_saturationLevel(1)
	, m_totalUIs(0)
	, m_centerVoltage(center)
	, m_maskHitRate(0)
	, m_type(etype)
{
	size_t npix = width*height;
	m_accumdata = new int64_t[npix];
	for(size_t i=0; i<npix; i++)
		m_accumdata[i] = 0;
}

EyeWaveform::~EyeWaveform()
{
	delete[] m_accumdata;
	m_accumdata = NULL;
}

void EyeWaveform::Normalize()
{
	//Preprocessing
	int64_t nmax = 0;
	int64_t halfwidth = m_width/2;
	size_t blocksize = halfwidth * sizeof(int64_t);
	for(size_t y=0; y<m_height; y++)
	{
		int64_t* row = m_accumdata + y*m_width;

		//Find peak amplitude
		for(size_t x=halfwidth; x<m_width; x++)
			nmax = max(row[x], nmax);

		//Copy right half to left half
		memcpy(row, row+halfwidth, blocksize);
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

/**
	@brief Gets the BER at a single point, relative to the center of the eye opening

	@param pointx	X coordinate of the point
	@param pointy	Y coordinate of the point

	@param xmid		X coordinate of the center of the eye
	@param ymid		Y coordinate of the center of the eye

	BER is calculated by drawing a vector from the eye center to the point, then continuing to the edge of the
	eye and calculating what fraction of the points are before vs after the given point.

	TODO: if we have multiple eye openings (MLT/PAM) we should stop at the adjacent levels, not the edge of the eye
 */
double EyeWaveform::GetBERAtPoint(ssize_t pointx, ssize_t pointy, ssize_t xmid, ssize_t ymid)
{
	if(m_type == EYE_BER)
	{
		//out of bounds? all error
		if( (pointx < 0) || (pointx >= (ssize_t)m_width) || (pointy < 0) || (pointy >= (ssize_t)m_height))
			return 1;

		else
			return m_accumdata[pointy*m_width + pointx] * 1e-15;
	}
	else
	{
		//Unit vector from cursor towards the center of the eye
		//BER at center of eye is zero by definition
		float uvecx = pointx - xmid;
		float uvecy = pointy - ymid;
		float len = sqrt(uvecx * uvecx + uvecy * uvecy);
		if(len < 0.5)
			return 0;
		uvecx /= len;
		uvecy /= len;

		//Integrate along the entire path
		//Find the highest value along the path
		int64_t innerhits = 0;
		for(size_t i=0; i<len; i++)
		{
			auto x = static_cast<size_t>(round(xmid + uvecx*i));
			auto y = static_cast<size_t>(round(ymid + uvecy*i));
			int64_t hits = m_accumdata[y*m_width + x];
			innerhits += hits;
		}

		//Continue along the path until we hit the edge of the eye
		int64_t totalhits = innerhits;
		for(size_t i=len; ; i++)
		{
			// must cast through a signed int type to avoid UB [conv.fpint]
			size_t x = static_cast<ssize_t>(round(xmid + uvecx*i));
			size_t y = static_cast<ssize_t>(round(ymid + uvecy*i));

			//no lower bounds check since size_t is unsigned
			//if we go off the low end we'll wrap and fail the high side bounds check
			if( (x >= m_width) || (y >= m_height) )
				break;

			int64_t hits = m_accumdata[y*m_width + x];
			totalhits += hits;
		}

		//Find how many of the total hits were between our cursor and the eye center
		return 1.0 * innerhits / totalhits;
	}
}
