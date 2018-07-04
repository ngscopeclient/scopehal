/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of ProtocolDecoder
 */

#ifndef ProtocolDecoder_h
#define ProtocolDecoder_h

#include "OscilloscopeChannel.h"
#include "../scopehal/ChannelRenderer.h"

class ProtocolDecoderParameter
{
public:
	enum ParameterTypes
	{
		TYPE_FLOAT,
		TYPE_INT,
		TYPE_FILENAME
	};

	ProtocolDecoderParameter(ParameterTypes type = TYPE_INT);

	void ParseString(std::string str);
	std::string ToString();

	int GetIntVal();
	float GetFloatVal();
	std::string GetFileName();

	void SetIntVal(int i);
	void SetFloatVal(float f);
	void SetFileName(std::string f);

	ParameterTypes GetType()
	{ return m_type; }

protected:
	ParameterTypes m_type;

	int m_intval;
	float m_floatval;
	std::string m_filename;
};

/**
	@brief Abstract base class for all protocol decoders
 */
class ProtocolDecoder : public OscilloscopeChannel
{
public:
	ProtocolDecoder(std::string hwname, OscilloscopeChannel::ChannelType type, std::string color);
	virtual ~ProtocolDecoder();

	virtual void Refresh() =0;

	//Channels
	size_t GetInputCount();
	std::string GetInputName(size_t i);
	void SetInput(size_t i, OscilloscopeChannel* channel);
	void SetInput(std::string name, OscilloscopeChannel* channel);

	OscilloscopeChannel* GetInput(size_t i);

	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel) =0;

	ProtocolDecoderParameter& GetParameter(std::string s);
	typedef std::map<std::string, ProtocolDecoderParameter> ParameterMapType;
	ParameterMapType::iterator GetParamBegin()
	{ return m_parameters.begin(); }
	ParameterMapType::iterator GetParamEnd()
	{ return m_parameters.end(); }

	/**
		@brief Return true (default) if this decoder should be overlaid on top of the original waveform.

		Return false (override) if it should be rendered as its own line.
	 */
	virtual bool IsOverlay();

	virtual bool NeedsConfig() =0;	//false if we can automatically do the decode from the signal w/ no configuration

protected:

	///Names of signals we take as input
	std::vector<std::string> m_signalNames;

	//Parameters
	ParameterMapType m_parameters;

	///The channels corresponding to our signals
	std::vector<OscilloscopeChannel*> m_channels;

public:
	typedef ProtocolDecoder* (*CreateProcType)(std::string, std::string);
	static void AddDecoderClass(std::string name, CreateProcType proc);

	static void EnumProtocols(std::vector<std::string>& names);
	static ProtocolDecoder* CreateDecoder(std::string protocol, std::string hwname, std::string color);

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;
};

#define PROTOCOL_DECODER_INITPROC(T) \
	static ProtocolDecoder* CreateInstance(std::string hwname, std::string color) \
	{ \
		return new T(hwname, color); \
	}

#endif
