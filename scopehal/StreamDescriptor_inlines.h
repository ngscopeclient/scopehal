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
	@brief Inline functions for StreamDescriptor
 */
#ifndef StreamDescriptor_inlines_h
#define StreamDescriptor_inlines_h

inline Unit StreamDescriptor::GetXAxisUnits()
{
	return m_channel->GetXAxisUnits();
}

inline Unit StreamDescriptor::GetYAxisUnits()
{
	return m_channel->GetYAxisUnits(m_stream);
}

inline WaveformBase* StreamDescriptor::GetData()
{
	if(m_channel == nullptr)
		return nullptr;
	else
		return m_channel->GetData(m_stream);
}

inline bool StreamDescriptor::operator==(const StreamDescriptor& rhs) const
{
	return (m_channel == rhs.m_channel) && (m_stream == rhs.m_stream);
}

inline bool StreamDescriptor::operator!=(const StreamDescriptor& rhs) const
{
	return (m_channel != rhs.m_channel) || (m_stream != rhs.m_stream);
}

inline bool StreamDescriptor::operator<(const StreamDescriptor& rhs) const
{
	if(m_channel < rhs.m_channel)
		return true;
	if( (m_channel == rhs.m_channel) && (m_stream < rhs.m_stream) )
		return true;

	return false;
}

inline uint8_t StreamDescriptor::GetFlags()
{
	if(m_channel == NULL)
		return 0;
	else
		return m_channel->GetStreamFlags(m_stream);
}

inline float StreamDescriptor::GetVoltageRange()
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan == NULL)
		return 1;
	else
		return schan->GetVoltageRange(m_stream);
}

inline float StreamDescriptor::GetOffset()
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan == NULL)
		return 0;
	else
		return schan->GetOffset(m_stream);
}

inline void StreamDescriptor::SetVoltageRange(float v)
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan)
		schan->SetVoltageRange(v, m_stream);
}

inline void StreamDescriptor::SetOffset(float v)
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan)
		schan->SetOffset(v, m_stream);
}

#endif
