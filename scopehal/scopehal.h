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
	@brief Main library include file
 */

#ifndef scopehal_h
#define scopehal_h

#define __USE_MINGW_ANSI_STDIO 1 // Required for MSYS2 mingw64 to support format "%z" ..

#include <deque>
#include <vector>
#include <string>
#include <map>
#include <stdint.h>
#include <chrono>
#include <thread>
#include <memory>
#include <climits>
#include <set>
#include <float.h>

#include <sigc++/sigc++.h>

#include <yaml-cpp/yaml.h>

#include "../log/log.h"
#include "ScopehalUtil.h"

#include "config.h"

//Vulkan is now a mandatory dependency, so no compile time enable flag
//(disable some warnings in Vulkan headers that we can't do anything about)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <vulkan/vulkan_raii.hpp>
#pragma GCC diagnostic pop

//must be early because a lot of inline methods in headers rely on these
#ifdef __x86_64__
extern bool g_hasFMA;
extern bool g_hasAvx512F;
extern bool g_hasAvx512VL;
extern bool g_hasAvx512DQ;
extern bool g_hasAvx2;
#endif

#include "Unit.h"
#include "Bijection.h"
#include "IDTable.h"

#include "AcceleratorBuffer.h"
#include "ComputePipeline.h"

#include "SCPITransport.h"
#include "SCPISocketTransport.h"
#include "SCPITwinLanTransport.h"
#include "SCPILinuxGPIBTransport.h"
#include "SCPILxiTransport.h"
#include "SCPINullTransport.h"
#include "SCPIUARTTransport.h"
#include "VICPSocketTransport.h"
#include "SCPIDevice.h"

#if !defined(_WIN32) && !defined(__APPLE__)
// TMC is only supported on Linux for now
// https://github.com/glscopeclient/scopehal/issues/519
#include "SCPITMCTransport.h"
#endif

#include "FlowGraphNode.h"
#include "OscilloscopeChannel.h"
#include "StreamDescriptor_inlines.h"
#include "FlowGraphNode_inlines.h"
#include "Trigger.h"

#include "Instrument.h"
#include "FunctionGenerator.h"
#include "Load.h"
#include "Multimeter.h"
#include "MultimeterChannel.h"
#include "Oscilloscope.h"
#include "SParameterChannel.h"
#include "PowerSupply.h"
#include "PowerSupplyChannel.h"
#include "RFSignalGenerator.h"
#include "SCPIInstrument.h"
#include "SCPIFunctionGenerator.h"
#include "SCPILoad.h"
#include "SCPIMultimeter.h"
#include "SCPIOscilloscope.h"
#include "SCPIRFSignalGenerator.h"
#include "SCPIPowerSupply.h"

#include "SParameters.h"
#include "TouchstoneParser.h"
#include "IBISParser.h"

#include "Statistic.h"
#include "FilterParameter.h"
#include "Filter.h"
#include "ImportFilter.h"
#include "PeakDetectionFilter.h"
#include "SpectrumChannel.h"
#include "SParameterSourceFilter.h"
#include "SParameterFilter.h"

#include "FilterGraphExecutor.h"

#include "QueueManager.h"

uint64_t ConvertVectorSignalToScalar(const std::vector<bool>& bits);

std::string GetDefaultChannelColor(int i);

std::string Trim(const std::string& str);
std::string TrimQuotes(const std::string& str);
std::string BaseName(const std::string& path);

std::string ReadFile(const std::string& path);
std::string ReadDataFile(const std::string& relpath);
std::vector<uint32_t> ReadDataFileUint32(const std::string& relpath);
std::string FindDataFile(const std::string& relpath);
void GetTimestampOfFile(std::string path, time_t& timestamp, int64_t& fs);

std::string to_string_sci(double d);
std::string to_string_hex(uint64_t n, bool zeropad = false, int len = 0);

void TransportStaticInit();
void DriverStaticInit();

bool VulkanInit(bool skipGLFW = false);
void InitializeSearchPaths();
void InitializePlugins();
void DetectCPUFeatures();
std::string GetDirOfCurrentExecutable();

void ScopehalStaticCleanup();

float FreqToPhase(float hz);

uint64_t next_pow2(uint64_t v);
uint64_t prev_pow2(uint64_t v);

std::vector<std::string> explode(const std::string& str, char separator);
std::string str_replace(const std::string& search, const std::string& replace, const std::string& subject);

#define FS_PER_SECOND 1e15
#define SECONDS_PER_FS 1e-15

//string to size_t conversion
#ifdef _WIN32
#define stos(str) static_cast<size_t>(stoll(str))
#else
#define stos(str) static_cast<size_t>(stol(str))
#endif

extern std::vector<std::string> g_searchPaths;

//Vulkan global stuff
extern vk::raii::Context g_vkContext;
extern std::unique_ptr<vk::raii::Instance> g_vkInstance;
extern uint8_t g_vkComputeDeviceUuid[16];
extern uint32_t g_vkComputeDeviceDriverVer;
extern vk::raii::PhysicalDevice* g_vkComputePhysicalDevice;
extern std::unique_ptr<QueueManager> g_vkQueueManager;
extern bool g_vulkanDeviceIsIntelMesa;
extern bool g_vulkanDeviceIsAnyMesa;
extern bool g_vulkanDeviceIsMoltenVK;

//Enable flags for various features
extern bool g_gpuFilterEnabled;
extern bool g_gpuScopeDriverEnabled;
extern bool g_hasShaderInt64;
extern bool g_hasShaderInt16;
extern bool g_hasShaderInt8;
extern bool g_hasDebugUtils;

//Shader args for frequently used kernels
struct ConvertRawSamplesShaderArgs
{
	uint32_t size;
	float gain;
	float offset;
};

uint32_t GetComputeBlockCount(size_t numGlobal, size_t blockSize);

//Validation helper for templates
//Throws compile-time error if specialized for false since there's no implementation
template<bool> class CompileTimeAssert;
template<> class CompileTimeAssert<true>{};

#ifdef _WIN32
std::string NarrowPath(wchar_t* wide);
#else
std::string ExpandPath(const std::string& in);
void CreateDirectory(const std::string& path);
#endif

//Checksum helpers
uint32_t CRC32(const uint8_t* bytes, size_t start, size_t end);
uint32_t CRC32(const std::vector<uint8_t>& bytes);

#endif
