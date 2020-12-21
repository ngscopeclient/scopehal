/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
#include "LeCroyOscilloscope.h"
#include "RigolOscilloscope.h"
#include "RohdeSchwarzOscilloscope.h"
#include "SignalGeneratorOscilloscope.h"
#include "SiglentSCPIOscilloscope.h"
#include "TektronixOscilloscope.h"

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

#ifdef HAVE_OPENCL
cl::Context* g_clContext = NULL;
vector<cl::Device> g_contextDevices;
#endif

AlignedAllocator<float, 32> g_floatVectorAllocator;

/**
	@brief Static initialization for SCPI transports
 */
void TransportStaticInit()
{
	AddTransportClass(SCPISocketTransport);
	AddTransportClass(SCPITMCTransport);
	AddTransportClass(SCPIUARTTransport);
	AddTransportClass(SCPINullTransport);
	AddTransportClass(VICPSocketTransport);

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

	//Check CPU features
	g_hasAvx512F = __builtin_cpu_supports("avx512f");
	g_hasAvx512VL = __builtin_cpu_supports("avx512vl");
	g_hasAvx512DQ = __builtin_cpu_supports("avx512dq");
	g_hasAvx2 = __builtin_cpu_supports("avx2");

	if(g_hasAvx2)
		LogDebug("* AVX2\n");
	if(g_hasAvx512F)
		LogDebug("* AVX512F\n");
	if(g_hasAvx512DQ)
		LogDebug("* AVX512DQ\n");
	if(g_hasAvx512VL)
		LogDebug("* AVX512VL\n");
	LogDebug("\n");
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
						devices[j].getInfo(CL_DEVICE_NAME, &dname);
						devices[j].getInfo(CL_DEVICE_OPENCL_C_VERSION, &dcvers);
						devices[j].getInfo(CL_DEVICE_PROFILE, &dprof);
						devices[j].getInfo(CL_DEVICE_VENDOR, &dvendor);
						devices[j].getInfo(CL_DEVICE_VERSION, &dversion);
						devices[j].getInfo(CL_DRIVER_VERSION, &ddversion);

						LogDebug("CL_DEVICE_NAME              = %s\n", dname.c_str());
						LogDebug("CL_DEVICE_OPENCL_C_VERSION  = %s\n", dcvers.c_str());
						LogDebug("CL_DEVICE_PROFILE           = %s\n", dprof.c_str());
						LogDebug("CL_DEVICE_VENDOR            = %s\n", dvendor.c_str());
						LogDebug("CL_DEVICE_VERSION           = %s\n", dversion.c_str());
						LogDebug("CL_DRIVER_VERSION           = %s\n", ddversion.c_str());
					}

					//For now, create a context on the first device of the first detected platform and hope for the best
					if(!g_clContext)
					{
						vector<cl::Device> devs;
						devs.push_back(devices[0]);

						//Passing CL_CONTEXT_PLATFORM as parameters seems to make context creation fail. Weird.
						g_clContext = new cl::Context(devs, NULL, NULL, NULL);
						g_contextDevices = g_clContext->getInfo<CL_CONTEXT_DEVICES>();
					}
				}
			}
		}
		catch(const cl::Error& e)
		{
			LogError("OpenCL error: %s (%d)\n", e.what(), e.err() );
			delete g_clContext;
			g_clContext = NULL;
			return;
		}

		#ifdef HAVE_CLFFT

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
	#ifdef HAVE_OPENCL
	#ifdef HAVE_CLFFT
	clfftTeardown();
	#endif
	#endif
}

/**
	@brief Static initialization for oscilloscopes
 */
void DriverStaticInit()
{
	DetectCPUFeatures();
	DetectGPUFeatures();

	AddDriverClass(AgilentOscilloscope);
	AddDriverClass(AntikernelLabsOscilloscope);
	AddDriverClass(AntikernelLogicAnalyzer);
	AddDriverClass(DemoOscilloscope);
	AddDriverClass(RigolOscilloscope);
	AddDriverClass(RohdeSchwarzOscilloscope);
	AddDriverClass(LeCroyOscilloscope);
	AddDriverClass(SiglentSCPIOscilloscope);
	AddDriverClass(SignalGeneratorOscilloscope);
	AddDriverClass(TektronixOscilloscope);

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
		search_dirs.push_back(dirname(selfPath));

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

	string ret(buf);
	delete[] buf;

	return ret;
}
