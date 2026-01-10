/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of DensityFunctionWaveform

	@ingroup datamodel
 */

#ifndef DensityFunctionWaveform_h
#define DensityFunctionWaveform_h

#include "Waveform.h"

/**
	@brief Base class for waveforms such as eye patterns, spectrograms, and waterfalls which are conceptually a 2D bitmap
	@ingroup datamodel

	Internally, the image data is represented as an AcceleratorBuffer<float> storing one float32 sample values per
	pixel in row major order (samples 0...width-1 of row 0, then samples 0...width-1 of row 1, etc).
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
	virtual void Rename([[maybe_unused]] const std::string& name = "") override
	{}

	///@brief Returns a pointer to the CPU-side sample data buffer
	float* GetData()
	{
		m_outdata.PrepareForCpuAccess();
		return m_outdata.GetCpuPointer();
	}

	///@brief Returns a reference to the output data buffer object
	AcceleratorBuffer<float>& GetOutData()
	{ return m_outdata; }

	///@brief Returns the height of the bitmap in pixels
	size_t GetHeight()
	{ return m_height; }

	///@brief Returns the width of the bitmap in pixels
	size_t GetWidth()
	{ return m_width; }

	//Unused virtual methods from WaveformBase that we have to override
	virtual void clear() override
	{}

	virtual void Resize([[maybe_unused]] size_t unused) override
	{}

	virtual void Reserve([[maybe_unused]] size_t unused) override
	{}

	virtual void PrepareForCpuAccess() override
	{ m_outdata.PrepareForCpuAccess(); }

	virtual void PrepareForGpuAccess() override
	{ m_outdata.PrepareForGpuAccess();}

	virtual void PrepareForGpuAccessNonblocking(vk::raii::CommandBuffer& cmdBuf) override
	{ m_outdata.PrepareForGpuAccessNonblocking(false, cmdBuf);}

	virtual void MarkSamplesModifiedFromCpu() override
	{ m_outdata.MarkModifiedFromCpu(); }

	virtual void MarkSamplesModifiedFromGpu() override
	{ m_outdata.MarkModifiedFromGpu(); }

	virtual void MarkModifiedFromCpu() override
	{ m_outdata.MarkModifiedFromCpu(); }

	virtual void MarkModifiedFromGpu() override
	{ m_outdata.MarkModifiedFromGpu(); }

	//we have no linear sample buffer so return 0
	virtual size_t size() const override
	{ return 0; }

protected:

	///@brief Buffer width, in pixels
	size_t m_width;

	///@brief Buffer height, in pixels
	size_t m_height;

	///@brief Pixel buffer
	AcceleratorBuffer<float> m_outdata;
};

#endif
