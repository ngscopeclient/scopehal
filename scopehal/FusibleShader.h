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
	@brief Declaration of FusibleShader
	@ingroup core
 */
#ifndef FusibleShader_h
#define FusibleShader_h

class ShaderInputInfo
{
public:
	ShaderInputInfo(const std::string& t, const std::string& n)
		: m_type(t)
		, m_name(n)
	{}

	std::string m_type;
	std::string m_name;
};

class PushConstantInfo
{
public:
	PushConstantInfo(const std::string& t, const std::string& n)
		: m_type(t)
		, m_name(n)
	{}

	std::string m_type;
	std::string m_name;
};

/**
	@brief Data for a generic shader kernel that can be fused with others to form a complete compute pipeline
 */
class FusibleShader
{
public:
	FusibleShader(const std::string& shaderFile, const std::string& name)
		: m_glslSource(ReadDataFile(std::string("shaderfusion/") + shaderFile))
		, m_name(name)
	{}

	///@brief Inputs we need for this shader
	std::vector<ShaderInputInfo> m_inputs;

	///@brief Push constants we need for this shader
	std::vector<PushConstantInfo> m_pushConstants;

	///@brief Extensions we require
	std::set<std::string> m_extensions;

	///@brief GLSL source for the module
	std::string m_glslSource;

	///@brief Human readable name for the kernel
	std::string m_name;
};

#endif
