/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "scopehal.h"
#include "PipelineCacheManager.h"
#include "FileSystem.h"
#include "VulkanFFTPlan.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <wordexp.h>
#endif

using namespace std;

unique_ptr<PipelineCacheManager> g_pipelineCacheMgr;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief
 */
PipelineCacheManager::PipelineCacheManager()
{
	FindPath();
	LoadFromDisk();
}

/**
	@brief Destroys the cache and writes it out to disk
 */
PipelineCacheManager::~PipelineCacheManager()
{
	SaveToDisk();
	Clear();
}

void PipelineCacheManager::FindPath()
{
#ifdef _WIN32
	wchar_t* stem;
	if(S_OK != SHGetKnownFolderPath(
		FOLDERID_RoamingAppData,
		KF_FLAG_CREATE,
		NULL,
		&stem))
	{
		throw std::runtime_error("failed to resolve %appdata%");
	}

	wchar_t directory[MAX_PATH];
	if(NULL == PathCombineW(directory, stem, L"ngscopeclient"))
	{
		throw runtime_error("failed to build directory path");
	}

	// Ensure the directory exists
	const auto result = CreateDirectoryW(directory, NULL);
	m_cacheRootDir = NarrowPath(directory) + "\\";

	if(!result && GetLastError() != ERROR_ALREADY_EXISTS)
	{
		throw runtime_error("failed to create preferences directory");
	}

	CoTaskMemFree(static_cast<void*>(stem));
#else
	// Ensure all directories in path exist
	CreateDirectory("~/.cache");
	CreateDirectory("~/.cache/ngscopeclient");
	m_cacheRootDir = ExpandPath("~/.cache/ngscopeclient") + "/";
#endif

	LogTrace("Cache root directory is %s\n", m_cacheRootDir.c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual cache logic

/**
	@brief Removes all content from the cache
 */
void PipelineCacheManager::Clear()
{
	lock_guard<mutex> lock(m_mutex);
	m_vkCache.clear();
	m_rawDataCache.clear();
}

/**
	@brief Look up a blob which may or may not be in the cache
 */
shared_ptr< vector<uint8_t> > PipelineCacheManager::LookupRaw(const string& key)
{
	lock_guard<mutex> lock(m_mutex);
	if(m_rawDataCache.find(key) != m_rawDataCache.end())
	{
		LogTrace("Hit for raw %s\n", key.c_str());
		return m_rawDataCache[key];
	}

	LogTrace("Miss for raw %s\n", key.c_str());
	return nullptr;
}

/**
	@brief Store a raw blob to the cache
 */
void PipelineCacheManager::StoreRaw(const string& key, shared_ptr< vector<uint8_t> > value)
{
	lock_guard<mutex> lock(m_mutex);
	m_rawDataCache[key] = value;

	LogTrace("Store raw: %s (%zu bytes)\n", key.c_str(), value->size());
}

/**
	@brief Returns a Vulkan pipeline cache object for the given path.

	If not found, a new cache object is created and returned.
 */
shared_ptr<vk::raii::PipelineCache> PipelineCacheManager::Lookup(const string& key, time_t target)
{
	lock_guard<mutex> lock(m_mutex);

	//Already in the cache? Return that copy
	if(m_vkCache.find(key) != m_vkCache.end())
	{
		if(m_vkCacheTimestamps[key] != target)
			LogTrace("Ignoring out of date cache entry for %s\n", key.c_str());
		else
		{
			LogTrace("Hit for pipeline %s\n", key.c_str());
			return m_vkCache[key];
		}
	}

	//Nope, make a new empty cache object and return it
	LogTrace("Miss for pipeline %s\n", key.c_str());
	vk::PipelineCacheCreateInfo info({},{});
	auto ret = make_shared<vk::raii::PipelineCache>(*g_vkComputeDevice, info);
	m_vkCache[key] = ret;
	m_vkCacheTimestamps[key] = target;

	//Name it
	if(g_hasDebugUtils)
	{
		string name = string("PipelineCache.") + key;
		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::ePipelineCache,
				reinterpret_cast<uint64_t>(static_cast<VkPipelineCache>(**ret)),
				name.c_str()));
	}

	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

/**
	@brief Loads cache content from disk
 */
void PipelineCacheManager::LoadFromDisk()
{
	lock_guard<mutex> lock(m_mutex);

	LogTrace("Loading pipeline cache\n");
	LogIndenter li;

	PipelineCacheFileHeader header;
	int vkfft_expected = VkFFTGetVersion();

	string rawPrefix = "raw_";
	string shaderPrefix = "pipeline_";
	string shaderSuffix = ".bin";

	//Load raw binary blobs (mostly for vkFFT)
	auto prefix = m_cacheRootDir + "shader_";
	auto files = Glob(prefix + "*", false);
	for(auto f : files)
	{
		if(f.find(prefix) == string::npos)
			f = m_cacheRootDir + f;

		//Extract the key from the file name
		auto key = f.substr(prefix.length());
		key = key.substr(0, key.length() - shaderSuffix.length());
		bool typeIsRaw = (key.find(rawPrefix) == 0);
		if(typeIsRaw)
			key = key.substr(rawPrefix.length());
		else
			key = key.substr(shaderPrefix.length());

		//Read the header and make sure it checks out
		FILE* fp = fopen(f.c_str(), "rb");
		if(1 != fread(&header, sizeof(header), 1, fp))
		{
			LogWarning("Read cache header failed (%s)\n", f.c_str());
			fclose(fp);
			continue;
		}

		LogTrace("Loading cache object %s (from %s, timestamp %zu)\n", key.c_str(), f.c_str(), (size_t)header.file_mtime);
		LogIndenter li2;

		if(0 != memcmp(header.cache_uuid, g_vkComputeDeviceUuid, 16))
		{
			LogTrace("Rejecting cache file (%s) due to mismatching UUID\n", f.c_str());
			fclose(fp);
			continue;
		}
		if(header.vkfft_ver != vkfft_expected)
		{
			LogTrace("Rejecting cache file (%s) due to mismatching vkFFT version\n", f.c_str());
			fclose(fp);
			continue;
		}
		if(header.driver_ver != g_vkComputeDeviceDriverVer)
		{
			LogTrace("Rejecting cache file (%s) due to mismatching driver version\n", f.c_str());
			fclose(fp);
			continue;
		}

		//All good. Read the file content
		auto p = make_shared< vector<uint8_t> >();
		p->resize(header.len);
		if(header.len != fread(&((*p)[0]), 1, header.len, fp))
		{
			LogWarning("Read cache content failed (%s)\n", f.c_str());
			fclose(fp);
			continue;
		}
		fclose(fp);

		//Verify the CRC
		if(header.crc != CRC32(*p))
		{
			LogWarning("Rejecting cache file (%s) due to bad CRC\n", f.c_str());
			continue;
		}

		//Done, add to cache if we get this far
		if(typeIsRaw)
			m_rawDataCache[key] = p;
		else
		{
			vector<uint8_t>& vec = *p;
			vk::PipelineCacheCreateInfo info({}, vec.size(), &vec[0]);
			auto ret = make_shared<vk::raii::PipelineCache>(*g_vkComputeDevice, info);
			m_vkCache[key] = ret;
			m_vkCacheTimestamps[key] = header.file_mtime;
		}
	}
}

/**
	@brief Writes cache content out to disk
 */
void PipelineCacheManager::SaveToDisk()
{
	lock_guard<mutex> lock(m_mutex);

	LogTrace("Saving cache\n");
	LogIndenter li;

	PipelineCacheFileHeader header;
	memcpy(header.cache_uuid, g_vkComputeDeviceUuid, 16);
	header.driver_ver = g_vkComputeDeviceDriverVer;
	header.vkfft_ver = VkFFTGetVersion();

	//Save raw data
	for(auto it : m_rawDataCache)
	{
		auto key = it.first;
		auto& vec = *it.second;
		auto fname = m_cacheRootDir + "shader_raw_" + key + ".bin";
		LogTrace("Saving shader %s (%zu bytes)\n", fname.c_str(), vec.size());
		FILE* fp = fopen(fname.c_str(), "wb");

		//Write the cache header
		header.len = vec.size();
		header.crc = CRC32(vec);
		header.file_mtime = 0;	//not used
		if(1 != fwrite(&header, sizeof(header), 1, fp))
		{
			LogWarning("Write cache header failed (%s)\n", fname.c_str());
			fclose(fp);
			continue;
		}

		//Write the data
		if(header.len != fwrite(&vec[0], 1, header.len, fp))
			LogWarning("Write cache data failed (%s)\n", fname.c_str());

		fclose(fp);
	}

	//Save Vulkan shader cache
	for(auto it : m_vkCache)
	{
		auto key = it.first;
		auto pcache = it.second;
		auto fname = m_cacheRootDir + "shader_pipeline_" + key + ".bin";

		//Extract the raw shader content
		auto vec = pcache->getData();
		LogTrace("Saving shader %s (%zu bytes)\n", fname.c_str(), vec.size());

		FILE* fp = fopen(fname.c_str(), "wb");

		//Write the cache header
		header.len = vec.size();
		header.crc = CRC32(vec);
		header.file_mtime = m_vkCacheTimestamps[key];
		if(1 != fwrite(&header, sizeof(header), 1, fp))
		{
			LogWarning("Write cache header failed (%s)\n", fname.c_str());
			fclose(fp);
			continue;
		}

		//Write the data
		if(header.len != fwrite(&vec[0], 1, header.len, fp))
			LogWarning("Write cache data failed (%s)\n", fname.c_str());

		fclose(fp);
	}
}
