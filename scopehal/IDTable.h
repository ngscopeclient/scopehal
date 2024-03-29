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
	@brief Declaration of IDTable class
 */
#ifndef IDTable_h
#define IDTable_h

class IDTable : public Bijection<uintptr_t, void*>
{
public:
	IDTable()
	: m_nextID(1)
	{
		emplace(0, NULL);
	}

	uintptr_t emplace(void* p)
	{
		if(HasID(p))
			return m_reverseMap[p];

		uint32_t id = m_nextID ++;
		Bijection::emplace(id, p);
		return id;
	}

	void emplace(uintptr_t id, void* p)
	{
		ReserveID(id);
		Bijection::emplace(id, p);
	}

	bool HasID(void* p)
	{ return (m_reverseMap.find(p) != m_reverseMap.end()); }

	bool HasID(uintptr_t id)
	{ return (m_forwardMap.find(id) != m_forwardMap.end()); }

	/*
		@brief Reserves an ID, guaranteeing that we will never use it for anything else
	 */
	void ReserveID(uintptr_t id)
	{ m_nextID = std::max(m_nextID, id+1); }

	/**
		@brief Deletes all entries from the table
	 */
	void clear()
	{
		m_forwardMap.clear();
		m_reverseMap.clear();
		m_nextID = 1;
	}

protected:
	uintptr_t m_nextID;
};

#endif
