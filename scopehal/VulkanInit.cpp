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
	@brief Vulkan initialization
 */
#include "scopehal.h"

using namespace std;

/**
	@brief Global Vulkan context
 */
vk::raii::Context g_vkContext;

/**
	@brief Global Vulkan instance
 */
unique_ptr<vk::raii::Instance> g_vkInstance;

/**
	@brief The Vulkan device selected for compute operations (may or may not be same device as rendering)
 */
unique_ptr<vk::raii::Device> g_vkComputeDevice;

/**
	@brief Command pool for AcceleratorBuffer transfers

	This is a single global resource interlocked by g_vkTransferMutex and is used for convenience and code simplicity
	when parallelism isn't that important.
 */
unique_ptr<vk::raii::CommandPool> g_vkTransferCommandPool;

/**
	@brief Command buffer for AcceleratorBuffer transfers

	This is a single global resource interlocked by g_vkTransferMutex and is used for convenience and code simplicity
	when parallelism isn't that important.
 */
std::unique_ptr<vk::raii::CommandBuffer> g_vkTransferCommandBuffer;

/**
	@brief Queue for AcceleratorBuffer transfers

	This is a single global resource interlocked by g_vkTransferMutex and is used for convenience and code simplicity
	when parallelism isn't that important.
 */
std::unique_ptr<vk::raii::Queue> g_vkTransferQueue;

/**
	@brief Mutex for interlocking access to g_vkTransferCommandBuffer and g_vkTransferCommandPool
 */
std::mutex g_vkTransferMutex;

/**
	@brief Vulkan memory type for CPU-based memory that is also GPU-readable
 */
size_t g_vkPinnedMemoryType;

/**
	@brief Vulkan memory type for GPU-based memory (generally not CPU-readable, except on integrated cards)
 */
size_t g_vkLocalMemoryType;

/**
	@brief Vulkan queue type for submitting compute operations (may or may not be render capable)
 */
size_t g_computeQueueType;

bool IsDevicePreferred(const vk::PhysicalDeviceProperties& a, const vk::PhysicalDeviceProperties& b);

bool g_hasShaderInt64 = false;
bool g_hasShaderInt16 = false;

/**
	@brief Initialize a Vulkan context for compute
 */
bool VulkanInit()
{
	LogDebug("Initializing Vulkan\n");
	LogIndenter li;

	try
	{
		//Vulkan 1.1 is the highest version supported on all targeted platforms (limited mostly by MoltenVK)
		//If we want to support llvmpipe, we need to stick to 1.0
		vk::ApplicationInfo appInfo("libscopehal", 1, "Vulkan.hpp", 1, VK_API_VERSION_1_1);
		vk::InstanceCreateInfo instanceInfo({}, &appInfo);

		//Create the instance
		g_vkInstance = make_unique<vk::raii::Instance>(g_vkContext, instanceInfo);

		//Look at our physical devices and print info out for each one
		LogDebug("Physical devices:\n");
		{
			LogIndenter li2;

			size_t bestDevice = 0;

			vk::raii::PhysicalDevices devices(*g_vkInstance);
			for(size_t i=0; i<devices.size(); i++)
			{
				auto device = devices[i];
				auto features = device.getFeatures();
				auto properties = device.getProperties();
				auto memProperties = device.getMemoryProperties();
				auto limits = properties.limits;

				//See what device to use
				//TODO: preference to override this
				if(IsDevicePreferred(devices[bestDevice].getProperties(), devices[i].getProperties()))
					bestDevice = i;

				//TODO: sparse properties

				LogDebug("Device %zu: %s\n", i, &properties.deviceName[0]);
				LogIndenter li3;

				LogDebug("API version:            0x%08x (%d.%d.%d.%d)\n",
					properties.apiVersion,
					(properties.apiVersion >> 29),
					(properties.apiVersion >> 22) & 0x7f,
					(properties.apiVersion >> 12) & 0x3ff,
					(properties.apiVersion >> 0) & 0xfff
					);

				//Driver version is NOT guaranteed to be encoded the same way as the API version.
				if(properties.vendorID == 0x10de)	//NVIDIA
				{
					LogDebug("Driver version:         0x%08x (%d.%d.%d.%d)\n",
						properties.driverVersion,
						(properties.driverVersion >> 22),
						(properties.driverVersion >> 14) & 0xff,
						(properties.driverVersion >> 6) & 0xff,
						(properties.driverVersion >> 0) & 0x3f
						);
				}

				//By default, assume it's the same as API
				else
				{
					LogDebug("Driver version:         0x%08x (%d.%d.%d.%d)\n",
						properties.driverVersion,
						(properties.driverVersion >> 29),
						(properties.driverVersion >> 22) & 0x7f,
						(properties.driverVersion >> 12) & 0x3ff,
						(properties.driverVersion >> 0) & 0xfff
						);
				}

				LogDebug("Vendor ID:              %04x\n", properties.vendorID);
				LogDebug("Device ID:              %04x\n", properties.deviceID);
				switch(properties.deviceType)
				{
					case vk::PhysicalDeviceType::eIntegratedGpu:
						LogDebug("Device type:            Integrated GPU\n");
						break;

					case vk::PhysicalDeviceType::eDiscreteGpu:
						LogDebug("Device type:            Discrete GPU\n");
						break;

					case vk::PhysicalDeviceType::eVirtualGpu:
						LogDebug("Device type:            Virtual GPU\n");
						break;

					case vk::PhysicalDeviceType::eCpu:
						LogDebug("Device type:            CPU\n");
						break;

					default:
					case vk::PhysicalDeviceType::eOther:
						LogDebug("Device type:            Other\n");
						break;
				}

				if(features.shaderInt64)
					LogDebug("int64:                  yes\n");
				else
					LogDebug("int64:                  no\n");

				if(features.shaderInt16)
					LogDebug("int16:                  yes\n");
				else
					LogDebug("int16:                  no\n");

				const size_t k = 1024LL;
				const size_t m = k*k;
				const size_t g = k*m;

				LogDebug("Max image dim 2D:       %u\n", limits.maxImageDimension2D);
				LogDebug("Max storage buf range:  %lu MB\n", limits.maxStorageBufferRange / m);
				LogDebug("Max mem alloc:          %lu MB\n", limits.maxMemoryAllocationCount / m);
				LogDebug("Max compute shared mem: %lu KB\n", limits.maxComputeSharedMemorySize / k);
				LogDebug("Max compute grp count:  %u x %u x %u\n",
					limits.maxComputeWorkGroupCount[0],
					limits.maxComputeWorkGroupCount[1],
					limits.maxComputeWorkGroupCount[2]);
				LogDebug("Max compute invocs:     %u\n", limits.maxComputeWorkGroupInvocations);
				LogDebug("Max compute grp size:   %u x %u x %u\n",
					limits.maxComputeWorkGroupSize[0],
					limits.maxComputeWorkGroupSize[1],
					limits.maxComputeWorkGroupSize[2]);

				LogDebug("Memory types:\n");
				for(size_t j=0; j<memProperties.memoryTypeCount; j++)
				{
					auto mtype = memProperties.memoryTypes[j];

					LogIndenter li4;
					LogDebug("Type %zu\n", j);
					LogIndenter li5;

					LogDebug("Heap index: %u\n", mtype.heapIndex);
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
						LogDebug("Device local\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
						LogDebug("Host visible\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent)
						LogDebug("Host coherent\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eHostCached)
						LogDebug("Host cached\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eLazilyAllocated)
						LogDebug("Lazily allocated\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eProtected)
						LogDebug("Protected\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceCoherentAMD)
						LogDebug("Device coherent\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceUncachedAMD)
						LogDebug("Device uncached\n");
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eRdmaCapableNV)
						LogDebug("RDMA capable\n");
				}

				LogDebug("Memory heaps:\n");
				for(size_t j=0; j<memProperties.memoryHeapCount; j++)
				{
					LogIndenter li4;
					LogDebug("Heap %zu\n", j);
					LogIndenter li5;
					auto heap = memProperties.memoryHeaps[j];

					if(heap.size > g)
						LogDebug("Size: %zu GB\n", heap.size / g);
					else if(heap.size > m)
						LogDebug("Size: %zu MB\n", heap.size / m);
					else if(heap.size > k)
						LogDebug("Size: %zu kB\n", heap.size / k);
					else
						LogDebug("Size: %zu B\n", heap.size);

					if(heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal)
						LogDebug("Device local\n");
					if(heap.flags & vk::MemoryHeapFlagBits::eMultiInstance)
						LogDebug("Multi instance\n");
					if(heap.flags & vk::MemoryHeapFlagBits::eMultiInstanceKHR)
						LogDebug("Multi instance (KHR)\n");
				}
			}

			LogDebug("Selected device %zu\n", bestDevice);
			{
				auto device = devices[bestDevice];

				LogIndenter li3;

				//Look at queue families
				auto families = device.getQueueFamilyProperties();
				LogDebug("Queue families\n");
				{
					LogIndenter li4;
					g_computeQueueType = 0;
					for(size_t j=0; j<families.size(); j++)
					{
						LogDebug("Queue type %zu\n", j);
						LogIndenter li5;

						auto f = families[j];
						LogDebug("Queue count:          %d\n", f.queueCount);
						LogDebug("Timestamp valid bits: %d\n", f.timestampValidBits);
						if(f.queueFlags & vk::QueueFlagBits::eGraphics)
							LogDebug("Graphics\n");
						if(f.queueFlags & vk::QueueFlagBits::eCompute)
							LogDebug("Compute\n");
						if(f.queueFlags & vk::QueueFlagBits::eTransfer)
							LogDebug("Transfer\n");
						if(f.queueFlags & vk::QueueFlagBits::eSparseBinding)
							LogDebug("Sparse binding\n");
						if(f.queueFlags & vk::QueueFlagBits::eProtected)
							LogDebug("Protected\n");
						//TODO: VIDEO_DECODE_BIT_KHR, VIDEO_ENCODE_BIT_KHR

						//Pick the first type that supports compute and transfers
						if( (f.queueFlags & vk::QueueFlagBits::eCompute) && (f.queueFlags & vk::QueueFlagBits::eTransfer) )
						{
							g_computeQueueType = j;
							break;
						}
					}
				}

				//See if the device has int64 support. If so, enable it
				vk::PhysicalDeviceFeatures enabledFeatures;
				if(device.getFeatures().shaderInt64)
				{
					enabledFeatures.shaderInt64 = true;
					g_hasShaderInt64 = true;
					LogDebug("Enabling 64-bit integer support\n");
				}
				if(device.getFeatures().shaderInt16)
				{
					enabledFeatures.shaderInt16 = true;
					g_hasShaderInt16 = true;
					LogDebug("Enabling 16-bit integer support\n");
				}

				//Initialize the device
				float queuePriority = 0;
				vk::DeviceQueueCreateInfo qinfo( {}, g_computeQueueType, 1, &queuePriority);
				vk::DeviceCreateInfo devinfo(
					{},
					qinfo,
					{},
					{},
					&enabledFeatures
					);
				g_vkComputeDevice = make_unique<vk::raii::Device>(device, devinfo);

				//Figure out what memory types to use for various purposes
				bool foundPinnedType = false;
				bool foundLocalType = false;
				g_vkPinnedMemoryType = 0;
				g_vkLocalMemoryType = 0;
				auto memProperties = device.getMemoryProperties();
				auto devtype = device.getProperties().deviceType;
				for(size_t j=0; j<memProperties.memoryTypeCount; j++)
				{
					auto mtype = memProperties.memoryTypes[j];

					//Pinned memory is host visible, host coherent, host cached, and usually not device local
					//Use the first type we find
					if(
						(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) &&
						(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent) &&
						(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eHostCached) )
					{
						//Device local? This is a disqualifier UNLESS we are an integrated card or CPU
						//(in which case we have shared memory)
						if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
						{
							if( (devtype != vk::PhysicalDeviceType::eIntegratedGpu) &&
								(devtype != vk::PhysicalDeviceType::eCpu ) )
							{
								continue;
							}
						}

						if(!foundPinnedType)
						{
							foundPinnedType = true;
							g_vkPinnedMemoryType = j;
						}
					}

					//Local memory is device local
					//Use the first type we find
					if(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
					{
						//Exclude any types that are host visible unless we're an integrated card
						//(Host visible + device local memory is generally limited and
						if( (devtype != vk::PhysicalDeviceType::eIntegratedGpu) &&
							(mtype.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) )
						{
							continue;
						}

						if(!foundLocalType)
						{
							foundLocalType = true;
							g_vkLocalMemoryType = j;
						}
					}
				}

				LogDebug("Using type %zu for pinned host memory\n", g_vkPinnedMemoryType);
				LogDebug("Using type %zu for card-local memory\n", g_vkLocalMemoryType);

				//Make a CommandPool
				vk::CommandPoolCreateInfo poolInfo(
					vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
					g_computeQueueType );
				g_vkTransferCommandPool = make_unique<vk::raii::CommandPool>(*g_vkComputeDevice, poolInfo);

				//Make a CommandBuffer for memory transfers that we can use implicitly during buffer management
				vk::CommandBufferAllocateInfo bufinfo(**g_vkTransferCommandPool, vk::CommandBufferLevel::ePrimary, 1);
				g_vkTransferCommandBuffer = make_unique<vk::raii::CommandBuffer>(
					std::move(vk::raii::CommandBuffers(*g_vkComputeDevice, bufinfo).front()));

				//Make a Queue for memory transfers that we can use implicitly during buffer management
				g_vkTransferQueue = make_unique<vk::raii::Queue>(*g_vkComputeDevice, g_computeQueueType, 0);
			}
		}
	}
	catch ( vk::SystemError & err )
	{
		LogError("vk::SystemError: %s\n", err.what());
		return false;
	}
	catch ( std::exception & err )
	{
		LogError("std::exception: %s\n", err.what());
		return false;
	}
	catch(...)
	{
		LogError("unknown exception\n");
		return false;
	}

	LogDebug("\n");

	//If we get here, everything is good
	g_gpuFilterEnabled = true;

	return true;
}

/**
	@brief Checks if a given Vulkan device is "better" than another

	True if we should use device B over A
 */
bool IsDevicePreferred(const vk::PhysicalDeviceProperties& a, const vk::PhysicalDeviceProperties& b)
{
	//If B is a discrete GPU, always prefer it
	//TODO: prefer one of multiple
	if(b.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
		return true;

	//Integrated GPUs beat anything but a discrete GPU
	if( (b.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) &&
		(a.deviceType != vk::PhysicalDeviceType::eDiscreteGpu) )
	{
		return true;
	}

	//Anything is better than a CPU
	if(a.deviceType == vk::PhysicalDeviceType::eCpu)
		return false;

	//By default, assume A is good enough
	return false;
}
