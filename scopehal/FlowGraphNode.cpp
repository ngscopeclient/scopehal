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
// StreamDescriptor

string StreamDescriptor::GetName() const
{
	if(m_channel == nullptr)
		return "NULL";

	string name = m_channel->GetDisplayName();
	if(m_channel->GetStreamCount() > 1)
		name += string(".") + m_channel->GetStreamName(m_stream);
	return name;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FlowGraphNode::FlowGraphNode()
{
}

FlowGraphNode::~FlowGraphNode()
{
	//Release any inputs we currently have refs to
	for(auto c : m_inputs)
	{
		auto schan = dynamic_cast<OscilloscopeChannel*>(c.m_channel);
		if(schan)
			schan->Release();
	}
}

/**
	@brief Disconnects all inputs from the node without releasing them.

	This function is intended for use in Oscilloscope::~Oscilloscope() only.
	Using it carelessly is likely to lead to memory leaks.
 */
void FlowGraphNode::DetachInputs()
{
	for(auto& c : m_inputs)
	{
		c.m_channel = nullptr;

		//TODO: disconnect us from their sinks
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accelerated waveform processing

/**
	@brief Evaluates a filter graph node.

	This version does not support using Vulkan acceleration and should be considered deprecated. It will be
	removed in the indefinite future once all filters have been converted to the new API.
 */
void FlowGraphNode::Refresh()
{
}

/**
	@brief Evaluates a filter graph node, using GPU acceleration if possible

	The default implementation calls the legacy non-accelerated Refresh() method.
 */
void FlowGraphNode::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated"

	//This is the deprecated legacy method signature, but we don't want to warn on this call to it
	Refresh();

	#pragma GCC diagnostic pop
}

/**
	@brief Gets the desired location of the nodes's input data

	The default implementation returns CPU.

	@return		LOC_CPU: if the filter assumes input waveforms are readable from the CPU
				LOC_GPU: if the filter assumes input waveforms are readable from the GPU
				LOC_DONTCARE: if the filter manages its own input memory, or can work with either CPU or GPU input
 */
FlowGraphNode::DataLocation FlowGraphNode::GetInputLocation()
{
	return LOC_CPU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

FilterParameter& FlowGraphNode::GetParameter(string s)
{
	if(m_parameters.find(s) == m_parameters.end())
		LogError("Invalid parameter name \"%s\"\n", s.c_str());

	return m_parameters[s];
}

size_t FlowGraphNode::GetInputCount()
{
	return m_signalNames.size();
}

string FlowGraphNode::GetInputName(size_t i)
{
	if(i < m_signalNames.size())
		return m_signalNames[i];
	else
	{
		LogError("Invalid channel index %zu in FlowGraphNode::GetInputName()\n", i);
		return "";
	}
}

/**
	@brief Called when a new input is connected to the node

	The default implementation does nothing, but some special cases may find this hook useful.

	For example, instrument channels may make hardware changes as soon as a new input is connected.
 */
void FlowGraphNode::OnInputChanged([[maybe_unused]] size_t i)
{
}

/**
	@brief Connects a stream to the input of this node

	@param i		Index of the input port to connect
	@param stream	Input data stream
	@param force	Forcibly connect this stream without checking to make sure it's the right type.
					Should only be set true by by Filter::LoadInputs() or in similar specialized situations.
 */
void FlowGraphNode::SetInput(size_t i, StreamDescriptor stream, bool force)
{
	if(i < m_signalNames.size())
	{
		//Calling SetInput with the current input is a legal no-op
		if(stream == m_inputs[i])
			return;

		if(stream.m_channel == nullptr)	//NULL is always legal
		{
			//Remove us from the old input's sink list
			if(m_inputs[i].m_channel)
				m_inputs[i].m_channel->m_sinks[m_inputs[i].m_stream].erase(this);

			//Deref whatever was there (if anything).
			auto oldchan = dynamic_cast<OscilloscopeChannel*>(m_inputs[i].m_channel);
			if(oldchan)
				oldchan->Release();

			//Make the new connection
			m_inputs[i] = StreamDescriptor(nullptr, 0);

			//Notify the derived class in case it wants to do anything
			OnInputChanged(i);
			return;
		}

		//If not forcing, make sure the input is legal
		if(!force)
		{
			if(!ValidateChannel(i, stream))
			{
				//If validation fails, set the input to null
				LogError("Invalid channel for input %zu of node\n", i);
				SetInput(i, StreamDescriptor(nullptr, 0), false);
				return;
			}
		}

		/*
			It's critical to ref the new input *before* dereffing the current one (#432).

			Consider a 3-node filter chain A -> B -> C, A and B offscreen.
			If we set C's input to A's output, B now has no loads and will get GC'd.
			... but now A has no loads!

			This causes A to get GC'd right before we hook up C's input to it, and Bad Things(tm) happen.
		 */
		auto schan = dynamic_cast<OscilloscopeChannel*>(stream.m_channel);
		if(schan)
			schan->AddRef();

		//Remove us from the old input's sink list
		if(m_inputs[i].m_channel)
			m_inputs[i].m_channel->m_sinks[m_inputs[i].m_stream].erase(this);

		//Deref whatever was there (if anything).
		auto oldchan = dynamic_cast<OscilloscopeChannel*>(m_inputs[i].m_channel);
		if(oldchan)
			oldchan->Release();

		//All good, we can save the new input
		m_inputs[i] = stream;

		//Notify the derived class in case it wants to do anything
		OnInputChanged(i);
	}
	else
	{
		LogError("Invalid channel index %zu in FlowGraphNode::SetInput()\n", i);
	}
}

/**
	@brief Connects a stream to the input of this filter

	@param name		Name of the input port to connect
	@param stream	Input data stream
	@param force	Forcibly connect this stream without checking to make sure it's the right type.
					Should only be set true by by Filter::LoadInputs() or in similar specialized situations.
 */
void FlowGraphNode::SetInput(const string& name, StreamDescriptor stream, bool force)
{
	//Find the channel
	for(size_t i=0; i<m_signalNames.size(); i++)
	{
		if(m_signalNames[i] == name)
		{
			SetInput(i, stream, force);
			return;
		}
	}

	//Not found
	LogError("Invalid channel name \"%s\" in FlowGraphNode::SetInput()\n", name.c_str());
}

/**
	@brief Gets the descriptor for one of our inputs
 */
StreamDescriptor FlowGraphNode::GetInput(size_t i)
{
	if(i < m_signalNames.size())
		return m_inputs[i];
	else
	{
		LogError("Invalid channel index %zu in FlowGraphNode::GetInput()\n", i);
		return StreamDescriptor(NULL, 0);
	}
}

/**
	@brief Gets the display name for one of our inputs.

	This includes the stream name iff the input comes from a multi-stream source.
 */
string FlowGraphNode::GetInputDisplayName(size_t i)
{
	auto in = m_inputs[i];
	if(in.m_channel == NULL)
		return "NULL";
	else if(in.m_channel->GetStreamCount() > 1)
		return in.m_channel->GetDisplayName() + "." + in.m_channel->GetStreamName(in.m_stream);
	else
		return in.m_channel->GetDisplayName();
}

/**
	@brief Creates and names an input signal
 */
void FlowGraphNode::CreateInput(const string& name)
{
	m_signalNames.push_back(name);
	m_inputs.push_back(StreamDescriptor(NULL, 0));
}

bool FlowGraphNode::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cone tracing

/**
	@brief Determines if this node is downstream of any of the specified other nodes.

	Returns true if any of this node's inputs, or their inputs, etc. eventually chain back to an element in the set.
 */
bool FlowGraphNode::IsDownstreamOf(set<FlowGraphNode*> nodes)
{
	for(size_t i=0; i<m_inputs.size(); i++)
	{
		auto chan = m_inputs[i].m_channel;
		if(!chan)
			continue;
		if(nodes.find(chan) != nodes.end())
			return true;
		if(chan->IsDownstreamOf(nodes))
			return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

YAML::Node FlowGraphNode::SerializeConfiguration(IDTable& table)
{
	YAML::Node node;

	//Inputs
	YAML::Node inputs;
	for(size_t i=0; i<m_inputs.size(); i++)
	{
		auto desc = m_inputs[i];
		string value;
		if(desc.m_channel == nullptr)
			value = "0";
		else
			value = to_string(table.emplace(desc.m_channel)) + "/" + to_string(desc.m_stream);
		inputs[m_signalNames[i]] = value;
	}
	node["inputs"] = inputs;

	//Parameters
	YAML::Node parameters;
	for(auto it : m_parameters)
	{
		//Save both type and value for the parameter
		YAML::Node pnode;
		pnode["type"] = it.second.GetUnit().ToString();
		pnode["value"] = it.second.ToString(false, 7);
		parameters[it.first] = pnode;
	}
	node["parameters"] = parameters;

	return node;
}

void FlowGraphNode::LoadParameters(const YAML::Node& node, IDTable& /*table*/)
{
	auto parameters = node["parameters"];
	for(auto it : parameters)
	{
		auto& param = GetParameter(it.first.as<string>());

		//Older files (up to bb51fd93e3e460b98986622e7559d1c8602b327e) would store just the value,
		//which didn't handle dynamic parameters (type depends on runtime configuration) well.
		auto& pnode = it.second;
		if(pnode.IsScalar())
			param.ParseString(pnode.as<string>(), false);

		//Newer files explicitly store the type.
		else
		{
			param.SetUnit(Unit(pnode["type"].as<string>()));
			param.ParseString(pnode["value"].as<string>(), false);
		}
	}
}

void FlowGraphNode::LoadInputs(const YAML::Node& node, IDTable& table)
{
	int index;
	int stream;

	auto inputs = node["inputs"];
	for(auto it : inputs)
	{
		//Inputs are formatted as %d/%d. Stream index may be omitted.
		auto sin = it.second.as<string>();
		if(2 != sscanf(sin.c_str(), "%d/%d", &index, &stream))
		{
			index = atoi(sin.c_str());
			stream = 0;
		}

		SetInput(
			it.first.as<string>(),
			StreamDescriptor(table.Lookup<OscilloscopeChannel*>(index), stream),
			true
			);
	}
}
