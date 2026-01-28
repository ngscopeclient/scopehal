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
	@author Frederic BORRY
	@brief Inline functions for StreamGroupDescriptor
 */
#ifndef StreamGroupDescriptor_inlines_h
#define StreamGroupDescriptor_inlines_h

inline Unit StreamGroupDescriptor::GetXAxisUnits()
{
	if(m_channels.empty())
		return Unit(Unit::UNIT_FS);
	return m_channels[0]->GetXAxisUnits();
}

inline Unit StreamGroupDescriptor::GetYAxisUnits()
{
	if(m_channels.empty())
		return Unit(Unit::UNIT_VOLTS);
	else
		return m_channels[0]->GetYAxisUnits(0);
}

inline std::string StreamGroupDescriptor::GetName() const
{
	return m_groupName;
}

/**
	@brief Get the type of stream (if connected). Returns STREAM_TYPE_ANALOG if null.
 */
inline Stream::StreamType StreamGroupDescriptor::GetType()
{
	if(m_channels.empty())
		return Stream::STREAM_TYPE_DIGITAL;
	return m_channels[0]->GetType(0);
}

#endif
