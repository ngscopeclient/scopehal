/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of ComputePipeline
	@ingroup vksupport
 */

#ifndef ComputePipeline_h
#define ComputePipeline_h

#include "scopehal.h"
#include "AcceleratorBuffer.h"

/**
	@brief Encapsulates a Vulkan compute pipeline and all necessary resources to use it.

	Supported shaders must have all image bindings numerically after all SSBO bindings.

	A ComputePipeline is typically owned by a filter instance.

	Prefers KHR_push_descriptor (and some APIs are only available if it is present), but basic functionality is
	available without it.

	@ingroup vksupport
 */
class ComputePipeline
{
public:
	ComputePipeline(
		const std::string& shaderPath,
		size_t numSSBOs,
		size_t pushConstantSize,
		size_t numStorageImages = 0,
		size_t numSampledImages = 0);
	virtual ~ComputePipeline();

	void Reinitialize(
		const std::string& shaderPath,
		size_t numSSBOs,
		size_t pushConstantSize,
		size_t numStorageImages = 0,
		size_t numSampledImages = 0);

	/**
		@brief Binds an input or output SSBO to a descriptor slot

		This method performs a blocking copy from the CPU to GPU views of the buffer if they are incoherent.

		@param i			Descriptor index
		@param buf			The buffer to bind to the slot
		@param outputOnly	Hint that the shader never reads from the buffer, so there is no need to ensure coherence
							between CPU and GPU cache views of the buffer before executing the shader.
	 */
	template<class T>
	void BindBuffer(size_t i, AcceleratorBuffer<T>& buf, bool outputOnly = false)
	{
		if(m_computePipeline == nullptr)
			DeferredInit();

		buf.PrepareForGpuAccess(outputOnly);

		m_bufferInfo[i] = buf.GetBufferInfo();
		if(g_hasPushDescriptor)
		{
			m_writeDescriptors[i] =
				vk::WriteDescriptorSet(nullptr, i, 0, vk::DescriptorType::eStorageBuffer, {}, m_bufferInfo[i]);
		}
		else
		{
			m_writeDescriptors[i] =
				vk::WriteDescriptorSet(**m_descriptorSet, i, 0, vk::DescriptorType::eStorageBuffer, {}, m_bufferInfo[i]);
		}
	}

	/**
		@brief Binds a storage (output) image to a descriptor slot

		@param i			Descriptor index
		@param sampler		Vulkan sampler
		@param view			Vulkan image view
		@param layout		Vulkan image layout
	 */
	void BindStorageImage(size_t i, vk::Sampler sampler, vk::ImageView view, vk::ImageLayout layout)
	{
		if(m_computePipeline == nullptr)
			DeferredInit();

		size_t numImage = i - m_numSSBOs;
		m_storageImageInfo[numImage] = vk::DescriptorImageInfo(sampler, view, layout);

		if(g_hasPushDescriptor)
		{
			m_writeDescriptors[i] = vk::WriteDescriptorSet(
				nullptr, i, 0, vk::DescriptorType::eStorageImage, m_storageImageInfo[numImage]);
		}
		else
		{
			m_writeDescriptors[i] = vk::WriteDescriptorSet(
				**m_descriptorSet, i, 0, vk::DescriptorType::eStorageImage, m_storageImageInfo[numImage]);
		}
	}

	/**
		@brief Binds a sampled (input) image to a descriptor slot

		@param i			Descriptor index
		@param sampler		Vulkan sampler
		@param view			Vulkan image view
		@param layout		Vulkan image layout
	 */
	void BindSampledImage(size_t i, vk::Sampler sampler, vk::ImageView view, vk::ImageLayout layout)
	{
		if(m_computePipeline == nullptr)
			DeferredInit();

		size_t numImage = i - (m_numSSBOs + m_numStorageImages);
		m_sampledImageInfo[numImage] = vk::DescriptorImageInfo(sampler, view, layout);

		if(g_hasPushDescriptor)
		{
			m_writeDescriptors[i] = vk::WriteDescriptorSet(
				nullptr, i, 0, vk::DescriptorType::eCombinedImageSampler, m_sampledImageInfo[numImage]);
		}
		else
		{
			m_writeDescriptors[i] = vk::WriteDescriptorSet(
				**m_descriptorSet, i, 0, vk::DescriptorType::eCombinedImageSampler, m_sampledImageInfo[numImage]);
		}
	}

	/**
		@brief Binds an input or output SSBO to a descriptor slot

		This method performs a nonblocking copy from the CPU to GPU views of the buffer if they are incoherent.

		@param i			Descriptor index
		@param buf			The buffer to bind to the slot
		@param cmdBuf		Command buffer to append the copy operation, if needed, to
		@param outputOnly	Hint that the shader never reads from the buffer, so there is no need to ensure coherence
							between CPU and GPU cache views of the buffer before executing the shader.
	 */
	template<class T>
	void BindBufferNonblocking(
		size_t i, AcceleratorBuffer<T>& buf, vk::raii::CommandBuffer& cmdBuf, bool outputOnly = false)
	{
		if(buf.empty())
		{
			LogWarning("Attempted to bind an empty buffer\n");
			return;
		}

		if(m_computePipeline == nullptr)
			DeferredInit();

		buf.PrepareForGpuAccessNonblocking(outputOnly, cmdBuf);

		m_bufferInfo[i] = buf.GetBufferInfo();
		if(g_hasPushDescriptor)
		{
			m_writeDescriptors[i] =
				vk::WriteDescriptorSet(nullptr, i, 0, vk::DescriptorType::eStorageBuffer, {}, m_bufferInfo[i]);
		}
		else
		{
			m_writeDescriptors[i] =
				vk::WriteDescriptorSet(**m_descriptorSet, i, 0, vk::DescriptorType::eStorageBuffer, {}, m_bufferInfo[i]);
		}
	}

	/**
		@brief Helper function to insert a shader write-to-read memory barrier in a command buffer

		@param cmdBuf	Command buffer to append the pipeline barrier to
	 */
	static void AddComputeMemoryBarrier(vk::raii::CommandBuffer& cmdBuf)
	{
		cmdBuf.pipelineBarrier(
			vk::PipelineStageFlagBits::eComputeShader,
			vk::PipelineStageFlagBits::eComputeShader,
			{},
			vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead),
			{},
			{});
	}

	/**
		@brief Binds the pipeline to a command buffer

		@param cmdBuf	Command buffer to append the bind to
	 */
	void Bind(vk::raii::CommandBuffer& cmdBuf)
	{
		if(m_computePipeline == nullptr)
			DeferredInit();
		cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, **m_computePipeline);
	}

	/**
		@brief Adds a vkCmdDispatch operation to a command buffer to execute the compute shader.

		If KHR_push_descriptor is not available, performs an updateDescriptorSets. This means only one Dispatch()
		of a given ComputePipeline can be present in a command buffer at a time.

		If KHR_push_descriptor is available, performs a pushDescriptorSetKHR. In this case, arbitrarily many Dispatch()
		calls on the same ComputePipeline may be submitted to the same command buffer in sequence.

		@param cmdBuf			Command buffer to append the dispatch operation to
		@param pushConstants	Constants to pass to the shader
		@param x				X size of the dispatch, in thread blocks
		@param y				Y size of the dispatch, in thread blocks
		@param z				Z size of the dispatch, in thread blocks
	 */
	template<class T>
	void Dispatch(vk::raii::CommandBuffer& cmdBuf, T pushConstants, uint32_t x, uint32_t y=1, uint32_t z=1)
	{
		if(!g_hasPushDescriptor)
			g_vkComputeDevice->updateDescriptorSets(m_writeDescriptors, nullptr);

		Bind(cmdBuf);
		cmdBuf.pushConstants<T>(
			**m_pipelineLayout,
			vk::ShaderStageFlagBits::eCompute,
			0,
			pushConstants);

		if(g_hasPushDescriptor)
		{
			cmdBuf.pushDescriptorSetKHR(
				vk::PipelineBindPoint::eCompute,
				**m_pipelineLayout,
				0,
			m_writeDescriptors
			);
		}
		else
		{
			cmdBuf.bindDescriptorSets(
				vk::PipelineBindPoint::eCompute,
				**m_pipelineLayout,
				0,
				**m_descriptorSet,
				{});
		}
		cmdBuf.dispatch(x, y, z);
	}

	/**
		@brief Similar to Dispatch() but does not bind descriptor sets.

		This allows multiple consecutive invocations of the same shader (potentially with different dispatch sizes
		or push constant values) in the same command buffer, even without KHR_push_descriptor, as long as the same
		set of input and output descriptors are used by each invocation.

		If KHR_push_descriptor is available, performs a vkPushDescriptorSetKHR. If not, descriptors are untouched.

		@param cmdBuf			Command buffer to append the dispatch operation to
		@param pushConstants	Constants to pass to the shader
		@param x				X size of the dispatch, in thread blocks
		@param y				Y size of the dispatch, in thread blocks
		@param z				Z size of the dispatch, in thread blocks
	 */
	template<class T>
	void DispatchNoRebind(vk::raii::CommandBuffer& cmdBuf, T pushConstants, uint32_t x, uint32_t y=1, uint32_t z=1)
	{
		if(g_hasPushDescriptor)
		{
			cmdBuf.pushDescriptorSetKHR(
				vk::PipelineBindPoint::eCompute,
				**m_pipelineLayout,
				0,
			m_writeDescriptors
			);
		}

		cmdBuf.pushConstants<T>(
			**m_pipelineLayout,
			vk::ShaderStageFlagBits::eCompute,
			0,
			pushConstants);
		cmdBuf.dispatch(x, y, z);
	}

protected:
	void DeferredInit();

	///@brief Filesystem path to the compiled SPIR-V shader binary
	std::string m_shaderPath;

	///@brief Number of SSBO bindings in the shader
	size_t m_numSSBOs;

	///@brief Number of output image bindings in the shader
	size_t m_numStorageImages;

	///@brief Number of input image bindings in the shader
	size_t m_numSampledImages;

	///@brief Size of the push constants, in bytes
	size_t m_pushConstantSize;

	///@brief Handle to the shader module object
	std::unique_ptr<vk::raii::ShaderModule> m_shaderModule;

	///@brief Handle to the Vulkan compute pipeline
	std::unique_ptr<vk::raii::Pipeline> m_computePipeline;

	///@brief Layout of the compute pipeline
	std::unique_ptr<vk::raii::PipelineLayout> m_pipelineLayout;

	///@brief Layout of our descriptor set
	std::unique_ptr<vk::raii::DescriptorSetLayout> m_descriptorSetLayout;

	///@brief Pool for allocating m_descriptorSet from
	std::unique_ptr<vk::raii::DescriptorPool> m_descriptorPool;

	///@brief The actual descriptor set storing our inputs and outputs
	std::unique_ptr<vk::raii::DescriptorSet> m_descriptorSet;

	///@brief Set of bindings to be written to m_descriptorSet
	std::vector<vk::WriteDescriptorSet> m_writeDescriptors;

	///@brief Details about our SSBOs
	std::vector<vk::DescriptorBufferInfo> m_bufferInfo;

	///@brief Details about our output images
	std::vector<vk::DescriptorImageInfo> m_storageImageInfo;

	///@brief Details about our input images
	std::vector<vk::DescriptorImageInfo> m_sampledImageInfo;
};

#endif
