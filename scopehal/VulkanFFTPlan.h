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
	@brief Declaration of VulkanFFTPlan
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
	@brief RAII wrapper around a VkFFTApplication and VkFFTConfiguration
 */
class VulkanFFTPlan
{
public:

	enum VulkanFFTPlanDirection
	{
		DIRECTION_FORWARD,
		DIRECTION_REVERSE
	};

	VulkanFFTPlan(size_t npoints, size_t nouts, VulkanFFTPlanDirection dir);
	~VulkanFFTPlan();

	void AppendForward(
		AcceleratorBuffer<float>& dataIn,
		AcceleratorBuffer<float>& dataOut,
		vk::raii::CommandBuffer& cmdBuf,
		uint64_t offsetIn = 0,
		uint64_t offsetOut = 0);

	void AppendReverse(
		AcceleratorBuffer<float>& dataIn,
		AcceleratorBuffer<float>& dataOut,
		vk::raii::CommandBuffer& cmdBuf);

	size_t size() const
	{ return m_size; }

protected:
	VkFFTApplication m_app;
	VkFFTConfiguration m_config;
	size_t m_size;

	//this is ugly but apparently we can't take a pointer to the underlying vk:: c++ wrapper objects?
	VkPhysicalDevice m_physicalDevice;
	VkDevice m_device;
	VkPipelineCache m_pipelineCache;

	vk::raii::Fence m_fence;
	VkFence m_rawfence;

	uint64_t m_bsize;
	uint64_t m_isize;
};

#endif
