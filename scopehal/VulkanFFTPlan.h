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
	@brief Declaration of VulkanFFTPlan
	@ingroup core
 */
#ifndef VulkanFFTPlan_h
#define VulkanFFTPlan_h

//Lots of warnings here, disable them
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <vkFFT.h>
#pragma GCC diagnostic pop

#include "AcceleratorBuffer.h"
#include "PipelineCacheManager.h"

/**
	@brief Arguments to a window function for FFT processing
	@ingroup core
 */
struct WindowFunctionArgs
{
	///@brief Number of samples in the input
	uint32_t numActualSamples;

	///@brief Number of FFT points
	uint32_t npoints;

	///@brief Offset from start of the input buffer to start reading from
	uint32_t offsetIn;

	///@brief Offset from start of the output buffer to start writing to
	uint32_t offsetOut;

	///@brief Scaling factor for normalization
	float scale;

	///@brief Alpha0 factor for cosine-sum windows, ignored for others
	float alpha0;

	///@brief Alpha1 factor for cosine-sum windows, ignored for others
	float alpha1;
};

/**
	@brief Arguments for normalizing output of a de-embed
	@ingroup core
 */
struct DeEmbedNormalizationArgs
{
	///@brief Length of the output buffer, in samples
	uint32_t outlen;

	///@brief Starting sample index
	uint32_t istart;

	///@brief Scaling factor for normalization
	float scale;
};

/**
	@brief RAII wrapper around a VkFFTApplication and VkFFTConfiguration
	@ingroup core
 */
class VulkanFFTPlan
{
public:

	///@brief Direction of a FFT
	enum VulkanFFTPlanDirection
	{
		///@brief Normal FFT
		DIRECTION_FORWARD,

		///@brief Inverse FFT
		DIRECTION_REVERSE
	};

	///@brief Data type of a FFT input or output
	enum VulkanFFTDataType
	{
		///@brief Real float32 values
		TYPE_REAL,

		///@brief Complex float32 values
		TYPE_COMPLEX
	};

	VulkanFFTPlan(
		size_t npoints,
		size_t nouts,
		VulkanFFTPlanDirection dir,
		size_t numBatches = 1,
		VulkanFFTDataType timeDomainType = VulkanFFTPlan::TYPE_REAL);
	~VulkanFFTPlan();

	void AppendForward(
		AcceleratorBuffer<float>& dataIn,
		AcceleratorBuffer<float>& dataOut,
		vk::raii::CommandBuffer& cmdBuf);

	void AppendReverse(
		AcceleratorBuffer<float>& dataIn,
		AcceleratorBuffer<float>& dataOut,
		vk::raii::CommandBuffer& cmdBuf);

	///@brief Return the number of points in the FFT
	size_t size() const
	{ return m_size; }

protected:

	///@brief VkFFT application handle
	VkFFTApplication m_app;

	///@brief VkFFT configuration state
	VkFFTConfiguration m_config;

	///@brief Number of points in the FFT
	size_t m_size;

	//this is ugly but apparently we can't take a pointer to the underlying vk:: c++ wrapper objects?
	///@brief Physical device the FFT is runnning on
	VkPhysicalDevice m_physicalDevice;

	///@brief Device the FFT is runnning on
	VkDevice m_device;

	///@brief Pipeline cache for precompiled shader binaries
	VkPipelineCache m_pipelineCache;

	///@brief Fence for synchronizing FFTs
	vk::raii::Fence m_fence;

	///@brief The underlying VkFence of m_fence (we need to be able to take a pointer to it)
	VkFence m_rawfence;

	///@brief Byte size of the output buffer (for forward) or input (for reverse)
	uint64_t m_bsize;

	///@brief Byte size of the temporary working buffer
	uint64_t m_tsize;

	///@brief Byte size of the input buffer (for forward) or output (for reverse)
	uint64_t m_isize;
};

#endif
