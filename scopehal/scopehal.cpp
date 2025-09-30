/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
#include "PicoOscilloscope.h"
#include "RigolOscilloscope.h"
#include "RohdeSchwarzOscilloscope.h"
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
#include "NthEdgeBurstTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"

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
	AddDriverClass(RSRTO6Oscilloscope);
	AddDriverClass(LeCroyOscilloscope);
	AddDriverClass(LeCroyFWPOscilloscope);
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
	uint32_t poly = 0xedb88320;

	uint32_t crc = 0xffffffff;
	for(size_t n=start; n <= end; n++)
	{
		uint8_t d = bytes[n];
		for(int i=0; i<8; i++)
		{
			bool b = ( crc ^ (d >> i) ) & 1;
			crc >>= 1;
			if(b)
				crc ^= poly;
		}
	}

	return ~(	((crc & 0x000000ff) << 24) |
				((crc & 0x0000ff00) << 8) |
				((crc & 0x00ff0000) >> 8) |
				 (crc >> 24) );
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
