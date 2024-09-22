/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of Bijection
	@ingroup core
 */
#ifndef Bijection_h
#define Bijection_h

/**
	@brief A strict one-to-one mapping from objects of type T1 to type T2 (which must be different types).

	Internally implemented as two synchronized std::map instances

	@ingroup core
 */
template<class T1, class T2, typename Compare1 = std::less<T1>, typename Compare2 = std::less<T2> >
class Bijection
{
public:

	typedef std::map<T1, T2, Compare1> forwardType;
	typedef std::map<T2, T1, Compare2> reverseType;

	///@brief
	typename forwardType::const_iterator begin()
	{ return m_forwardMap.begin(); }

	typename forwardType::const_iterator end()
	{ return m_forwardMap.end(); }

	void emplace(T1 a, T2 b)
	{
		m_forwardMap[a] = b;
		m_reverseMap[b] = a;
	}

	const T1& operator[](T2 key)
	{ return m_reverseMap[key]; }

	const T2& operator[](T1 key)
	{ return m_forwardMap[key]; }

	bool HasEntry(T1 key)
	{ return m_forwardMap.find(key) != m_forwardMap.end(); }

	bool HasEntry(T2 key)
	{ return m_reverseMap.find(key) != m_reverseMap.end(); }

	void clear()
	{
		m_forwardMap.clear();
		m_reverseMap.clear();
	}

	void erase(T1 key)
	{
		auto value = m_forwardMap[key];
		m_forwardMap.erase(key);
		m_reverseMap.erase(value);
	}

	void erase(T2 key)
	{
		auto value = m_reverseMap[key];
		m_reverseMap.erase(key);
		m_forwardMap.erase(value);
	}

	/**
		@brief Replaces one value with another, keeping the keys identical
	 */
	void replace(T2 oldval, T2 newval)
	{
		T1 key = m_reverseMap[oldval];
		m_reverseMap.erase(oldval);
		m_forwardMap[key] = newval;
		m_reverseMap[newval] = key;
	}

	size_t size()
	{ return m_forwardMap.size(); }

protected:
	forwardType m_forwardMap;
	reverseType m_reverseMap;
};

#endif
