/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

string StreamDescriptor::GetName()
{
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
		if(c.m_channel != NULL)
			c.m_channel->Release();
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
		c.m_channel = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

/**
	@brief Notifies the node that a parameter has been changed (typically called by a GUI)

	@return True if the GUI should refresh the parameters, ports, etc, false if no refreshing is required.
 */
bool FlowGraphNode::OnParameterChanged(const string& /*name*/)
{
	return false;
}

FilterParameter& FlowGraphNode::GetParameter(string s)
{
	if(m_parameters.find(s) == m_parameters.end())
		LogError("Invalid parameter name\n");

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

		if(stream.m_channel == NULL)	//NULL is always legal
		{
			m_inputs[i] = StreamDescriptor(NULL, 0);
			return;
		}

		//If forcing, don't validate the channel
		if(!force)
		{
			if(!ValidateChannel(i, stream))
			{
				LogError("Invalid channel for input %zu of node\n", i);
				m_inputs[i] = StreamDescriptor(NULL, 0);
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
		stream.m_channel->AddRef();

		//Deref whatever was there (if anything)
		if(m_inputs[i].m_channel != NULL)
			m_inputs[i].m_channel->Release();

		//All good, we can save the new input
		m_inputs[i] = stream;
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
	LogError("Invalid channel name\n");
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string FlowGraphNode::SerializeConfiguration(IDTable& table, size_t indent)
{
	string sindent = "";
	for(size_t i=0; i<indent; i++)
		sindent += ' ';

	//Inputs
	char tmp[1024];
	string config = sindent + "inputs:\n";
	for(size_t i=0; i<m_inputs.size(); i++)
	{
		auto desc = m_inputs[i];
		if(desc.m_channel == NULL)
			snprintf(tmp, sizeof(tmp), "    %-20s 0\n", (m_signalNames[i] + ":").c_str());
		else
		{
			snprintf(tmp, sizeof(tmp), "    %-20s %d/%zu\n",
				(m_signalNames[i] + ":").c_str(),
				table.emplace(desc.m_channel),
				desc.m_stream
			);
		}
		config += sindent + tmp;
	}

	//Parameters
	config += sindent + "parameters:\n";
	for(auto it : m_parameters)
	{
		switch(it.second.GetType())
		{
			case FilterParameter::TYPE_FLOAT:
			case FilterParameter::TYPE_INT:
			case FilterParameter::TYPE_BOOL:
				snprintf(
					tmp,
					sizeof(tmp),
					"    %-20s %s\n", (it.first+":").c_str(), it.second.ToString().c_str());
				break;

			case FilterParameter::TYPE_FILENAME:
			case FilterParameter::TYPE_FILENAMES:
			default:
				snprintf(
					tmp,
					sizeof(tmp),
					"    %-20s \"%s\"\n", (it.first+":").c_str(), it.second.ToString().c_str());
				break;
		}

		config += sindent + tmp;
	}

	return config;
}

void FlowGraphNode::LoadParameters(const YAML::Node& node, IDTable& /*table*/)
{
	auto parameters = node["parameters"];
	for(auto it : parameters)
		GetParameter(it.first.as<string>()).ParseString(it.second.as<string>());
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
			StreamDescriptor(static_cast<OscilloscopeChannel*>(table[index]), stream),
			true
			);
	}
}
