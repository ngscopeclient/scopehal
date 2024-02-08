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
	@brief Vulkan initialization
 */
#include "scopehal.h"
#include <glslang_c_interface.h>
#include "PipelineCacheManager.h"
#include "QueueManager.h"
#include <GLFW/glfw3.h>

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
shared_ptr<vk::raii::Device> g_vkComputeDevice;

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
unique_ptr<vk::raii::CommandBuffer> g_vkTransferCommandBuffer;

/**
	@brief Queue for AcceleratorBuffer transfers

	This is a single global resource interlocked by g_vkTransferMutex and is used for convenience and code simplicity
	when parallelism isn't that important.
 */
shared_ptr<QueueHandle> g_vkTransferQueue;

/**
 * @brief Allocates QueueHandle objects
 *
 * This is a single global resource, all QueueHandles must be obtained through this object.
 */
std::unique_ptr<QueueManager> g_vkQueueManager;

/**
	@brief Mutex for interlocking access to g_vkTransferCommandBuffer and g_vkTransferCommandPool
 */
mutex g_vkTransferMutex;

/**
	@brief Vulkan memory type for CPU-based memory that is also GPU-readable
 */
uint32_t g_vkPinnedMemoryType;

/**
	@brief Vulkan memory type for GPU-based memory (generally not CPU-readable, except on integrated cards)
 */
uint32_t g_vkLocalMemoryType;

/**
	@brief UUID of g_vkComputeDevice
 */
uint8_t g_vkComputeDeviceUuid[16];

/**
	@brief Driver version of g_vkComputeDevice
 */
uint32_t g_vkComputeDeviceDriverVer;

/**
	@brief Physical device for g_vkComputeDevice
 */
vk::raii::PhysicalDevice* g_vkComputePhysicalDevice;

/**
	@brief Heap from which g_vkPinnedMemoryType is allocated
 */
uint32_t g_vkPinnedMemoryHeap = 0;

/**
	@brief Heap from which g_vkLocalMemoryType is allocated
 */
uint32_t g_vkLocalMemoryHeap = 0;

bool IsDevicePreferred(const vk::PhysicalDeviceProperties& a, const vk::PhysicalDeviceProperties& b);

//Feature flags indicating that we have support for specific data types / features on the GPU
bool g_hasShaderFloat64 = false;
bool g_hasShaderInt64 = false;
bool g_hasShaderInt16 = false;
bool g_hasShaderInt8 = false;
bool g_hasShaderAtomicFloat = false;
bool g_hasDebugUtils = false;
bool g_hasMemoryBudget = false;
bool g_hasPushDescriptor = false;

//Max compute group count in each direction
size_t g_maxComputeGroupCount[3] = {0};

//Feature flags indicating specific drivers, for bug workarounds
bool g_vulkanDeviceIsIntelMesa = false;
bool g_vulkanDeviceIsAnyMesa = false;
bool g_vulkanDeviceIsMoltenVK = false;

void VulkanCleanup();

/**
	@brief Initialize a Vulkan context for compute

	@param skipGLFW Do not initalize GLFW (workaround for what looks like gtk or video driver bug).
			This should only be set true in glscopeclient.
 */
bool VulkanInit(bool skipGLFW)
{
	LogDebug("Initializing Vulkan\n");
	LogIndenter li;

	try
	{
		auto extensions = g_vkContext.enumerateInstanceExtensionProperties();
		bool hasPhysicalDeviceProperties2 = false;
		bool hasXlibSurface = false;
		bool hasXcbSurface = false;
		for(auto e : extensions)
		{
			if(!strcmp((char*)e.extensionName, "VK_KHR_get_physical_device_properties2"))
			{
				LogDebug("VK_KHR_get_physical_device_properties2: supported\n");
				hasPhysicalDeviceProperties2 = true;
			}
			if(!strcmp((char*)e.extensionName, "VK_EXT_debug_utils"))
			{
				LogDebug("VK_EXT_debug_utils: supported\n");
				g_hasDebugUtils = true;
			}
			if(!strcmp((char*)e.extensionName, "VK_KHR_xcb_surface"))
			{
				LogDebug("VK_KHR_xcb_surface: supported\n");
				hasXcbSurface = true;
			}
			if(!strcmp((char*)e.extensionName, "VK_KHR_xlib_surface"))
			{
				LogDebug("VK_KHR_xlib_surface: supported\n");
				hasXlibSurface = true;
			}
		}

		//Vulkan 1.1 is the highest version supported on all targeted platforms (limited mostly by MoltenVK)
		//But if Vulkan 1.2 is available, request it.
		//TODO: If we want to support llvmpipe, we need to stick to 1.0
		auto apiVersion = VK_API_VERSION_1_1;
		auto availableVersion = g_vkContext.enumerateInstanceVersion();
		uint32_t loader_major = VK_VERSION_MAJOR(availableVersion);
		uint32_t loader_minor = VK_VERSION_MINOR(availableVersion);
		bool vulkan11Available = false;
		bool vulkan12Available = false;
		LogDebug("Loader/API support available for Vulkan %d.%d\n", loader_major, loader_minor);
		if( (loader_major >= 1) || ( (loader_major == 1) && (loader_minor >= 2) ) )
		{
			apiVersion = VK_API_VERSION_1_2;
			vulkan12Available = true;
			vulkan11Available = true;
			LogDebug("Vulkan 1.2 support available, requesting it\n");
		}
		else
			LogDebug("Vulkan 1.2 support not available\n");

		if(skipGLFW)
			LogDebug("Skipping GLFW init to work around gtk gl/vulkan interop bug\n");
		else
		{
			//Log glfw version
			LogDebug("Initializing glfw %s\n", glfwGetVersionString());

			//Initialize glfw
			glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, GLFW_FALSE);
			glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
			if(!glfwInit())
			{
				LogError("glfw init failed\n");
				return false;
			}
			if(!glfwVulkanSupported())
			{
				LogError("glfw vulkan support not available\n");
				return false;
			}
		}

		//Request VK_KHR_get_physical_device_properties2 if available, plus all extensions needed by glfw
		vk::ApplicationInfo appInfo("libscopehal", 1, "Vulkan.hpp", 1, apiVersion);
		vector<const char*> extensionsToUse;
		if(hasPhysicalDeviceProperties2)
			extensionsToUse.push_back("VK_KHR_get_physical_device_properties2");
		if(hasXlibSurface)
			extensionsToUse.push_back("VK_KHR_xlib_surface");
		if(hasXcbSurface)
			extensionsToUse.push_back("VK_KHR_xcb_surface");
		extensionsToUse.push_back("VK_KHR_surface");

		//Request debug utilities if available
		if(g_hasDebugUtils)
			extensionsToUse.push_back("VK_EXT_debug_utils");

		//Required for MoltenVK
		#ifdef __APPLE__
		extensionsToUse.push_back("VK_KHR_portability_enumeration");
		#endif

		//See what extensions are required
		if(!skipGLFW)
		{
			uint32_t glfwRequiredCount = 0;
			auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwRequiredCount);
			if(glfwExtensions == nullptr)
			{
				LogError("glfwGetRequiredInstanceExtensions failed\n");
				return false;
			}
			LogDebug("GLFW required extensions:\n");
			for(size_t i=0; i<glfwRequiredCount; i++)
			{
				LogIndenter li2;
				LogDebug("%s\n", glfwExtensions[i]);
				extensionsToUse.push_back(glfwExtensions[i]);
			}
		}

		//Create the instance
		vk::InstanceCreateFlags flags = {};
		#ifdef __APPLE__
		flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
		#endif
		vk::InstanceCreateInfo instanceInfo(flags, &appInfo, {}, extensionsToUse);
		g_vkInstance = make_unique<vk::raii::Instance>(g_vkContext, instanceInfo);

		//Look at our physical devices and print info out for each one
		LogDebug("Physical devices:\n");
		{
			LogIndenter li2;

			size_t bestDevice = 0;

			static vk::raii::PhysicalDevices devices(*g_vkInstance);
			for(size_t i=0; i<devices.size(); i++)
			{
				auto& device = devices[i];
				auto features = device.getFeatures();
				auto properties = device.getProperties();
				auto memProperties = device.getMemoryProperties();
				auto limits = properties.limits;

				//See what device to use
				//TODO: preference to override this
				if(IsDevicePreferred(devices[bestDevice].getProperties(), devices[i].getProperties()))
					bestDevice = i;

				//TODO: check that the extensions we need are supported

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

				if(hasPhysicalDeviceProperties2)
				{
					//Get more details
					auto features2 = device.getFeatures2<
						vk::PhysicalDeviceFeatures2,
						vk::PhysicalDevice16BitStorageFeatures,
						vk::PhysicalDevice8BitStorageFeatures,
						vk::PhysicalDeviceVulkan12Features
						>();
					auto storageFeatures16 = std::get<1>(features2);
					auto storageFeatures8 = std::get<2>(features2);
					auto vulkan12Features = std::get<3>(features2);

					if(features.shaderInt16)
					{
						if(storageFeatures16.storageBuffer16BitAccess && storageFeatures16.uniformAndStorageBuffer16BitAccess)
							LogDebug("int16:                  yes (allowed in SSBOs)\n");
						else
							LogDebug("int16:                  yes (but not allowed in SSBOs)\n");
					}
					else
						LogDebug("int16:                  no\n");

					if(vulkan12Features.shaderInt8)
					{
						if(storageFeatures8.uniformAndStorageBuffer8BitAccess)
							LogDebug("int8:                   yes (allowed in SSBOs)\n");
						else
							LogDebug("int8:                   yes (but not allowed in SSBOs)\n");
					}
					else
						LogDebug("int8:                   no\n");
				}

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
				for(int j=0; j<3; j++)
					g_maxComputeGroupCount[j] = limits.maxComputeWorkGroupCount[j];
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
						LogDebug("Size: %" PRIu64 " GB\n", heap.size / g);
					else if(heap.size > m)
						LogDebug("Size: %" PRIu64 " MB\n", heap.size / m);
					else if(heap.size > k)
						LogDebug("Size: %" PRIu64 " kB\n", heap.size / k);
					else
						LogDebug("Size: %" PRIu64 " B\n", heap.size);

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
				auto& device = devices[bestDevice];
				g_vkComputePhysicalDevice = &devices[bestDevice];

				LogIndenter li3;

				//Look at queue families
				auto families = device.getQueueFamilyProperties();
				LogDebug("Queue families (%zu total)\n", families.size());
				{
					LogIndenter li4;
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
						#ifdef VK_ENABLE_BETA_EXTENSIONS
							if(f.queueFlags & vk::QueueFlagBits::eVideoDecodeKHR)
								LogDebug("Video decode\n");
							if(f.queueFlags & vk::QueueFlagBits::eVideoEncodeKHR)
								LogDebug("Video encode\n");
						#endif
					}
				}

				//Save settings
				auto properties = device.getProperties();
				g_vkComputeDeviceDriverVer = properties.driverVersion;
				memcpy(g_vkComputeDeviceUuid, properties.pipelineCacheUUID, 16);

				//Detect driver (used by some workarounds for bugs etc)
				if(vulkan11Available)
				{
					auto features2 = device.getProperties2<
						vk::PhysicalDeviceProperties2,
						vk::PhysicalDeviceDriverProperties
						>();
					auto driverProperties = std::get<1>(features2);

					//Identify driver
					g_vulkanDeviceIsIntelMesa = false;
					g_vulkanDeviceIsAnyMesa = false;
					g_vulkanDeviceIsMoltenVK = false;
					switch(driverProperties.driverID)
					{
						case vk::DriverId::eIntelOpenSourceMESA:
							g_vulkanDeviceIsIntelMesa = true;
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eIntelOpenSourceMESA\n");
							break;

						case vk::DriverId::eMesaRadv:
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eMesaRadv\n");
							break;

						case vk::DriverId::eMesaLlvmpipe:
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eMesaLlvmpipe\n");
							break;

						case vk::DriverId::eMesaTurnip:
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eMesaTurnip\n");
							break;

						case vk::DriverId::eMesaV3Dv:
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eMesaV3Dv\n");
							break;

						case vk::DriverId::eMesaPanvk:
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eMesaPanvk\n");
							break;

						case vk::DriverId::eMesaVenus:
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eMesaVenus\n");
							break;

						case vk::DriverId::eMesaDozen:
							g_vulkanDeviceIsAnyMesa = true;
							LogDebug("Driver: vk::DriverId::eMesaDozen\n");
							break;

						case vk::DriverId::eMoltenvk:
							g_vulkanDeviceIsMoltenVK = true;
							LogDebug("Driver: vk::DriverId::eMoltenvk\n");
							break;

						case vk::DriverId::eNvidiaProprietary:
							LogDebug("Driver: vk::DriverId::eNvidiaProprietary\n");
							break;

						default:
							LogDebug("Driver: %d\n", (int)driverProperties.driverID);
					}
				}

				//See if the device has good integer data type support. If so, enable it
				vk::PhysicalDeviceFeatures enabledFeatures;
				vk::PhysicalDevice16BitStorageFeatures features16bit;
				vk::PhysicalDevice8BitStorageFeatures features8bit;
				vk::PhysicalDeviceVulkan12Features featuresVulkan12;
				void* pNext = nullptr;
				if(device.getFeatures().shaderFloat64)
				{
					enabledFeatures.shaderFloat64 = true;
					g_hasShaderFloat64 = true;
					LogDebug("Enabling 64-bit float support\n");
				}
				if(device.getFeatures().shaderInt64)
				{
					enabledFeatures.shaderInt64 = true;
					g_hasShaderInt64 = true;
					LogDebug("Enabling 64-bit integer support\n");
				}
				if(device.getFeatures().shaderInt16)
				{
					enabledFeatures.shaderInt16 = true;
					LogDebug("Enabling 16-bit integer support\n");
				}
				if(hasPhysicalDeviceProperties2)
				{
					//Get more details
					auto features2 = device.getFeatures2<
						vk::PhysicalDeviceFeatures2,
						vk::PhysicalDevice16BitStorageFeatures,
						vk::PhysicalDevice8BitStorageFeatures,
						vk::PhysicalDeviceVulkan12Features
						>();
					auto storageFeatures16 = std::get<1>(features2);
					auto storageFeatures8 = std::get<2>(features2);
					auto vulkan12Features = std::get<3>(features2);

					//Enable 16 bit SSBOs
					if(storageFeatures16.storageBuffer16BitAccess && storageFeatures16.uniformAndStorageBuffer16BitAccess)
					{
						features16bit.storageBuffer16BitAccess = true;
						features16bit.uniformAndStorageBuffer16BitAccess = true;
						features16bit.pNext = pNext;
						pNext = &features16bit;
						LogDebug("Enabling 16-bit integer support for SSBOs\n");
						g_hasShaderInt16 = true;
					}

					//Vulkan 1.2 allows some stuff to be done simpler
					if(vulkan12Available)
					{
						//Enable 8 bit shader variables
						if(vulkan12Features.shaderInt8)
						{
							featuresVulkan12.shaderInt8 = true;
							LogDebug("Enabling 8-bit integer support\n");
						}

						//Enable 8 bit SSBOs
						if(storageFeatures8.uniformAndStorageBuffer8BitAccess)
						{
							featuresVulkan12.uniformAndStorageBuffer8BitAccess = true;
							LogDebug("Enabling 8-bit integer support for SSBOs\n");
							g_hasShaderInt8 = true;
						}

						featuresVulkan12.pNext = pNext;
						pNext = &featuresVulkan12;
					}

					//Nope, need to use the old way
					else
					{
						//Enable 8 bit SSBOs
						if(storageFeatures8.storageBuffer8BitAccess)
						{
							features8bit.storageBuffer8BitAccess = true;
							features8bit.pNext = pNext;
							pNext = &features8bit;
							LogDebug("Enabling 8-bit integer support for SSBOs\n");
						}
					}
				}

				//Request all available queues, and make them all equal priority.
				vector<vk::DeviceQueueCreateInfo> qinfo;
				vector<float> queuePriority;
				for(size_t i=0; i<families.size(); i++)
				{
					auto f = families[i];
					for(size_t j=queuePriority.size(); j<f.queueCount; j++)
						queuePriority.push_back(0.5);
					qinfo.push_back(vk::DeviceQueueCreateInfo(
						{}, i, f.queueCount, &queuePriority[0]));
				}

				//See if the device has KHR_portability_subset (typically the case for MoltenVK)
				//or KHR_shader_non_semantic_info (required for debug printf)
				bool hasPortabilitySubset = false;
				bool hasNonSemanticInfo = false;
				auto devexts = device.enumerateDeviceExtensionProperties();
				for(auto ext : devexts)
				{
					if(!strcmp(&ext.extensionName[0], "VK_KHR_portability_subset"))
					{
						hasPortabilitySubset = true;
						LogDebug("Device has VK_KHR_portability_subset, requesting it\n");
					}
					if(!strcmp(&ext.extensionName[0], "VK_KHR_shader_non_semantic_info"))
					{
						hasNonSemanticInfo = true;
						LogDebug("Device has VK_KHR_shader_non_semantic_info, requesting it\n");
					}
					if(!strcmp(&ext.extensionName[0], "VK_KHR_push_descriptor"))
					{
						g_hasPushDescriptor = true;
						LogDebug("Device has VK_KHR_push_descriptor, requesting it\n");
					}
					if(!strcmp(&ext.extensionName[0], "VK_EXT_shader_atomic_float"))
					{
						g_hasShaderAtomicFloat = true;
						LogDebug("Device has VK_EXT_shader_atomic_float, requesting it\n");
					}

					if(!strcmp(&ext.extensionName[0], "VK_EXT_memory_budget"))
					{
						if(!hasPhysicalDeviceProperties2)
							LogWarning("VK_EXT_memory_budget is supported, but not VK_KHR_get_physical_device_properties2 so it's useless\n");
						else
						{
							LogDebug("Device has VK_EXT_memory_budget, requesting it\n");
							g_hasMemoryBudget = true;
						}
					}
				}

				//Initialize the device
				vector<const char*> devextensions;
				devextensions.push_back("VK_KHR_swapchain");
				if(hasPortabilitySubset)
					devextensions.push_back("VK_KHR_portability_subset");
				if(hasNonSemanticInfo)
					devextensions.push_back("VK_KHR_shader_non_semantic_info");
				if(g_hasShaderAtomicFloat)
					devextensions.push_back("VK_EXT_shader_atomic_float");
				if(g_hasMemoryBudget)
					devextensions.push_back("VK_EXT_memory_budget");
				if(g_hasPushDescriptor)
					devextensions.push_back("VK_KHR_push_descriptor");
				vk::DeviceCreateInfo devinfo(
					{},
					qinfo,
					{},
					devextensions,
					&enabledFeatures,
					pNext);
				g_vkComputeDevice = make_shared<vk::raii::Device>(device, devinfo);

				//Figure out what memory types to use for various purposes
				bool foundPinnedType = false;
				bool foundLocalType = false;
				g_vkPinnedMemoryType = 0;
				g_vkLocalMemoryType = 0;
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
							g_vkPinnedMemoryHeap = mtype.heapIndex;
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
							g_vkLocalMemoryHeap = mtype.heapIndex;
						}
					}
				}

				LogDebug("Using type %u for pinned host memory\n", g_vkPinnedMemoryType);
				LogDebug("Using type %u for card-local memory\n", g_vkLocalMemoryType);

				//Make the queue manager
				g_vkQueueManager = make_unique<QueueManager>(g_vkComputePhysicalDevice, g_vkComputeDevice);

				//Make a Queue for memory transfers that we can use implicitly during buffer management
				g_vkTransferQueue = g_vkQueueManager->GetTransferQueue("g_vkTransferQueue");

				//Make a CommandPool for transfers
				vk::CommandPoolCreateInfo poolInfo(
					vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
					g_vkTransferQueue->m_family );
				g_vkTransferCommandPool = make_unique<vk::raii::CommandPool>(*g_vkComputeDevice, poolInfo);

				//Make a CommandBuffer for memory transfers that we can use implicitly during buffer management
				vk::CommandBufferAllocateInfo bufinfo(**g_vkTransferCommandPool, vk::CommandBufferLevel::ePrimary, 1);
				g_vkTransferCommandBuffer = make_unique<vk::raii::CommandBuffer>(
					std::move(vk::raii::CommandBuffers(*g_vkComputeDevice, bufinfo).front()));
			}

			//Destroy other physical devices that we're not using
			for(size_t i=0; i<devices.size(); i++)
			{
				if(i == bestDevice)
					continue;
				devices[i].clear();
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
	g_gpuScopeDriverEnabled = true;

	//Initialize the glsl compiler since vkFFT does JIT generation of kernels
	if(1 != glslang_initialize_process())
		LogError("Failed to initialize glslang compiler\n");

	//Initialize our pipeline cache manager and load existing cache data
	g_pipelineCacheMgr = make_unique<PipelineCacheManager>();

	//Print out vkFFT version for debugging
	int vkfftver = VkFFTGetVersion();
	int vkfft_major = vkfftver / 10000;
	int vkfft_minor = (vkfftver / 100) % 100;
	int vkfft_patch = vkfftver % 100;
	LogDebug("vkFFT version: %d.%d.%d\n", vkfft_major, vkfft_minor, vkfft_patch);

	if(g_hasDebugUtils)
	{
		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eDevice,
				reinterpret_cast<int64_t>(static_cast<VkDevice>(**g_vkComputeDevice)),
				"g_vkComputeDevice"));

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eCommandBuffer,
				reinterpret_cast<int64_t>(static_cast<VkCommandBuffer>(**g_vkTransferCommandBuffer)),
				"g_vkTransferCommandBuffer"));

		//For some reason this doesn't work?
		/*g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::ePhysicalDevice,
				reinterpret_cast<int64_t>(static_cast<VkPhysicalDevice>(**g_vkComputePhysicalDevice)),
				"g_vkComputePhysicalDevice"));*/
	}

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

/**
	@brief Free all global Vulkan resources in the correct order
 */
void VulkanCleanup()
{
	glfwTerminate();

	g_pipelineCacheMgr = nullptr;

	glslang_finalize_process();

	g_vkTransferQueue = nullptr;
	g_vkTransferCommandBuffer = nullptr;
	g_vkTransferCommandPool = nullptr;

	g_vkQueueManager = nullptr;

	g_vkComputeDevice = nullptr;
	g_vkInstance = nullptr;
}
