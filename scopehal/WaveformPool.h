/***********************************************************************************************************************
*                                                                                                                      *
* ngscopeclient                                                                                                        *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg                                                                          *
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
	@brief  Declaration of WaveformPool
 */
#ifndef WaveformPool_h
#define WaveformPool_h

/**
	@brief Thread safe memory pool for reusing Waveform objects
 */
class WaveformPool
{
public:
	WaveformPool()
	: m_maxSize(16)
	{}

	~WaveformPool()
	{
		for(auto w : m_waveforms)
			delete w;
		m_waveforms.clear();
	}

	/**
		@brief Adds a new waveform to the pool if there's space for it, otherwise free it
	 */
	void Add(WaveformBase* w)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		w->Rename("WaveformPool.freelist");

		if(m_waveforms.size() < m_maxSize)
			m_waveforms.push_back(w);
		else
			delete w;
	}

	/**
		@brief Gets a waveform from the pool if there's space for it, otherwise return null
	 */
	WaveformBase* Get()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if(m_waveforms.empty())
			return nullptr;

		auto ret = *m_waveforms.begin();
		ret->m_revision ++;
		m_waveforms.pop_front();

		ret->Rename("WaveformPool.allocated");
		return ret;
	}

protected:
	size_t m_maxSize;

	std::mutex m_mutex;

	std::list<WaveformBase*> m_waveforms;
};

#endif
