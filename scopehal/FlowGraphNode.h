/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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

	StreamDescriptor(OscilloscopeChannel* channel, size_t stream = 0)
		: m_channel(channel)
		, m_stream(stream)
	{}

	std::string GetName();

	OscilloscopeChannel* m_channel;
	size_t m_stream;

	WaveformBase* GetData()
	{ return m_channel->GetData(m_stream); }

	bool operator==(const StreamDescriptor& rhs) const
	{ return (m_channel == rhs.m_channel) && (m_stream == rhs.m_stream); }

	bool operator!=(const StreamDescriptor& rhs) const
	{ return (m_channel != rhs.m_channel) || (m_stream != rhs.m_stream); }

	bool operator<(const StreamDescriptor& rhs) const
	{
		if(m_channel < rhs.m_channel)
			return true;
		if( (m_channel == rhs.m_channel) && (m_stream < rhs.m_stream) )
			return true;

		return false;
	}
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
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) =0;

	StreamDescriptor GetInput(size_t i);

	//Parameters
public:
	FilterParameter& GetParameter(std::string s);
	typedef std::map<std::string, FilterParameter> ParameterMapType;

	ParameterMapType::iterator GetParamBegin()
	{ return m_parameters.begin(); }

	ParameterMapType::iterator GetParamEnd()
	{ return m_parameters.end(); }

	//Input handling helpers
protected:

	/**
		@brief Gets the waveform attached to the specified input.

		This function is safe to call on a NULL input and will return NULL in that case.
	 */
	WaveformBase* GetInputWaveform(size_t i)
	{
		auto chan = m_inputs[i].m_channel;
		if(chan == NULL)
			return NULL;
		return chan->GetData(m_inputs[i].m_stream);
	}

	///Gets the analog waveform attached to the specified input
	AnalogWaveform* GetAnalogInputWaveform(size_t i)
	{ return dynamic_cast<AnalogWaveform*>(GetInputWaveform(i)); }

	///Gets the digital waveform attached to the specified input
	DigitalWaveform* GetDigitalInputWaveform(size_t i)
	{ return dynamic_cast<DigitalWaveform*>(GetInputWaveform(i)); }

	///Gets the digital bus waveform attached to the specified input
	DigitalBusWaveform* GetDigitalBusInputWaveform(size_t i)
	{ return dynamic_cast<DigitalBusWaveform*>(GetInputWaveform(i)); }

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
};

#endif
