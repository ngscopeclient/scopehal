/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "../scopehal/scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ComputePipeline::ComputePipeline(const std::string& shaderPath, size_t numSSBOs, size_t pushConstantSize)
{
	//Load the shader module
	auto srcvec = ReadDataFileUint32(shaderPath);
	vk::ShaderModuleCreateInfo info({}, srcvec);
	m_shaderModule = make_unique<vk::raii::ShaderModule>(*g_vkComputeDevice, info);

	//Configure shader input bindings
	vector<vk::DescriptorSetLayoutBinding> bindings;
	for(size_t i=0; i<numSSBOs; i++)
	{
		bindings.push_back(vk::DescriptorSetLayoutBinding(
			i, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute));
	}
	vk::DescriptorSetLayoutCreateInfo dinfo({}, bindings);
	m_descriptorSetLayout = make_unique<vk::raii::DescriptorSetLayout>(*g_vkComputeDevice, dinfo);

	//Configure push constants
	vk::PushConstantRange range(vk::ShaderStageFlagBits::eCompute, 0, pushConstantSize);

	//Make the pipeline layout
	vk::PipelineLayoutCreateInfo linfo(
		{},
		**m_descriptorSetLayout,
		range);
	m_pipelineLayout = make_unique<vk::raii::PipelineLayout>(*g_vkComputeDevice, linfo);

	//Make the pipeline
	vk::PipelineShaderStageCreateInfo stageinfo({}, vk::ShaderStageFlagBits::eCompute, **m_shaderModule, "main");
	vk::ComputePipelineCreateInfo pinfo({}, stageinfo, **m_pipelineLayout);
	m_computePipeline = make_unique<vk::raii::Pipeline>(
		std::move(g_vkComputeDevice->createComputePipelines(nullptr, pinfo).front()));	//TODO: pipeline cache

	//Descriptor pool for our shader parameters
	vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, numSSBOs);
	vk::DescriptorPoolCreateInfo poolInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
			vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
		1,
		poolSize);
	m_descriptorPool = make_unique<vk::raii::DescriptorPool>(*g_vkComputeDevice, poolInfo);

	//Set up descriptors for our buffers
	vk::DescriptorSetAllocateInfo dsinfo(**m_descriptorPool, **m_descriptorSetLayout);
	m_descriptorSet = make_unique<vk::raii::DescriptorSet>(
		std::move(vk::raii::DescriptorSets(*g_vkComputeDevice, dsinfo).front()));

	m_writeDescriptors.resize(numSSBOs);
	m_bufferInfo.resize(numSSBOs);
}

ComputePipeline::~ComputePipeline()
{
	//Make sure we destroy some objects in a particular order
	//TODO: how much of this really is important?
	m_computePipeline = nullptr;
	m_descriptorSetLayout = nullptr;
	m_pipelineLayout = nullptr;
	m_shaderModule = nullptr;
}
