/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#ifndef PipelineCacheManager_h
#define PipelineCacheManager_h

/**
	@brief Helper for managing Vulkan / vkFFT pipeline cache objects

	The cache is stored on disk under the .cache/glscopeclient directory on Linux, or FIXME on Windows.

	Structure is $cachedir/shaders/[key].yml

	YAML schema for each file:
		devname: xxx
		driver_ver: xxx
		uuid: xxx
		crc: xxx
		data: xxx

 */
class PipelineCacheManager
{
public:
	PipelineCacheManager();
	~PipelineCacheManager();

	std::shared_ptr< std::vector<uint8_t> > Lookup(const std::string& key);
	void Store(const std::string& key, std::shared_ptr< std::vector<uint8_t> > value);

	void LoadFromDisk();
	void SaveToDisk();

protected:

	///@brief Mutex to interlock access to the cache
	std::mutex m_mutex;

	///@brief The actual cache data store
	std::map<std::string, std::shared_ptr<std::vector<uint8_t> > > m_cache;
};

#endif
