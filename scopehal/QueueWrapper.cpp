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
	@brief Vulkan queue management
 */

#include <vulkan/vulkan_raii.hpp>

#include "log.h"
#include "QueueWrapper.h"

using namespace std;
extern bool g_hasDebugUtils;

QueueWrapper::QueueWrapper(std::shared_ptr<vk::raii::Device> device, size_t family, size_t index, string name)
	: m_family(family)
	, m_index(index)
	, m_mutex()
	, m_name()
	, m_device(device)
	, m_queue(make_unique<vk::raii::Queue>(*device, family, index))
{
	AddName(name);

	//Try to name the queue
	if(g_hasDebugUtils)
	{
		m_device->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eQueue,
				reinterpret_cast<uint64_t>(static_cast<VkQueue>(**m_queue)),
				m_name.c_str()));
	}
}

QueueWrapper::~QueueWrapper()
{
	const lock_guard<recursive_mutex> lock(m_mutex);

	//make sure vulkan objects are destroyed in the correct order
	m_queue = nullptr;
	m_device = nullptr;
}

void QueueWrapper::AddName(string name)
{
	const lock_guard<recursive_mutex> lock(m_mutex);

	//Check if the name is already in the list
	if(m_name.find(name) != string::npos)
		return;

	if(m_name.size() != 0)
		m_name += ";";
	m_name += name;

	if(g_hasDebugUtils)
	{
		m_device->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eQueue,
				reinterpret_cast<uint64_t>(static_cast<VkQueue>(**m_queue)),
				m_name.c_str()));
	}
}
