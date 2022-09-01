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
	@brief Implementation of global functions
 */
#include "scopehal.h"
#include <gtkmm/drawingarea.h>
#include <libgen.h>

#include "AgilentOscilloscope.h"
#include "AntikernelLabsOscilloscope.h"
#include "AntikernelLogicAnalyzer.h"
#include "DemoOscilloscope.h"
#include "DigilentOscilloscope.h"
#include "DSLabsOscilloscope.h"
#include "KeysightDCA.h"
#include "LeCroyOscilloscope.h"
#include "PicoOscilloscope.h"
#include "RigolOscilloscope.h"
#include "RohdeSchwarzOscilloscope.h"
#include "SiglentSCPIOscilloscope.h"
#include "TektronixOscilloscope.h"

#include "RohdeSchwarzHMC8012Multimeter.h"

#include "RohdeSchwarzHMC804xPowerSupply.h"

#include "SiglentVectorSignalGenerator.h"

#include "CDR8B10BTrigger.h"
#include "CDRNRZPatternTrigger.h"
#include "DCAEdgeTrigger.h"
#include "DropoutTrigger.h"
#include "EdgeTrigger.h"
#include "GlitchTrigger.h"
#include "NthEdgeBurstTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#include <shlwapi.h>
#endif

#ifdef HAVE_CLFFT
#include <clFFT.h>
#endif

using namespace std;

bool g_hasAvx512F = false;
bool g_hasAvx512DQ = false;
bool g_hasAvx512VL = false;
bool g_hasAvx2 = false;
bool g_hasFMA = false;
bool g_disableOpenCL = false;

///@brief True if filters can use GPU acceleration
bool g_gpuFilterEnabled = false;

///@brief True if scope drivers can use GPU acceleration
bool g_gpuScopeDriverEnabled = false;

vector<string> g_searchPaths;

#ifdef HAVE_OPENCL
cl::Context* g_clContext = NULL;
vector<cl::Device> g_contextDevices;
size_t g_maxClLocalSizeX = 0;
#endif

AlignedAllocator<float, 32> g_floatVectorAllocator;

/**
	@brief Static initialization for SCPI transports
 */
void TransportStaticInit()
{
	AddTransportClass(SCPISocketTransport);
	AddTransportClass(SCPITMCTransport);
	AddTransportClass(SCPITwinLanTransport);
	AddTransportClass(SCPIUARTTransport);
	AddTransportClass(SCPINullTransport);
	AddTransportClass(VICPSocketTransport);

#ifdef HAS_LXI
	AddTransportClass(SCPILxiTransport);
#endif
#ifdef HAS_LINUXGPIB
	AddTransportClass(SCPILinuxGPIBTransport);
#endif
}

/**
	@brief Static initialization for CPU feature flags
 */
void DetectCPUFeatures()
{
	LogDebug("Detecting CPU features...\n");
	LogIndenter li;

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
#endif
}

/**
	@brief Static initialization for OpenCL
 */
void DetectGPUFeatures()
{
	#ifdef HAVE_OPENCL
		try
		{
			LogDebug("Detecting OpenCL devices...\n");

			LogIndenter li;

			if(g_disableOpenCL)
			{
				LogNotice("g_disableOpenCL set, disabling OpenCL\n");
				return;
			}

			//Find platforms and print info
			vector<cl::Platform> platforms;
			cl::Platform::get(&platforms);
			if(platforms.empty())
			{
				LogNotice("No platforms found, disabling OpenCL\n");
				return;
			}
			else
			{
				for(size_t i=0; i<platforms.size(); i++)
				{
					LogDebug("Platform %zu\n", i);
					LogIndenter li2;

					string name;
					string profile;
					string vendor;
					string version;
					platforms[i].getInfo(CL_PLATFORM_NAME, &name);
					platforms[i].getInfo(CL_PLATFORM_PROFILE, &profile);
					platforms[i].getInfo(CL_PLATFORM_VENDOR, &vendor);
					platforms[i].getInfo(CL_PLATFORM_VERSION, &version);
					LogDebug("CL_PLATFORM_NAME    = %s\n", name.c_str());
					LogDebug("CL_PLATFORM_PROFILE = %s\n", profile.c_str());
					LogDebug("CL_PLATFORM_VENDOR  = %s\n", vendor.c_str());
					LogDebug("CL_PLATFORM_VERSION = %s\n", version.c_str());

					vector<cl::Device> devices;
					platforms[i].getDevices(CL_DEVICE_TYPE_GPU, &devices);
					if(devices.empty())
						LogDebug("No GPUs found\n");
					for(size_t j=0; j<devices.size(); j++)
					{
						LogDebug("Device %zu\n", j);
						LogIndenter li3;

						string dname;
						string dcvers;
						string dprof;
						string dvendor;
						string dversion;
						string ddversion;
						string extensions;
						unsigned long globalCacheSize;
						unsigned long globalCacheLineSize;
						unsigned long globalMemSize;
						unsigned long localMemSize;
						unsigned int maxClock;
						unsigned int maxComputeUnits;
						unsigned int maxConstantArgs;
						unsigned long maxConstantBuffer;
						unsigned long maxMemAllocSize;
						size_t maxParameterSize;
						size_t maxWorkGroupSize;
						size_t maxWorkItemSizes[3];
						devices[j].getInfo(CL_DEVICE_NAME, &dname);
						devices[j].getInfo(CL_DEVICE_OPENCL_C_VERSION, &dcvers);
						devices[j].getInfo(CL_DEVICE_PROFILE, &dprof);
						devices[j].getInfo(CL_DEVICE_VENDOR, &dvendor);
						devices[j].getInfo(CL_DEVICE_VERSION, &dversion);
						devices[j].getInfo(CL_DRIVER_VERSION, &ddversion);
						devices[j].getInfo(CL_DEVICE_EXTENSIONS, &extensions);
						devices[j].getInfo(CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, &globalCacheSize);
						devices[j].getInfo(CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE, &globalCacheLineSize);
						devices[j].getInfo(CL_DEVICE_GLOBAL_MEM_SIZE, &globalMemSize);
						devices[j].getInfo(CL_DEVICE_LOCAL_MEM_SIZE, &localMemSize);
						devices[j].getInfo(CL_DEVICE_MAX_CLOCK_FREQUENCY, &maxClock);
						devices[j].getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &maxComputeUnits);
						devices[j].getInfo(CL_DEVICE_MAX_CONSTANT_ARGS, &maxConstantArgs);
						devices[j].getInfo(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, &maxConstantBuffer);
						devices[j].getInfo(CL_DEVICE_MAX_MEM_ALLOC_SIZE, &maxMemAllocSize);
						devices[j].getInfo(CL_DEVICE_MAX_PARAMETER_SIZE, &maxParameterSize);
						devices[j].getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &maxWorkGroupSize);
						devices[j].getInfo(CL_DEVICE_MAX_WORK_ITEM_SIZES, &maxWorkItemSizes);

						float k = 1024;
						float m = k*k;
						float g = m*k;

						LogDebug("CL_DRIVER_VERSION                   = %s\n", ddversion.c_str());
						LogDebug("CL_DEVICE_NAME                      = %s\n", dname.c_str());
						LogDebug("CL_DEVICE_OPENCL_C_VERSION          = %s\n", dcvers.c_str());
						LogDebug("CL_DEVICE_PROFILE                   = %s\n", dprof.c_str());
						LogDebug("CL_DEVICE_VENDOR                    = %s\n", dvendor.c_str());
						LogDebug("CL_DEVICE_VERSION                   = %s\n", dversion.c_str());
						LogDebug("CL_DEVICE_GLOBAL_MEM_CACHE_SIZE     = %.3f MB\n", globalCacheSize / m);
						LogDebug("CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE = %lu\n", globalCacheLineSize);
						LogDebug("CL_DEVICE_GLOBAL_MEM_SIZE           = %.2f GB\n", globalMemSize / g);
						LogDebug("CL_DEVICE_LOCAL_MEM_SIZE            = %.2f kB\n", localMemSize / k);
						LogDebug("CL_DEVICE_MAX_CLOCK_FREQUENCY       = %u MHz\n", maxClock);
						LogDebug("CL_DEVICE_MAX_COMPUTE_UNITS         = %u\n", maxComputeUnits);
						LogDebug("CL_DEVICE_MAX_CONSTANT_ARGS         = %u\n", maxConstantArgs);
						LogDebug("CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE  = %.2f kB\n", maxConstantBuffer / k);
						LogDebug("CL_DEVICE_MAX_MEM_ALLOC_SIZE        = %.2f GB\n", maxMemAllocSize / g);
						LogDebug("CL_DEVICE_MAX_PARAMETER_SIZE        = %zu\n", maxParameterSize);
						LogDebug("CL_DEVICE_MAX_WORK_GROUP_SIZE       = %zu\n", maxWorkGroupSize);
						LogDebug("CL_DEVICE_MAX_WORK_ITEM_SIZES       = %zu, %zu, %zu\n",
							maxWorkItemSizes[0], maxWorkItemSizes[1], maxWorkItemSizes[2]);

						vector<string> extensionlist;
						string tmp;
						for(size_t f=0; f<extensions.size(); f++)
						{
							if(isspace(extensions[f]))
							{
								if(tmp != "")
									extensionlist.push_back(tmp);
								tmp = "";
							}
							else
								tmp += extensions[f];
						}

						{
							LogDebug("CL_DEVICE_EXTENSIONS:\n");
							LogIndenter li4;
							for(auto e : extensionlist)
								LogDebug("%s\n", e.c_str());
						}

						//For now, create a context on the first device of the first detected platform
						//and hope for the best.
						//TODO: multi-device support?
						if(!g_clContext && !devices.empty())
						{
							vector<cl::Device> devs;
							devs.push_back(devices[0]);

							//Passing CL_CONTEXT_PLATFORM as parameters seems to make context creation fail. Weird.
							g_clContext = new cl::Context(devs, NULL, NULL, NULL);
							g_contextDevices = g_clContext->getInfo<CL_CONTEXT_DEVICES>();

							//Save some settings about the OpenCL implementation so that we can tune appropriately
							g_maxClLocalSizeX = maxWorkItemSizes[0];
						}
					}
				}
			}
		}
		catch(const cl::Error& e)
		{
			//CL_PLATFORM_NOT_FOUND_KHR is an expected error if there's no GPU on the system
			if( (string(e.what()) == "clGetPlatformIDs") && (e.err() == -1001) )
				LogNotice("No platforms found, disabling OpenCL\n");
			else
				LogError("OpenCL error: %s (%d)\n", e.what(), e.err() );
			delete g_clContext;
			g_clContext = NULL;
			return;
		}

		#ifdef HAVE_CLFFT

			if(g_clContext)
			{
				clfftSetupData data;
				clfftInitSetupData(&data);
				if(CLFFT_SUCCESS != clfftSetup(&data))
				{
					LogError("clFFT init failed, aborting\n");
					abort();
				}

				cl_uint major;
				cl_uint minor;
				cl_uint patch;
				if(CLFFT_SUCCESS != clfftGetVersion(&major, &minor, &patch))
				{
					LogError("clFFT version query failed, aborting\n");
					abort();
				}
				LogDebug("clFFT version: %d.%d.%d\n", major, minor, patch);
			}
			else
				LogDebug("clFFT: present at compile time, but cannot use because no OpenCL devices found\n");

		#else
			LogNotice("clFFT support: not present at compile time\n");
		#endif

	#else
		LogNotice("OpenCL support: not present at compile time. GPU acceleration disabled.\n");
	#endif

	LogDebug("\n");
}

void ScopehalStaticCleanup()
{
	g_vkTransferQueue = nullptr;
	g_vkTransferCommandBuffer = nullptr;
	g_vkTransferCommandPool = nullptr;
	g_vkComputeDevice = nullptr;
	g_vkInstance = nullptr;

	#ifdef HAVE_OPENCL
	#ifdef HAVE_CLFFT
	clfftTeardown();
	#endif
	#endif
}

/**
	@brief Static initialization for instrument drivers
 */
void DriverStaticInit()
{
	InitializeSearchPaths();
	DetectCPUFeatures();
	DetectGPUFeatures();

	AddDriverClass(AgilentOscilloscope);
	AddDriverClass(AntikernelLabsOscilloscope);
	//AddDriverClass(AntikernelLogicAnalyzer);
	AddDriverClass(DemoOscilloscope);
	AddDriverClass(DigilentOscilloscope);
	AddDriverClass(DSLabsOscilloscope);
	AddDriverClass(KeysightDCA);
	AddDriverClass(PicoOscilloscope);
	AddDriverClass(RigolOscilloscope);
	AddDriverClass(RohdeSchwarzOscilloscope);
	AddDriverClass(LeCroyOscilloscope);
	AddDriverClass(SiglentSCPIOscilloscope);
	AddDriverClass(TektronixOscilloscope);

	AddMultimeterDriverClass(RohdeSchwarzHMC8012Multimeter);

	AddPowerSupplyDriverClass(RohdeSchwarzHMC804xPowerSupply);

	AddRFSignalGeneratorDriverClass(SiglentVectorSignalGenerator);

	AddTriggerClass(CDR8B10BTrigger);
	AddTriggerClass(CDRNRZPatternTrigger);
	AddTriggerClass(DCAEdgeTrigger);
	AddTriggerClass(DropoutTrigger);
	AddTriggerClass(EdgeTrigger);
	AddTriggerClass(GlitchTrigger);
	AddTriggerClass(NthEdgeBurstTrigger);
	AddTriggerClass(PulseWidthTrigger);
	AddTriggerClass(RuntTrigger);
	AddTriggerClass(SlewRateTrigger);
	AddTriggerClass(UartTrigger);
	AddTriggerClass(WindowTrigger);
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
	char selfPath[1024] = "";
	ssize_t readlinkReturn = readlink("/proc/self/exe", selfPath, (sizeof(selfPath) - 1) );
	if ( readlinkReturn > 0)
	{
		string dir = dirname(selfPath);

		//If the binary directory is under /usr, do *not* search it!
		//We're probably in /usr/bin and we really do not want to be dlopen-ing every single thing in there.
		//See https://github.com/azonenberg/scopehal-apps/issues/393
		if(dir.find("/usr") != 0)
			search_dirs.push_back(dir);
	}

	//Home directory
	snprintf(tmp, sizeof(tmp), "%s/.scopehal/plugins", getenv("HOME"));
	search_dirs.push_back(tmp);

	for(auto dir : search_dirs)
	{
		DIR* hdir = opendir(dir.c_str());
		if(!hdir)
			continue;

		dirent* pent;
		while((pent = readdir(hdir)))
		{
			//Don't load hidden files or parent directory entries
			if(pent->d_name[0] == '.')
				continue;

			//Try loading it and see if it works.
			//(for now, never unload the plugins)
			string fname = dir + "/" + pent->d_name;
			void* hlib = dlopen(fname.c_str(), RTLD_NOW);
			if(hlib == NULL)
				continue;

			//If loaded, look for PluginInit()
			typedef void (*PluginInit)();
			PluginInit init = (PluginInit)dlsym(hlib, "PluginInit");
			if(!init)
				continue;

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
			auto extension = PathFindExtension(fileName);

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
				}

				FreeLibrary(module);
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
#ifdef __GNUC__
	if(v == 1)
		return 1;
	else
		return 1 << (64 - __builtin_clzl(v-1));
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
	string binRootDir;
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

		// On mingw, binPath would typically be /mingw64/bin now
		//and our data files in /mingw64/share. Strip back one more layer
		// of hierarchy so we can start appending.
		binRootDir = dirname(binPath);
	}
#else
	char binDir[1024] = {0};
	ssize_t readlinkReturn = readlink("/proc/self/exe", binDir, (sizeof(binDir) - 1) );
	if ( readlinkReturn <= 0 )
		LogError("Error: readlink() failed.\n");
	else if ( (unsigned) readlinkReturn > (sizeof(binDir) - 1) )
		LogError("Error: readlink() returned a path larger than our buffer.\n");
	else
	{
		g_searchPaths.push_back(dirname(binDir));
		binRootDir = dirname(binDir);
	}
#endif

	// Add the share directories associated with the binary location
	if(binRootDir.size() > 0)
	{
		g_searchPaths.push_back(binRootDir + "/share/glscopeclient");
		g_searchPaths.push_back(binRootDir + "/share/scopehal");
	}

	//Local directories preferred over system ones
#ifndef _WIN32
	string home = getenv("HOME");
	g_searchPaths.push_back(home + "/.glscopeclient");
	g_searchPaths.push_back(home + "/.scopehal");
	g_searchPaths.push_back("/usr/local/share/glscopeclient");
	g_searchPaths.push_back("/usr/local/share/scopehal");
	g_searchPaths.push_back("/usr/share/glscopeclient");
	g_searchPaths.push_back("/usr/share/scopehal");

	//for macports
	g_searchPaths.push_back("/opt/local/share/glscopeclient");
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
	FILE* fp = NULL;
	for(auto dir : g_searchPaths)
	{
		string path = dir + "/" + relpath;
		fp = fopen(path.c_str(), "rb");
		if(fp)
			break;
	}

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

	FILE* fp = NULL;
	for(auto dir : g_searchPaths)
	{
		string path = dir + "/" + relpath;
		fp = fopen(path.c_str(), "rb");
		if(fp)
			break;
	}

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
	for(auto dir : g_searchPaths)
	{
		string path = dir + "/" + relpath;
		FILE* fp = fopen(path.c_str(), "rb");
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

/**
	@brief Helper function that submits a command buffer to a queue and blocks until it completes
 */
void SubmitAndBlock(vk::raii::CommandBuffer& cmdBuf, vk::raii::Queue& queue)
{
	vk::raii::Fence fence(*g_vkComputeDevice, vk::FenceCreateInfo());
	vk::SubmitInfo info({}, {}, *cmdBuf);
	queue.submit(info, *fence);
	while(vk::Result::eTimeout == g_vkComputeDevice->waitForFences({*fence}, VK_TRUE, 1000 * 1000))
	{}
}

uint32_t GetComputeBlockCount(size_t numGlobal, size_t blockSize)
{
	uint32_t ret = numGlobal / blockSize;
	if(numGlobal % blockSize)
		ret ++;
	return ret;
}
