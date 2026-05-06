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
	@brief Declaration of IDTable class
	@ingroup core
 */
#ifndef IDTable_h
#define IDTable_h

#include "SerializableObject.h"
#include <type_traits>

/**
	@brief Bidirectional table mapping integer IDs in scopesession files to object pointers
	@ingroup core

	Pointers must be SerializableObject derived to ensure type safety
 */
class IDTable
{
public:

	///@brief Type of the forward map
	typedef std::map<uintptr_t, SerializableObject*, std::less<uintptr_t> > forwardType;

	///@brief Type of the reverse map
	typedef std::map<SerializableObject*, uintptr_t, std::less<SerializableObject*> > reverseType;

	///@brief Get an iterator to the start of the forward map
	typename forwardType::const_iterator begin()
	{ return m_forwardMap.begin(); }

	///@brief Get an iterator to the end of the forward map
	typename forwardType::const_iterator end()
	{ return m_forwardMap.end(); }

public:
	IDTable()
	: m_nextID(1)
	{
		emplace(0, nullptr);
	}

	/**
		@brief Store a new object in the table

		@param p		Pointer to the object

		@return The ID of the object
	 */
	uintptr_t emplace(SerializableObject* p)
	{
		if(HasID(p))
			return m_reverseMap[p];

		uint32_t id = m_nextID ++;
		m_forwardMap[id] = p;
		m_reverseMap[p] = id;
		return id;
	}

	/**
		@brief Store a new object in the table using a specific ID

		@param id		ID to assign to the object
		@param p		Pointer to the object
	 */
	void emplace(uintptr_t id, SerializableObject* p)
	{
		ReserveID(id);
		m_forwardMap[id] = p;
		m_reverseMap[p] = id;
	}

	/**
		@brief Checks if we have an object at a specific pointer

		@param p		Pointer to the object
	 */
	bool HasID(SerializableObject* p)
	{ return (m_reverseMap.find(p) != m_reverseMap.end()); }

	/**
		@brief Checks if we have an object with a specific ID

		@param id		ID of the object
	 */
	bool HasID(uintptr_t id)
	{ return (m_forwardMap.find(id) != m_forwardMap.end()); }

	/**
		@brief Marks an ID as unavailable for use, without assigning an pointer to it
	 */
	void ReserveID(uintptr_t id)
	{ m_nextID = std::max(m_nextID, id+1); }

	/**
		@brief Type-safe object lookup
	 */
	template<class T>
	T* Lookup(uintptr_t id)
	{
		static_assert(std::is_base_of_v<SerializableObject, T> == true);
		return dynamic_cast<T*>(m_forwardMap[id]);
	}

	///@brief Forward lookup
	uintptr_t operator[](SerializableObject* key)
	{ return m_reverseMap[key]; }

	/**
		@brief Deletes all entries from the table
	 */
	void clear()
	{
		m_forwardMap.clear();
		m_reverseMap.clear();
		m_nextID = 1;
	}

	/**
		@brief Erase an entry given a forward key

		@param key	The object to remove
	 */
	void erase(uintptr_t key)
	{
		auto value = m_forwardMap[key];
		m_forwardMap.erase(key);
		m_reverseMap.erase(value);
	}

	/**
		@brief Erase an entry given a reverse key

		@param key	The object to remove
	 */
	void erase(SerializableObject* key)
	{
		auto value = m_reverseMap[key];
		m_reverseMap.erase(key);
		m_forwardMap.erase(value);
	}

	/**
		@brief Replaces one value with another, keeping the keys identical
	 */
	void replace(SerializableObject* oldval, SerializableObject* newval)
	{
		uintptr_t key = m_reverseMap[oldval];
		m_reverseMap.erase(oldval);
		m_forwardMap[key] = newval;
		m_reverseMap[newval] = key;
	}

	///@brief Return the number of entries in the bijection
	size_t size() const
	{ return m_forwardMap.size(); }

protected:

	///@brief Index of the next ID to be assigned
	uintptr_t m_nextID;

	///@brief Map of object-to-object in the forward direction
	forwardType m_forwardMap;

	///@brief Map of object-to-object in the reverse direction
	reverseType m_reverseMap;
};

#endif
