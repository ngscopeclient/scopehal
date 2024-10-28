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
	@brief Declaration of IDTable class
	@ingroup core
 */
#ifndef IDTable_h
#define IDTable_h

/**
	@brief Bidirectional table mapping integer IDs in scopesession files to object pointers
	@ingroup core

	No type information is stored, the caller is responsible for knowing what type of object is being stored
	in the table.

	TODO: can we store RTTI info along with the objects to sanity check that we're using the right kind of object
 */
class IDTable : public Bijection<uintptr_t, void*>
{
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
	uintptr_t emplace(void* p)
	{
		if(HasID(p))
			return m_reverseMap[p];

		uint32_t id = m_nextID ++;
		Bijection::emplace(id, p);
		return id;
	}

	/**
		@brief Store a new object in the table using a specific ID

		@param id		ID to assign to the object
		@param p		Pointer to the object
	 */
	void emplace(uintptr_t id, void* p)
	{
		ReserveID(id);
		Bijection::emplace(id, p);
	}

	/**
		@brief Checks if we have an object at a specific pointer

		@param p		Pointer to the object
	 */
	bool HasID(void* p)
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
		@brief Deletes all entries from the table
	 */
	void clear()
	{
		m_forwardMap.clear();
		m_reverseMap.clear();
		m_nextID = 1;
	}

protected:

	///@brief Index of the next ID to be assigned
	uintptr_t m_nextID;
};

#endif
