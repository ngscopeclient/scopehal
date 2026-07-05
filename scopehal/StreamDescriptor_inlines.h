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
	@brief Inline functions for StreamDescriptor
 */
#ifndef StreamDescriptor_inlines_h
#define StreamDescriptor_inlines_h

inline Unit StreamDescriptor::GetXAxisUnits()
{
	if(m_channel == nullptr)
		return Unit(Unit::UNIT_COUNTS);
	else
		return m_channel->GetXAxisUnits();
}

inline Unit StreamDescriptor::GetYAxisUnits()
{
	if(m_channel == nullptr)
		return Unit(Unit::UNIT_VOLTS);
	else
		return m_channel->GetYAxisUnits(m_stream);
}

inline WaveformBase* StreamDescriptor::GetData() const
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

inline uint8_t StreamDescriptor::GetFlags() const
{
	if(m_channel == nullptr)
		return 0;
	else
		return m_channel->GetStreamFlags(m_stream);
}

inline float StreamDescriptor::GetVoltageRange()
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan == nullptr)
		return 1;
	else
		return schan->GetVoltageRange(m_stream);
}

inline float StreamDescriptor::GetOffset()
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan == nullptr)
		return 0;
	else
		return schan->GetOffset(m_stream);
}

inline bool StreamDescriptor::IsHighRateOffsetCapable()
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan == nullptr)
		return true;
	else
		return schan->IsHighRateOffsetCapable();
}

inline void StreamDescriptor::SetVoltageRange(float v)
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan)
		schan->SetVoltageRange(v, m_stream);
}

inline bool StreamDescriptor::IsInverted()
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan)
		return schan->IsInverted(m_stream);
	return false;
}

inline void StreamDescriptor::SetOffset(float v)
{
	auto schan = dynamic_cast<OscilloscopeChannel*>(m_channel);
	if(schan)
		schan->SetOffset(v, m_stream);
}

inline float StreamDescriptor::GetScalarValue()
{
	if(m_channel == nullptr)
		return 0;
	else
		return m_channel->GetScalarValue(m_stream);
}

inline uint64_t StreamDescriptor::GetDigitalScalarValue()
{
	if(m_channel == nullptr)
		return 0;
	else
		return m_channel->GetDigitalScalarValue(m_stream);
}

inline size_t StreamDescriptor::GetDigitalWidth()
{
	if(m_channel == nullptr)
		return 0;
	else
		return m_channel->GetDigitalWidth(m_stream);
}

inline void StreamDescriptor::AddSink(FlowGraphNode* node)
{
	if(m_channel)
		m_channel->AddSink(m_stream, node);
}

inline void StreamDescriptor::RemoveSink(FlowGraphNode* node)
{
	if(m_channel)
		m_channel->RemoveSink(m_stream, node);
}

inline const std::set<FlowGraphNode*>& StreamDescriptor::GetSinks()
{
	return m_channel->GetSinks(m_stream);
}

inline std::string StreamDescriptor::PrettyPrintDigitalScalarHex()
{
	auto width = GetDigitalWidth();
	uint64_t value = GetDigitalScalarValue();

	//Calculate number of hex digits
	int nibbles = ceil(width / 4.0);
	if(nibbles < 1)
		nibbles = 1;
	if(nibbles > 16)
		nibbles = 16;

	char format[16];
	snprintf(format, sizeof(format), "%d'h%%0%d%s", static_cast<int>(width), nibbles, PRIx64);

	//Actually format the value
	char tmp[32];
	snprintf(tmp, sizeof(tmp), format, value);

	return std::string(tmp);
}


inline std::string StreamDescriptor::PrettyPrintDigitalScalarBinary()
{
	auto width = GetDigitalWidth();
	uint64_t value = GetDigitalScalarValue();

	std::string ret = std::to_string(width) + "'b";
	for(int i=width-1; i >= 0; i--)
	{
		uint64_t mask = static_cast<uint64_t>(1) << i;
		if( (value & mask) == mask)
			ret += "1";
		else
			ret += "0";
	}

	return ret;
}

inline std::string StreamDescriptor::PrettyPrintDigitalScalarDecimal()
{
	auto width = GetDigitalWidth();
	uint64_t value = GetDigitalScalarValue();

	//Calculate number of decimal digits
	int digits = ceil(log10(pow(2, width)));
	if(digits > 16)
		digits = 16;

	char format[32];
	snprintf(format, sizeof(format), "%d'd%%0%d%s", static_cast<int>(width), digits, PRIu64);

	//Actually format the value
	char tmp[32];
	snprintf(tmp, sizeof(tmp), format, value);

	return std::string(tmp);
}

#endif
