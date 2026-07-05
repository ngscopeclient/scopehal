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
	@brief Declaration of StreamDescriptor

	@ingroup core
 */
#ifndef StreamDescriptor_h
#define StreamDescriptor_h

class InstrumentChannel;
class InputConstraint;

/**
	@brief Descriptor for a single stream coming off a channel
 */
class StreamDescriptor
{
public:
	StreamDescriptor()
	: m_channel(NULL)
	, m_stream(0)
	{}

	StreamDescriptor(InstrumentChannel* channel, size_t stream = 0)
		: m_channel(channel)
		, m_stream(stream)
	{}

	///@return True if this is an invalid stream (index greater than the highest allowed value)
	bool IsOutOfRange()
	{
		if(m_channel == nullptr)
			return true;
		return (m_stream >= m_channel->GetStreamCount());
	}

	operator bool() const
	{ return (m_channel != nullptr); }

	void AddSink(FlowGraphNode* node);
	void RemoveSink(FlowGraphNode* node);
	const std::set<FlowGraphNode*>& GetSinks();

	std::string GetName() const;

	InstrumentChannel* m_channel;
	size_t m_stream;

	//None of these functions can be inlined here, because OscilloscopeChannel isn't fully declared yet.
	//See StreamDescriptor_inlines.h for implementations
	Unit GetXAxisUnits();
	Unit GetYAxisUnits();
	WaveformBase* GetData() const;
	bool operator==(const StreamDescriptor& rhs) const;
	bool operator!=(const StreamDescriptor& rhs) const;
	bool operator<(const StreamDescriptor& rhs) const;
	uint8_t GetFlags() const;
	float GetVoltageRange();
	float GetOffset();
	bool IsHighRateOffsetCapable();
	void SetVoltageRange(float v);
	void SetOffset(float v);
	Stream::StreamType GetType();
	float GetScalarValue();
	uint64_t GetDigitalScalarValue();
	size_t GetDigitalWidth();
	std::string PrettyPrintDigitalScalarHex();
	std::string PrettyPrintDigitalScalarBinary();
	std::string PrettyPrintDigitalScalarDecimal();
	bool IsInverted();
};

/**
	@brief Base class for filter graph inputs
	@ingroup core

	An individual node may override CreateInput() to create derived-class objects with additional metadata.
 */
class InputDescriptor
{
public:
	InputDescriptor(const std::string& name = "", const StreamDescriptor source = nullptr)
		: m_name(name)
		, m_sourceStream(source)
	{}

	virtual ~InputDescriptor()
	{}

	//not copyable or assignable
	InputDescriptor(const InputDescriptor& rhs) =delete;
	InputDescriptor& operator=(const InputDescriptor& rhs) =delete;

	//Porting helpers and trivial accessors
	Unit GetYAxisUnits()
	{ return m_sourceStream.GetYAxisUnits(); }

	Unit GetXAxisUnits()
	{ return m_sourceStream.GetXAxisUnits(); }

	WaveformBase* GetData() const
	{ return m_sourceStream.GetData(); }

	float GetVoltageRange()
	{ return m_sourceStream.GetVoltageRange(); }

	/**
		@brief Name of the input port displayed in the graph editor

		Must be unique within a given node
	 */
	std::string m_name;

	///@brief The stream, if any, connected to this input port
	StreamDescriptor m_sourceStream;

	///@brief Constraints that apply to this input
	std::shared_ptr<InputConstraint> m_constraints;
};


#endif
