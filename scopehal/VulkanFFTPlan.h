/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

extern std::unique_ptr<vk::raii::CommandPool> g_vkFFTCommandPool;
extern std::unique_ptr<vk::raii::CommandBuffer> g_vkFFTCommandBuffer;
extern std::unique_ptr<vk::raii::Queue> g_vkFFTQueue;
extern std::mutex g_vkFFTMutex;
extern vk::raii::PhysicalDevice* g_vkfftPhysicalDevice;

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

	__attribute__((noinline))
	VulkanFFTPlan(size_t npoints, size_t nouts, VulkanFFTPlanDirection dir)
		: m_size(npoints)
		, m_fence(*g_vkComputeDevice, vk::FenceCreateInfo())
	{
		memset(&m_app, 0, sizeof(m_app));
		memset(&m_config, 0, sizeof(m_config));

		std::lock_guard<std::mutex> lock(g_vkFFTMutex);

		//Only 1D FFTs supported for now
		m_config.FFTdim = 1;
		m_config.size[0] = npoints;
		m_config.size[1] = 1;
		m_config.size[2] = 1;

		//Extract raw handles of all of our Vulkan objects
		m_physicalDevice = **g_vkfftPhysicalDevice;
		m_device = **g_vkComputeDevice;
		m_pool = **g_vkFFTCommandPool;
		m_queue = **g_vkFFTQueue;
		m_rawfence = *m_fence;

		m_config.physicalDevice = &m_physicalDevice;
		m_config.device = &m_device;
		m_config.queue = &m_queue;
		m_config.commandPool = &m_pool;
		m_config.fence = &m_rawfence;
		m_config.isCompilerInitialized = 1;
		m_config.isInputFormatted = 1;

		//We have "C" locale all the time internally, so no need to setlocale() in the library
		m_config.disableSetLocale = 1;

		m_config.performR2C = 1;				//real time domain / complex frequency domain points

		if(dir == DIRECTION_FORWARD)
		{
			m_config.makeForwardPlanOnly = 1;

			//output is complex buffer of full size
			m_bsize = 2 * nouts * sizeof(float);

			//input is real buffer of full size
			m_isize = npoints * sizeof(float);

			m_config.bufferSize = &m_bsize;
			m_config.inputBufferSize = &m_isize;
		}
		else
		{
			m_config.makeInversePlanOnly = 1;

			//input is complex buffer of full size
			m_isize = 2 * nouts * sizeof(float);

			//output is real buffer of full size
			m_bsize = npoints * sizeof(float);

			m_config.bufferSize = &m_bsize;
			m_config.inputBufferSize = &m_isize;
			m_config.inverseReturnToInputBuffer = 1;
		}

		auto err = initializeVkFFT(&m_app, m_config);
		if(VKFFT_SUCCESS != err)
			LogError("Failed to initialize vkFFT (code %d)\n", err);
	}

	__attribute__((noinline))
	void AppendForward(
		AcceleratorBuffer<float>& dataIn,
		AcceleratorBuffer<float>& dataOut,
		vk::raii::CommandBuffer& cmdBuf)
	{
		dataIn.PrepareForGpuAccess();
		dataOut.PrepareForGpuAccess();

		//Extract raw handles of all of our Vulkan objects
		VkBuffer inbuf = dataIn.GetBuffer();
		VkBuffer outbuf = dataOut.GetBuffer();
		VkCommandBuffer cmd = *cmdBuf;

		VkFFTLaunchParams params;
		memset(&params, 0, sizeof(params));
		params.inputBuffer = &inbuf;
		params.buffer = &outbuf;
		params.commandBuffer = &cmd;

		auto err = VkFFTAppend(&m_app, -1, &params);
		if(VKFFT_SUCCESS != err)
			LogError("Failed to append vkFFT transform (code %d)\n", err);

		dataOut.MarkModifiedFromGpu();
	}

	__attribute__((noinline))
	void AppendReverse(
		AcceleratorBuffer<float>& dataIn,
		AcceleratorBuffer<float>& dataOut,
		vk::raii::CommandBuffer& cmdBuf)
	{
		dataIn.PrepareForGpuAccess();
		dataOut.PrepareForGpuAccess();

		//Extract raw handles of all of our Vulkan objects
		VkBuffer inbuf = dataIn.GetBuffer();
		VkBuffer outbuf = dataOut.GetBuffer();
		VkCommandBuffer cmd = *cmdBuf;

		VkFFTLaunchParams params;
		memset(&params, 0, sizeof(params));
		params.inputBuffer = &outbuf;		//inverse transform writes to *input* buffer when returnToInputBuffer is set
		params.buffer = &inbuf;
		params.commandBuffer = &cmd;

		auto err = VkFFTAppend(&m_app, 1, &params);
		if(VKFFT_SUCCESS != err)
			LogError("Failed to append vkFFT transform (code %d)\n", err);

		dataOut.MarkModifiedFromGpu();
	}

	~VulkanFFTPlan()
	{
		deleteVkFFT(&m_app);
	}

	size_t size() const
	{ return m_size; }

protected:
	VkFFTApplication m_app;
	VkFFTConfiguration m_config;
	size_t m_size;

	//this is ugly but apparently we can't take a pointer to the underlying vk:: c++ wrapper object?
	VkPhysicalDevice m_physicalDevice;
	VkDevice m_device;
	VkCommandPool m_pool;
	VkQueue m_queue;

	vk::raii::Fence m_fence;
	VkFence m_rawfence;

	uint64_t m_bsize;
	uint64_t m_isize;
};

#endif
