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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of DensityFunctionWaveform

	@ingroup datamodel
 */

#include "scopehal.h"
#include "DensityFunctionWaveform.h"

/**
	@brief Initialize a new density function waveform of a given size

	@param width	Bitmap width, in pixels
	@param height	Bitmap height, in pixels
 */
DensityFunctionWaveform::DensityFunctionWaveform(size_t width, size_t height)
	: m_width(width)
	, m_height(height)
	, m_outdata("DensityFunctionWaveform.m_outdata")
{
	//Default to CPU+GPU mirror
	m_outdata.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_outdata.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	size_t npix = width*height;
	m_outdata.resize(npix);
	m_outdata.PrepareForCpuAccess();
	for(size_t i=0; i<npix; i++)
		m_outdata[i] = 0;
	m_outdata.MarkModifiedFromCpu();
}

DensityFunctionWaveform::~DensityFunctionWaveform()
{
}
