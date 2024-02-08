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
	@brief Declaration of FlowGraphNode
 */
#ifndef FlowGraphNode_h
#define FlowGraphNode_h

class OscilloscopeChannel;
class WaveformBase;

#include "FilterParameter.h"
#include "Waveform.h"
#include "Stream.h"

class InstrumentChannel;

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

	operator bool() const
	{ return (m_channel != NULL); }

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
	void SetVoltageRange(float v);
	void SetOffset(float v);
	Stream::StreamType GetType();
	float GetScalarValue();
};

/**
	@brief Abstract base class for a node in the signal flow graph.

	A FlowGraphNode has one or more channel inputs, zero or more configuration parameters.
 */
class FlowGraphNode
{
public:
	FlowGraphNode();
	virtual ~FlowGraphNode();

	void DetachInputs();

	//Inputs
public:
	size_t GetInputCount();
	std::string GetInputName(size_t i);

	void SetInput(size_t i, StreamDescriptor stream, bool force = false);
	void SetInput(const std::string& name, StreamDescriptor stream, bool force = false);
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	StreamDescriptor GetInput(size_t i);

	//Parameters
public:
	FilterParameter& GetParameter(std::string s);
	typedef std::map<std::string, FilterParameter> ParameterMapType;

	bool HasParameter(std::string s)
	{ return (m_parameters.find(s) != m_parameters.end()); }

	ParameterMapType::iterator GetParamBegin()
	{ return m_parameters.begin(); }

	ParameterMapType::iterator GetParamEnd()
	{ return m_parameters.end(); }

	size_t GetParamCount()
	{ return m_parameters.size(); }

	/**
		@brief Serializes this trigger's configuration to a YAML string.

		@return YAML block with this trigger's configuration
	 */
	virtual YAML::Node SerializeConfiguration(IDTable& table);

	/**
		@brief Load configuration from a save file
	 */
	virtual void LoadParameters(const YAML::Node& node, IDTable& table);
	virtual void LoadInputs(const YAML::Node& node, IDTable& table);

	bool IsDownstreamOf(std::set<FlowGraphNode*> nodes);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Accelerated waveform accessors

	enum DataLocation
	{
		LOC_CPU,
		LOC_GPU,
		LOC_DONTCARE
	};

	virtual DataLocation GetInputLocation();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Filter evaluation

	virtual void
	#ifndef _MSC_VER
	__attribute__((deprecated))
	#endif
	 Refresh();

	//GPU accelerated refresh method
	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue);

	//Input handling helpers
protected:

	/**
		@brief Gets the waveform attached to the specified input.

		This function is safe to call on a NULL input and will return NULL in that case.
	 */
	WaveformBase* GetInputWaveform(size_t i);	//implementation in FlowGraphNode_inlines.h

	///@brief Gets the analog waveform attached to the specified input
	SparseAnalogWaveform* GetSparseAnalogInputWaveform(size_t i)
	{ return dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(i)); }

	///@brief Gets the analog waveform attached to the specified input
	UniformAnalogWaveform* GetUniformAnalogInputWaveform(size_t i)
	{ return dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(i)); }

	///@brief Gets the digital waveform attached to the specified input
	SparseDigitalWaveform* GetSparseDigitalInputWaveform(size_t i)
	{ return dynamic_cast<SparseDigitalWaveform*>(GetInputWaveform(i)); }

	///@brief Gets the digital waveform attached to the specified input
	UniformDigitalWaveform* GetUniformDigitalInputWaveform(size_t i)
	{ return dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(i)); }

	///Gets the digital bus waveform attached to the specified input
	SparseDigitalBusWaveform* GetSparseDigitalBusInputWaveform(size_t i)
	{ return dynamic_cast<SparseDigitalBusWaveform*>(GetInputWaveform(i)); }

	/**
		@brief Creates and names an input signal
	 */
	void CreateInput(const std::string& name)
	{
		m_signalNames.push_back(name);
		m_inputs.push_back(StreamDescriptor(NULL, 0));
	}

	std::string GetInputDisplayName(size_t i);

protected:
	///Names of signals we take as input
	std::vector<std::string> m_signalNames;

	///The channel (if any) connected to each of our inputs
	std::vector<StreamDescriptor> m_inputs;

	//Parameters
	ParameterMapType m_parameters;

public:

	sigc::signal<void()> signal_parametersChanged()
	{ return m_parametersChangedSignal; }

	sigc::signal<void()> signal_inputsChanged()
	{ return m_inputsChangedSignal; }

protected:

	///@brief Signal emitted when the set of parameters changes
	sigc::signal<void()> m_parametersChangedSignal;

	///@brief Signal emitted when the set of inputs changes
	sigc::signal<void()> m_inputsChangedSignal;
};

#endif
