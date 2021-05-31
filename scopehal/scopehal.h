/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

#include <vector>
#include <string>
#include <map>
#include <stdint.h>
#include <chrono>
#include <thread>

#include <sigc++/sigc++.h>
#include <cairomm/context.h>

#include <yaml-cpp/yaml.h>

#include "../log/log.h"
#include "../graphwidget/Graph.h"

#include "config.h"
#ifdef HAVE_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#define CL_HPP_MINIMUM_OPENCL_VERSION CL_TARGET_OPENCL_VERSION
#define CL_HPP_TARGET_OPENCL_VERSION CL_TARGET_OPENCL_VERSION
#define CL_HPP_ENABLE_PROGRAM_CONSTRUCTION_FROM_ARRAY_COMPATIBILITY
#define CL_HPP_ENABLE_EXCEPTIONS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wignored-attributes"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <CL/opencl.hpp>
#pragma GCC diagnostic pop
#endif

#include "Unit.h"
#include "Bijection.h"
#include "IDTable.h"

#include "SCPITransport.h"
#include "SCPISocketTransport.h"
#include "SCPILxiTransport.h"
#include "SCPINullTransport.h"
#include "SCPITMCTransport.h"
#include "SCPIUARTTransport.h"
#include "VICPSocketTransport.h"
#include "SCPIDevice.h"

#include "OscilloscopeChannel.h"
#include "FlowGraphNode.h"
#include "Trigger.h"

#include "Instrument.h"
#include "FunctionGenerator.h"
#include "Multimeter.h"
#include "Oscilloscope.h"
#include "SCPIOscilloscope.h"
#include "PowerSupply.h"

#include "Statistic.h"
#include "FilterParameter.h"
#include "Filter.h"
#include "PeakDetectionFilter.h"
#include "SpectrumChannel.h"

#include "SParameters.h"
#include "TouchstoneParser.h"
#include "IBISParser.h"

uint64_t ConvertVectorSignalToScalar(const std::vector<bool>& bits);

std::string GetDefaultChannelColor(int i);

std::string Trim(const std::string& str);
std::string TrimQuotes(const std::string& str);
std::string BaseName(const std::string& path);

std::string ReadFile(const std::string& path);
std::string ReadDataFile(const std::string& relpath);
std::string FindDataFile(const std::string& relpath);

std::string to_string_sci(double d);
std::string to_string_hex(uint64_t n, bool zeropad = false, int len = 0);

void TransportStaticInit();
void DriverStaticInit();

void InitializeSearchPaths();
void InitializePlugins();
void DetectCPUFeatures();
void DetectGPUFeatures();

void ScopehalStaticCleanup();

float FreqToPhase(float hz);

uint64_t next_pow2(uint64_t v);

extern bool g_hasAvx512F;
extern bool g_hasAvx512VL;
extern bool g_hasAvx512DQ;
extern bool g_hasAvx2;

#define FS_PER_SECOND 1e15
#define SECONDS_PER_FS 1e-15

//string to size_t conversion
#ifdef _WIN32
#define stos(str) static_cast<size_t>(stoll(str))
#else
#define stos(str) static_cast<size_t>(stol(str))
#endif

extern std::vector<std::string> g_searchPaths;

//Set true prior to calling DetectGPUFeatures() to force OpenCL to not be used
extern bool g_disableOpenCL;

#ifdef HAVE_OPENCL
extern cl::Context* g_clContext;
extern std::vector<cl::Device> g_contextDevices;
#endif

#endif
