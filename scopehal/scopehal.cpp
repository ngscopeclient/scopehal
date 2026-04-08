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
	@brief Implementation of global functions
 */
#include "scopehal.h"
#include "scopehal-version.h"
#include <libgen.h>
#include <filesystem>

#include "AgilentOscilloscope.h"
#include "AlientekPowerSupply.h"
#include "HP662xAPowerSupply.h"
#include "AntikernelLabsOscilloscope.h"
#include "AntikernelLogicAnalyzer.h"
#include "DemoOscilloscope.h"
#include "DemoPowerSupply.h"
#include "DigilentOscilloscope.h"
#include "DSLabsOscilloscope.h"
#include "KeysightDCA.h"
#include "LeCroyOscilloscope.h"
#include "LeCroyFWPOscilloscope.h"
#include "MagnovaOscilloscope.h"
#include "PicoOscilloscope.h"
#include "RigolOscilloscope.h"
#include "RohdeSchwarzOscilloscope.h"
#include "RSRTB2kOscilloscope.h"
#include "RSRTO6Oscilloscope.h"
#include "SCPIPowerSupply.h"
#include "SiglentSCPIOscilloscope.h"
#include "TektronixOscilloscope.h"
#include "TektronixHSIOscilloscope.h"
#include "ThunderScopeOscilloscope.h"
#include "HaasoscopePro.h"
#include "TinySA.h"

#include "AntikernelLabsTriggerCrossbar.h"
#include "MultiLaneBERT.h"

#include "CSVStreamInstrument.h"
#include "SocketCANAnalyzer.h"

#include "OwonXDMMultimeter.h"
#include "RohdeSchwarzHMC8012Multimeter.h"

#include "OwonXDGFunctionGenerator.h"
#include "SiglentFunctionGenerator.h"
#include "RigolFunctionGenerator.h"

#include "GWInstekGPDX303SPowerSupply.h"
#include "RigolDP8xxPowerSupply.h"
#include "RohdeSchwarzHMC804xPowerSupply.h"
#include "SiglentPowerSupply.h"
#include "RidenPowerSupply.h"
#include "SinilinkPowerSupply.h"
#include "KuaiquPowerSupply.h"

#include "SiglentLoad.h"

#include "AseqSpectrometer.h"

#include "UHDBridgeSDR.h"

#include "CopperMountainVNA.h"
#include "NanoVNA.h"
#include "PicoVNA.h"

#include "SiglentVectorSignalGenerator.h"

#include "CDR8B10BTrigger.h"
#include "CDRNRZPatternTrigger.h"
#include "DCAEdgeTrigger.h"
#include "DropoutTrigger.h"
#include "EdgeTrigger.h"
#include "GlitchTrigger.h"
#include "LineTrigger.h"
#include "NthEdgeBurstTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"

#include "RSRTB2kRiseTimeTrigger.h"
#include "RSRTB2kRuntTrigger.h"
#include "RSRTB2kTimeoutTrigger.h"
#include "RSRTB2kVideoTrigger.h"
#include "RSRTB2kWidthTrigger.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <sys/stat.h>
#include <wordexp.h>
#include <dirent.h>
#else
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#endif

using namespace std;

#ifdef __x86_64__
bool g_hasAvx512F = false;
bool g_hasAvx512DQ = false;
bool g_hasAvx512VL = false;
bool g_hasAvx2 = false;
bool g_hasFMA = false;
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static const uint32_t g_crc32Table[] =
{
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,	0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,	0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,	0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,	0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,	0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,	0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,	0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,	0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,	0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,	0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,	0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,	0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,	0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,	0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,	0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,	0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,	0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AcceleratorBuffer object enumeration, should probably be moved to its own file eventually

recursive_mutex AcceleratorBufferBase::m_objectListMutex;
set<AcceleratorBufferBase*> AcceleratorBufferBase::m_objectList;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AcceleratorBuffer performance counters, should probably be moved to its own file eventually

atomic<int64_t> AcceleratorBufferPerformanceCounters::m_hostDeviceCopiesBlocking;
atomic<int64_t> AcceleratorBufferPerformanceCounters::m_hostDeviceCopiesNonBlocking;
atomic<int64_t> AcceleratorBufferPerformanceCounters::m_hostDeviceCopiesSkipped;

atomic<int64_t> AcceleratorBufferPerformanceCounters::m_deviceHostCopiesBlocking;
atomic<int64_t> AcceleratorBufferPerformanceCounters::m_deviceHostCopiesNonBlocking;
atomic<int64_t> AcceleratorBufferPerformanceCounters::m_deviceHostCopiesSkipped;

atomic<int64_t> AcceleratorBufferPerformanceCounters::m_deviceDeviceCopiesBlocking;
atomic<int64_t> AcceleratorBufferPerformanceCounters::m_deviceDeviceCopiesNonBlocking;
atomic<int64_t> AcceleratorBufferPerformanceCounters::m_deviceDeviceCopiesSkipped;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief True if filters can use GPU acceleration

	Will be deprecated soon since Vulkan is now a mandatory core part of the application
 */
bool g_gpuFilterEnabled = false;

vector<string> g_searchPaths;

void VulkanCleanup();

///@brief List of handlers for low memory registered by various subsystems
set<MemoryPressureHandler> g_memoryPressureHandlers;

/**
	@brief Mutex for controlling access to background Vulkan activity

	Arbitrarily many threads can own this mutex at once, but it must be held when calling vkDeviceWaitIdle.
 */
shared_mutex g_vulkanActivityMutex;

/**
	@brief Static initialization for SCPI transports
 */
void TransportStaticInit()
{
	AddTransportClass(SCPISocketTransport);
#if !defined(_WIN32) && !defined(__APPLE__)
// TMC is only supported on Linux for now
// https://github.com/glscopeclient/scopehal/issues/519
	AddTransportClass(SCPITMCTransport);
#endif
	AddTransportClass(SCPITwinLanTransport);
	AddTransportClass(SCPIUARTTransport);
	AddTransportClass(SCPIHIDTransport);
	AddTransportClass(SCPINullTransport);
	AddTransportClass(VICPSocketTransport);

	//SocketCAN is a Linux-specific feature
#ifdef __linux
	AddTransportClass(SCPISocketCANTransport);
#endif

#ifdef HAS_LXI
	AddTransportClass(SCPILxiTransport);
#endif
}

/**
	@brief Static initialization for CPU feature flags
 */
void DetectCPUFeatures()
{
	LogDebug("Detecting CPU features...\n");
	LogIndenter li;

#ifdef __x86_64__
	//Check CPU features
	g_hasAvx512F = __builtin_cpu_supports("avx512f");
	g_hasAvx512VL = __builtin_cpu_supports("avx512vl");
	g_hasAvx512DQ = __builtin_cpu_supports("avx512dq");
	g_hasAvx2 = __builtin_cpu_supports("avx2");
	g_hasFMA = __builtin_cpu_supports("fma");

	if(g_hasAvx2)
		LogDebug("* AVX2\n");
	if(g_hasFMA)
		LogDebug("* FMA\n");
	if(g_hasAvx512F)
		LogDebug("* AVX512F\n");
	if(g_hasAvx512DQ)
		LogDebug("* AVX512DQ\n");
	if(g_hasAvx512VL)
		LogDebug("* AVX512VL\n");
	LogDebug("\n");
#if defined(_WIN32) && defined(__GNUC__) // AVX2 is temporarily disabled on MingW64/GCC until this in resolved: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412
	if (g_hasAvx2 || g_hasAvx512F || g_hasAvx512DQ || g_hasAvx512VL)
	{
		g_hasAvx2 = g_hasAvx512F = g_hasAvx512DQ = g_hasAvx512VL = false;
		LogWarning("AVX2/AVX512 detected but disabled on MinGW64/GCC (see https://github.com/azonenberg/scopehal-apps/issues/295)\n");
	}
#endif /* defined(_WIN32) && defined(__GNUC__) */
#endif /* __x86_64__ */
}

void ScopehalStaticCleanup()
{
	VulkanCleanup();
}

/**
	@brief Static initialization for instrument drivers
 */
void DriverStaticInit()
{
	InitializeSearchPaths();
	DetectCPUFeatures();
	Unit::InitializeLocales();

	AddBERTDriverClass(AntikernelLabsTriggerCrossbar);
	AddBERTDriverClass(MultiLaneBERT);

	AddDriverClass(AgilentOscilloscope);
	AddDriverClass(AntikernelLabsOscilloscope);
	//AddDriverClass(AntikernelLogicAnalyzer);
	AddDriverClass(DemoOscilloscope);
	AddDriverClass(DigilentOscilloscope);
	AddDriverClass(DSLabsOscilloscope);
	AddDriverClass(HaasoscopePro);
	AddDriverClass(KeysightDCA);
	AddDriverClass(PicoOscilloscope);
	AddDriverClass(RigolOscilloscope);
	AddDriverClass(RohdeSchwarzOscilloscope);
	AddDriverClass(RSRTB2kOscilloscope);
	AddDriverClass(RSRTO6Oscilloscope);
	AddDriverClass(LeCroyOscilloscope);
	AddDriverClass(LeCroyFWPOscilloscope);
	AddDriverClass(MagnovaOscilloscope);
	AddDriverClass(SiglentSCPIOscilloscope);
	AddDriverClass(TektronixOscilloscope);
	AddDriverClass(TektronixHSIOscilloscope);
	AddDriverClass(ThunderScopeOscilloscope);
	AddDriverClass(TinySA);
#ifdef __linux
	AddDriverClass(SocketCANAnalyzer);
#endif

	AddFunctionGeneratorDriverClass(OwonXDGFunctionGenerator);
	AddFunctionGeneratorDriverClass(RigolFunctionGenerator);
	AddFunctionGeneratorDriverClass(SiglentFunctionGenerator);

	AddLoadDriverClass(SiglentLoad);

	AddMiscInstrumentDriverClass(CSVStreamInstrument);

	AddMultimeterDriverClass(OwonXDMMultimeter);
	AddMultimeterDriverClass(RohdeSchwarzHMC8012Multimeter);

	AddPowerSupplyDriverClass(DemoPowerSupply);
	AddPowerSupplyDriverClass(GWInstekGPDX303SPowerSupply);
	AddPowerSupplyDriverClass(RigolDP8xxPowerSupply);
	AddPowerSupplyDriverClass(RohdeSchwarzHMC804xPowerSupply);
	AddPowerSupplyDriverClass(SiglentPowerSupply);
	AddPowerSupplyDriverClass(HP662xAPowerSupply);
	AddPowerSupplyDriverClass(AlientekPowerSupply);
	AddPowerSupplyDriverClass(RidenPowerSupply);
	AddPowerSupplyDriverClass(SinilinkPowerSupply);
	AddPowerSupplyDriverClass(KuaiquPowerSupply);

	AddRFSignalGeneratorDriverClass(SiglentVectorSignalGenerator);

	AddSpectrometerDriverClass(AseqSpectrometer);

	AddSDRDriverClass(UHDBridgeSDR);

	AddVNADriverClass(CopperMountainVNA);
	AddVNADriverClass(NanoVNA);
	AddVNADriverClass(PicoVNA);

	AddTriggerClass(CDR8B10BTrigger);
	AddTriggerClass(CDRNRZPatternTrigger);
	AddTriggerClass(DCAEdgeTrigger);
	AddTriggerClass(DropoutTrigger);
	AddTriggerClass(EdgeTrigger);
	AddTriggerClass(GlitchTrigger);
	AddTriggerClass(LineTrigger);
	AddTriggerClass(NthEdgeBurstTrigger);
	AddTriggerClass(PulseWidthTrigger);
	AddTriggerClass(RuntTrigger);
	AddTriggerClass(SlewRateTrigger);
	AddTriggerClass(UartTrigger);
	AddTriggerClass(WindowTrigger);
/*
	AddTriggerClass(RSRTB2kRiseTimeTrigger);
	AddTriggerClass(RSRTB2kRuntTrigger);
	AddTriggerClass(RSRTB2kTimeoutTrigger);
	AddTriggerClass(RSRTB2kVideoTrigger);
	AddTriggerClass(RSRTB2kWidthTrigger);*/
}

string GetDefaultChannelColor(int i)
{
	const int NUM_COLORS = 12;
	static const char* colorTable[NUM_COLORS] =
	{
		// cppcheck-suppress constStatement
		"#a6cee3",
		"#1f78b4",
		"#b2df8a",
		"#33a02c",
		"#fb9a99",
		"#e31a1c",
		"#fdbf6f",
		"#ff7f00",
		"#cab2d6",
		"#6a3d9a",
		"#ffff99",
		"#b15928"
	};

	return colorTable[i % NUM_COLORS];
}

/**
	@brief Converts a vector bus signal into a scalar (up to 64 bits wide)
 */
uint64_t ConvertVectorSignalToScalar(const vector<bool>& bits)
{
	uint64_t rval = 0;
	for(auto b : bits)
		rval = (rval << 1) | b;
	return rval;
}

/**
	@brief Initialize all plugins
 */
void InitializePlugins()
{
#ifndef _WIN32
	char tmp[1024];
	vector<string> search_dirs;
	search_dirs.push_back("/usr/lib/scopehal/plugins/");
	search_dirs.push_back("/usr/local/lib/scopehal/plugins/");

	//current binary dir
	string binDir = GetDirOfCurrentExecutable();
	if ( !binDir.empty() )
	{
		//If the binary directory is under /usr, do *not* search it!
		//We're probably in /usr/bin and we really do not want to be dlopen-ing every single thing in there.
		//See https://github.com/azonenberg/scopehal-apps/issues/393
		if(binDir.find("/usr") != 0)
			search_dirs.push_back(binDir);
	}

	//Home directory
	snprintf(tmp, sizeof(tmp), "%s/.scopehal/plugins", getenv("HOME"));
	search_dirs.push_back(tmp);

	for(auto dir : search_dirs)
	{
		DIR* hdir = opendir(dir.c_str());
		LogDebug("Searching for plugins in %s\n", dir.c_str());
		LogIndenter li;
		if(!hdir)
			continue;

		dirent* pent;
		while((pent = readdir(hdir)))
		{
			//Don't load hidden files or parent directory entries
			if(pent->d_name[0] == '.')
				continue;

			// Don't load directories
			if(pent->d_type == DT_DIR)
				continue;

			//Try loading it and see if it works.
			//(for now, never unload the plugins)
			string fname = dir + "/" + pent->d_name;
			void* hlib = dlopen(fname.c_str(), RTLD_NOW);
			if(hlib == nullptr)
				continue;
			LogDebug("Checking %s\n", fname.c_str());
			LogIndenter li2;

			//If loaded, look for PluginInit()
			typedef void (*PluginInit)();
			PluginInit init = (PluginInit)dlsym(hlib, "PluginInit");
			if(!init)
			{
				LogDebug("PluginInit not found, skipping\n");
				continue;
			}

			//If found, it's a valid plugin
			LogDebug("Loading plugin %s\n", fname.c_str());
			init();
		}

		closedir(hdir);
	}
#else
	// Get path of process image
	TCHAR binPath[MAX_PATH];

	if( GetModuleFileName(NULL, binPath, MAX_PATH) == 0 )
	{
		LogError("Error: GetModuleFileName() failed.\n");
		return;
	}

	// Remove file name from path
	if( !PathRemoveFileSpec(binPath) )
	{
		LogError("Error: PathRemoveFileSpec() failed.\n");
		return;
	}

	TCHAR searchPath[MAX_PATH];
	if( PathCombine(searchPath, binPath, "plugins\\*.dll") == NULL )
	{
		LogError("Error: PathCombine() failed.\n");
		return;
	}

	// For now, we only search in the folder that contains the binary.
	WIN32_FIND_DATA findData;
	HANDLE findHandle = INVALID_HANDLE_VALUE;

	// First file entry
	findHandle = FindFirstFile(searchPath, &findData);

	// Is there at least one file?
	if(findHandle == INVALID_HANDLE_VALUE)
	{
		return;
	}

	do
	{
		// Exclude directories
		if(!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			auto fileName = findData.cFileName;
			auto fileNameCStr = reinterpret_cast<const char*>(fileName);

			// The file name does not contain the full path, which poses a problem since the file is
			// located in the plugins subdirectory
			TCHAR filePath[MAX_PATH];

			if( PathCombine(filePath, "plugins", fileName) == NULL )
			{
				LogError("Error: PathCombine() failed.\n");
				return;
			}

			// Try to open it as a library
			auto module = LoadLibrary(filePath);

			if(module != NULL)
			{
				// Try to retrieve plugin entry point address
				auto procAddr = GetProcAddress(module, "PluginInit");

				if(procAddr != NULL)
				{
					typedef void (*PluginInit)();
					auto proc = reinterpret_cast<PluginInit>(procAddr);
					proc();
				}
				else
				{
					LogWarning("Warning: Found plugin %s, but has no init symbol\n", fileNameCStr);
					FreeLibrary(module);
				}
			}
			else
			{
				LogWarning("Warning: Found plugin %s, but isn't valid library\n", fileNameCStr);
			}
		}
	}
	while(0 != FindNextFile(findHandle, &findData));

	auto error = GetLastError();

	if(error != ERROR_NO_MORE_FILES)
	{
		LogError("Error: Enumeration of plugin files failed.\n");
	}

	FindClose(findHandle);

#endif
}

/**
	@brief Removes whitespace from the start and end of a string
 */
string Trim(const string& str)
{
	string ret;
	string tmp;

	//Skip leading spaces
	size_t i=0;
	for(; i<str.length() && isspace(str[i]); i++)
	{}

	//Read non-space stuff
	for(; i<str.length(); i++)
	{
		//Non-space
		char c = str[i];
		if(!isspace(c))
		{
			ret = ret + tmp + c;
			tmp = "";
		}

		//Space. Save it, only append if we have non-space after
		else
			tmp += c;
	}

	return ret;
}

/**
	@brief Removes quotes from the start and end of a string
 */
string TrimQuotes(const string& str)
{
	string ret;
	string tmp;

	//Skip leading spaces
	size_t i=0;
	for(; i<str.length() && (str[i] == '\"'); i++)
	{}

	//Read non-space stuff
	for(; i<str.length(); i++)
	{
		//Non-quote
		char c = str[i];
		if(c != '\"')
		{
			ret = ret + tmp + c;
			tmp = "";
		}

		//Quote. Save it, only append if we have non-quote after
		else
			tmp += c;
	}

	return ret;
}

string BaseName(const string & path)
{
	return path.substr(path.find_last_of("/\\") + 1);
}

/**
	@brief Converts a frequency in Hz to a phase velocity in rad/sec
 */
float FreqToPhase(float hz)
{
	return 2 * M_PI * hz;
}

/**
	@brief Like std::to_string, but output in scientific notation
 */
string to_string_sci(double d)
{
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%e", d);
	return tmp;
}

/**
	@brief Like std::to_string, but output in hex
 */
string to_string_hex(uint64_t n, bool zeropad, int len)
{
	char format[32];
	if(zeropad)
		snprintf(format, sizeof(format), "%%0%dlx", len);
	else if(len > 0)
		snprintf(format, sizeof(format), "%%%dlx", len);
	else
		snprintf(format, sizeof(format), "%%lx");

	char tmp[32];
	snprintf(tmp, sizeof(tmp), format, n);
	return tmp;
}

/**
	@brief Rounds a 64-bit integer up to the next power of 2
 */
uint64_t next_pow2(uint64_t v)
{
//TODO add __lzcnt64 intrinsic for MSVC, handle other platforms if needed
#ifdef __GNUC__
	if(v == 1)
		return 1;
	else
		return 1 << (64 - __builtin_clzll(v-1));
#else
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v |= v >> 32;
	v++;
	return v;
#endif
}

/**
	@brief Rounds a 64-bit integer down to the next power of 2
 */
uint64_t prev_pow2(uint64_t v)
{
	uint64_t next = next_pow2(v);

	if(next == v)
		return v;
	else
		return next/2;
}

/**
	@brief Returns the contents of a file
 */
string ReadFile(const string& path)
{
	//Read the file
	FILE* fp = fopen(path.c_str(), "rb");
	if(!fp)
	{
		LogWarning("ReadFile: Could not open file \"%s\"\n", path.c_str());
		return "";
	}
	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char* buf = new char[fsize + 1];
	if(fsize != fread(buf, 1, fsize, fp))
	{
		LogWarning("ReadFile: Could not read file \"%s\"\n", path.c_str());
		delete[] buf;
		fclose(fp);
		return "";
	}
	buf[fsize] = 0;
	fclose(fp);

	string ret(buf, fsize);
	delete[] buf;

	return ret;
}

/**
	@brief Gets the path to the directory containing the current executable
 */
string GetDirOfCurrentExecutable()
{
#ifdef _WIN32
	TCHAR binPath[MAX_PATH];
	if(GetModuleFileName(NULL, binPath, MAX_PATH) == 0)
		LogError("Error: GetModuleFileName() failed.\n");
	else if(!PathRemoveFileSpec(binPath) )
		LogError("Error: PathRemoveFileSpec() failed.\n");
	else
		return binPath;
#elif defined(__APPLE__)
	char binDir[1024] = {0};
	uint32_t size = sizeof(binDir) - 1;
	if (_NSGetExecutablePath(binDir, &size) != 0) {
		// Buffer size is too small.
		LogError("Error: _NSGetExecutablePath() returned a path larger than our buffer.\n");
		return "";
	}
	return dirname(binDir);
#else
	char binDir[1024] = {0};
	ssize_t readlinkReturn = readlink("/proc/self/exe", binDir, (sizeof(binDir) - 1) );
	if ( readlinkReturn <= 0 )
		LogError("Error: readlink() failed.\n");
	else if ( (unsigned) readlinkReturn > (sizeof(binDir) - 1) )
		LogError("Error: readlink() returned a path larger than our buffer.\n");
	else
		return dirname(binDir);
#endif

	return "";
}

void InitializeSearchPaths()
{
	std::filesystem::path binRootDir;
	//Search in the directory of the glscopeclient binary first
#ifdef _WIN32
	TCHAR binPath[MAX_PATH];
	if(GetModuleFileName(NULL, binPath, MAX_PATH) == 0)
		LogError("Error: GetModuleFileName() failed.\n");
	else if(!PathRemoveFileSpec(binPath) )
		LogError("Error: PathRemoveFileSpec() failed.\n");
	else
	{
		g_searchPaths.push_back(binPath);
		binRootDir = binPath;
	}
#else
	binRootDir = GetDirOfCurrentExecutable();
	if(!binRootDir.empty())
	{
		g_searchPaths.push_back(binRootDir);
	}
#endif

	// Add the share directories associated with the binary location
	if(!binRootDir.empty())
	{
		std::filesystem::path rootDir = binRootDir.parent_path();
		g_searchPaths.push_back((rootDir / "share/ngscopeclient").string());
		g_searchPaths.push_back((rootDir / "share/scopehal").string());
	}

	//Local directories preferred over system ones
#ifndef _WIN32
	string home = getenv("HOME");
	g_searchPaths.push_back(home + "/.scopehal");
	g_searchPaths.push_back("/usr/local/share/ngscopeclient");
	g_searchPaths.push_back("/usr/local/share/scopehal");
	g_searchPaths.push_back("/usr/share/ngscopeclient");
	g_searchPaths.push_back("/usr/share/scopehal");

	//for macports
	g_searchPaths.push_back("/opt/local/share/ngscopeclient");
	g_searchPaths.push_back("/opt/local/share/scopehal");
#endif

	//TODO: add system directories for Windows (%appdata% etc)?
	//The current strategy of searching the binary directory should work fine in the common case
	//of installing binaries and data files all in one directory under Program Files.
}

/**
	@brief Locates and returns the contents of a data file as a std::string
 */
string ReadDataFile(const string& relpath)
{
	auto abspath = FindDataFile(relpath);
	FILE* fp = fopen(abspath.c_str(), "rb");

	if(!fp)
	{
		LogWarning("ReadDataFile: Could not open file \"%s\"\n", relpath.c_str());
		return "";
	}
	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char* buf = new char[fsize + 1];
	if(fsize != fread(buf, 1, fsize, fp))
	{
		LogWarning("ReadDataFile: Could not read file \"%s\"\n", relpath.c_str());
		delete[] buf;
		fclose(fp);
		return "";
	}
	buf[fsize] = 0;
	fclose(fp);

	string ret(buf, fsize);
	delete[] buf;

	return ret;
}

/**
	@brief Locates and returns the contents of a data file as a std::vector<uint32_t>
 */
vector<uint32_t> ReadDataFileUint32(const string& relpath)
{
	vector<uint32_t> buf;

	auto abspath = FindDataFile(relpath);
	FILE* fp = fopen(abspath.c_str(), "rb");

	if(!fp)
	{
		LogWarning("ReadDataFile: Could not open file \"%s\"\n", relpath.c_str());
		return buf;
	}
	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	size_t wordsize = fsize / sizeof(uint32_t);
	buf.resize(wordsize);
	if(wordsize != fread(&buf[0], sizeof(uint32_t), wordsize, fp))
	{
		LogWarning("ReadDataFile: Could not read file \"%s\"\n", relpath.c_str());
		fclose(fp);
		return buf;
	}
	fclose(fp);

	return buf;
}

/**
	@brief Locates a data file
 */
string FindDataFile(const string& relpath)
{
	//Check relative path first
	FILE* fp = fopen(relpath.c_str(), "rb");
	if(fp)
	{
		fclose(fp);
		return relpath;
	}

	for(auto dir : g_searchPaths)
	{
		string path = dir + "/" + relpath;
		fp = fopen(path.c_str(), "rb");
		if(fp)
		{
			fclose(fp);
			return path;
		}
	}

	return "";
}

void GetTimestampOfFile(string path, time_t& timestamp, int64_t& fs)
{
	//TODO: Add Windows equivalent
	#ifndef _WIN32
		struct stat st;
		if(0 == stat(path.c_str(), &st))
		{
			timestamp = st.st_mtim.tv_sec;
			fs = st.st_mtim.tv_nsec * 1000L * 1000L;
		}
	#endif
}

/**
	@brief Splits a string up into an array separated by delimiters
 */
vector<string> explode(const string& str, char separator)
{
	vector<string> ret;
	string tmp;
	for(auto c : str)
	{
		if(c == separator)
		{
			if(!tmp.empty())
				ret.push_back(tmp);
			tmp = "";
		}
		else
			tmp += c;
	}
	if(!tmp.empty())
		ret.push_back(tmp);
	return ret;
}

/**
	@brief Converts a string to lower case
 */
string strtolower(const string& s)
{
	string ret;
	for(auto c : s)
		ret += tolower(c);
	return ret;
}

/**
	@brief Replaces all occurrences of the search string with "replace" in the given string
 */
string str_replace(const string& search, const string& replace, const string& subject)
{
	string ret;

	//This can probably be made more efficient, but for now we only call it on very short strings
	for(size_t i=0; i<subject.length(); i++)
	{
		//Match?
		if(0 == strncmp(&subject[i], &search[0], search.length()))
		{
			ret += replace;
			i += search.length() - 1;
		}

		//No, just copy
		else
			ret += subject[i];
	}

	return ret;
}

uint32_t GetComputeBlockCount(size_t numGlobal, size_t blockSize)
{
	uint32_t ret = numGlobal / blockSize;
	if(numGlobal % blockSize)
		ret ++;
	return ret;
}

#ifdef _WIN32

string NarrowPath(wchar_t* wide)
{
	char narrow[MAX_PATH];
	const auto len = wcstombs(narrow, wide, MAX_PATH);

	if(len == static_cast<size_t>(-1))
		throw runtime_error("Failed to convert wide string");

	return std::string(narrow);
}
#endif

#ifndef _WIN32
// POSIX-specific filesystem helpers. These will be moved to xptools in a generalized form later.

// Expand things like ~ in path
string ExpandPath(const string& in)
{
	wordexp_t result;
	wordexp(in.c_str(), &result, 0);
	auto expanded = result.we_wordv[0];
	string out{ expanded };
	wordfree(&result);
	return out;
}

void CreateDirectory(const string& path)
{
	const auto expanded = ExpandPath(path);

	struct stat fst{ };

	// Check if it exists
	if(stat(expanded.c_str(), &fst) != 0)
	{
		// If not, create it
		if(mkdir(expanded.c_str(), 0755) != 0 && errno != EEXIST)
		{
			perror("");
			throw runtime_error("failed to create preferences directory");
		}
	}
	else if(!S_ISDIR(fst.st_mode))
	{
		// Exists, but is not a directory
		throw runtime_error("preferences directory exists but is not a directory");
	}
}
#endif

/**
	@brief Calculates a CRC32 checksum using the standard Ethernet polynomial
 */
uint32_t CRC32(const uint8_t* bytes, size_t start, size_t end)
{
	uint32_t crc = 0xffffffff;
	for(size_t n=start; n <= end; n++)
		crc = g_crc32Table[ (crc & 0xff) ^ bytes[n] ] ^ (crc >> 8);
	return __builtin_bswap32(~crc);
}

uint32_t CRC32(const vector<uint8_t>& bytes)
{
	return CRC32(&bytes[0], 0, bytes.size()-1);
}

/**
	@brief Returns the library version string (Semantic Version formatted)
 */
const char* ScopehalGetVersion()
{
	return SCOPEHAL_VERSION;
}

/**
	@brief Called when we run low on memory

	@param level			Indicates if this is a soft or hard memory exhaustion condition
	@param type				Indicates if we are low on CPU or GPU memory
	@param requestedSize	For hard memory exhaustion, the size of the failing allocation.
							For soft exhaustion, ignored and set to zero

	@return True if memory was freed, false if no space could be freed
 */
bool OnMemoryPressure(MemoryPressureLevel level, MemoryPressureType type, size_t requestedSize)
{
	//Only allow one OnMemoryPressure() to execute at a time
	//Anyone else who simultaneously OOM'd has to wait
	static mutex memoryPressureMutex;
	lock_guard<mutex> lock(memoryPressureMutex);

	LogWarning("OnMemoryPressure: %s memory exhaustion on %s (tried to allocate %s)\n",
		(level == MemoryPressureLevel::Hard) ? "Hard" : "Soft",
		(type == MemoryPressureType::Host) ? "host" : "device",
		Unit(Unit::UNIT_BYTES).PrettyPrint(requestedSize, 4).c_str());

	bool moreFreed = false;

	for(auto handler : g_memoryPressureHandlers)
	{
		if(handler(level, type, requestedSize))
			moreFreed = true;
	}

	return moreFreed;
}
