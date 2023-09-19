/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of DensityFunction
 */

#ifndef DensityFunctionWaveform_h
#define DensityFunctionWaveform_h

#include "Waveform.h"

/**
	@brief Base class for waveforms such as eye patterns, spectrograms, and waterfalls which are conceptually a 2D bitmap

	Internally, the image data is represented as an AcceleratorBuffer<float> storing 2D sample values.
 */
class DensityFunctionWaveform : public WaveformBase
{
public:

	DensityFunctionWaveform(size_t width, size_t height);
	virtual ~DensityFunctionWaveform();

	//not copyable or assignable
	DensityFunctionWaveform(const DensityFunctionWaveform&) =delete;
	DensityFunctionWaveform& operator=(const DensityFunctionWaveform&) =delete;

	//nothing to do if not gpu accelerated
	virtual void Rename(const std::string& /*name*/ = "")
	{}

	float* GetData()
	{
		m_outdata.PrepareForCpuAccess();
		return m_outdata.GetCpuPointer();
	}

	AcceleratorBuffer<float>& GetOutData()
	{ return m_outdata; }

	size_t GetHeight()
	{ return m_height; }

	size_t GetWidth()
	{ return m_width; }

	//Unused virtual methods from WaveformBase that we have to override
	virtual void clear()
	{}

	virtual void Resize(size_t /*unused*/)
	{}

	virtual void PrepareForCpuAccess()
	{ m_outdata.PrepareForCpuAccess(); }

	virtual void PrepareForGpuAccess()
	{ m_outdata.PrepareForGpuAccess();}

	virtual void MarkSamplesModifiedFromCpu()
	{ m_outdata.MarkModifiedFromCpu(); }

	virtual void MarkSamplesModifiedFromGpu()
	{ m_outdata.MarkModifiedFromGpu(); }

	virtual void MarkModifiedFromCpu()
	{ m_outdata.MarkModifiedFromCpu(); }

	virtual void MarkModifiedFromGpu()
	{ m_outdata.MarkModifiedFromGpu(); }

	virtual size_t size() const
	{ return 0; }

protected:
	size_t m_width;
	size_t m_height;

	AcceleratorBuffer<float> m_outdata;
};

#endif
