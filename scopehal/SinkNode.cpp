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

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SinkNode::SinkNode()
{
}

SinkNode::~SinkNode()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dummy refresh function since most sinks don't do actual compute

void SinkNode::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers for dynamic port count

/**
	@brief Regenerate the list of inputs so that each port has a unique, monotonic name
 */
void SinkNode::RefreshInputNames()
{
	for(size_t i=0; i<m_inputs.size(); i++)
		m_inputs[i]->m_name = string("in") + to_string(i);
}

/**
	@brief Removes any empty spaces from m_inputs
 */
void SinkNode::ClearEmptyInputs()
{
	//do NOT cache input count here, it can change during the loop
	for(size_t i=0; i<GetInputCount(); i++)
	{
		if(m_inputs[i]->m_sourceStream == nullptr)
		{
			LogTrace("Input %zu was null, removing\n", i);
			RemoveStream(i);
		}
	}
}

/**
	@brief Remove a stream connected to one of our inputs but DOES NOT free the reference
 */
void SinkNode::RemoveStream(size_t i)
{
	//Break the link to us
	auto stream = m_inputs[i]->m_sourceStream;
	m_inputs.erase(m_inputs.begin() + i);

	//Remove us from the sink list
	stream.RemoveSink(this);

	//Make names consistent again
	RefreshInputNames();
}

/**
	@brief Get the position of the provided Stream in this SinkNode

	@param desc the stream to get the position for
	@return the position of the stream if found or the number of inputs if not
 */
size_t SinkNode::GetStreamPosition(StreamDescriptor desc)
{
	for (size_t i = 0; i < m_inputs.size(); ++i)
	{
		auto c = m_inputs[i];
		if(c && c->m_sourceStream == desc)
			return i;
	}

	return m_inputs.size();
}

/**
	@brief Move a stream to another position in this plot
	@param desc the stream to move
	@param newPosition the position to move the stream to
 */
void SinkNode::MoveStream(StreamDescriptor desc, size_t newPosition)
{
	// Find original position
	size_t oldIndex = GetStreamPosition(desc);
	if (oldIndex == m_inputs.size())	// Not found
		return;

	if (oldIndex == newPosition)		// Nothing to do
		return;

	// Backup value
	auto temp = m_inputs[oldIndex];

	// Remove from list
	m_inputs.erase(m_inputs.begin() + oldIndex);

	// If we move after, index has to be shifted
	if (oldIndex < newPosition)
		newPosition--;

	// Insert at new position
	m_inputs.insert(m_inputs.begin() + newPosition, temp);

	//Make names consistent again
	RefreshInputNames();
}
