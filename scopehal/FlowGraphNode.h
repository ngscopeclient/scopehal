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
	@brief Declaration of FlowGraphNode
	@ingroup core
 */
#ifndef FlowGraphNode_h
#define FlowGraphNode_h

class OscilloscopeChannel;
class WaveformBase;
class StreamDescriptor;

#include "FilterParameter.h"
#include "Waveform.h"
#include "Stream.h"
#include "SerializableObject.h"

/**
	@brief Abstract base class for a node in the signal flow graph.
	@ingroup core

	A FlowGraphNode has one or more channel inputs, zero or more configuration parameters.
 */
class FlowGraphNode : public SerializableObject
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

protected:
	virtual void OnInputChanged(size_t i);

	//Parameters
public:
	FilterParameter& GetParameter(std::string s);

	///@brief Short name for a map of strings to parameters
	typedef std::map<std::string, FilterParameter> ParameterMapType;

	/**
		@brief Checks if we have a parameter with a given name

		@param s	Name of the parameter

		@return		True if found, false if not found
	 */
	bool HasParameter(std::string s)
	{ return (m_parameters.find(s) != m_parameters.end()); }

	///@brief Returns an iterator to the beginning of our parameter map
	ParameterMapType::iterator GetParamBegin()
	{ return m_parameters.begin(); }

	///@brief Returns an iterator to the end of our parameter map
	ParameterMapType::iterator GetParamEnd()
	{ return m_parameters.end(); }

	///@brief Returns the number of parameter we have
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

	//Legacy prototype that isn't GPU capable
	[[deprecated]]
	virtual void Refresh();

	//Filter evaluation (GPU accelerated)
	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Error detection and reporting
public:

	///@brief Checks if this graph node reported any errors the last time it was refreshed
	bool HasErrors()
	{ return !m_errorTitle.empty(); }

	///@brief Returns the error log from this filter block, if any
	const std::string& GetErrorLog()
	{ return m_errorLog; }

	///@brief Returns the error message title from this filter block, if any
	const std::string& GetErrorTitle()
	{ return m_errorTitle; }

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

	void CreateInput(const std::string& name);

	std::string GetInputDisplayName(size_t i);

protected:
	///Names of signals we take as input
	std::vector<std::string> m_signalNames;

	///@brief The channel (if any) connected to each of our inputs
	std::vector<StreamDescriptor> m_inputs;

	/**
		@brief The nodes (if any) that each of our streams drives

		m_sinks[i] is the set of sinks for stream i
	 */
	std::vector< std::set<FlowGraphNode*> > m_sinks;

	//Parameters
	ParameterMapType m_parameters;

public:

	sigc::signal<void()> signal_parametersChanged()
	{ return m_parametersChangedSignal; }

	sigc::signal<void()> signal_inputsChanged()
	{ return m_inputsChangedSignal; }

protected:

	///@brief Remove any error messages left over from the last graph refresh
	void ClearErrors()
	{
		m_errorTitle = "";
		m_errorLog = "";
	}

	///@brief Add a new error message
	void AddErrorMessage(const std::string& err)
	{ m_errorLog += std::string("• ") + err + "\n"; }

	/**
		@brief Add a new error message with a title

		The title replaces any previous title, if set
	 */
	void AddErrorMessage(const std::string& title, const std::string& err)
	{
		m_errorTitle = title;
		AddErrorMessage(err);
	}

	///@brief Signal emitted when the set of parameters changes
	sigc::signal<void()> m_parametersChangedSignal;

	///@brief Signal emitted when the set of inputs changes
	sigc::signal<void()> m_inputsChangedSignal;

	///@brief Title / summary of error messages from the most recent filter refresh
	std::string m_errorTitle;

	///@brief Log of error messages from the most recent filter refresh
	std::string m_errorLog;
};

#endif
