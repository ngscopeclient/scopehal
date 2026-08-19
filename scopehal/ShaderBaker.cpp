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
	@brief Implementation of ShaderBaker
	@ingroup core
 */
#include "scopehal.h"
#include "ShaderBaker.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ShaderBaker::ShaderBaker()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual baking logic

string ShaderBaker::Bake()
{
	//Generate the output shader
	string ret;
	ret += "//Baked shader\n";
	ret += "#version 460\n";
	ret += "#pragma shader_stage(compute)\n";
	ret += "\n";

	//Extensions
	ret += "//Extensions\n";
	set<string> combinedExtensions;
	for(auto stage : m_stages)
	{
		for(auto ext : stage->m_shader->m_extensions)
			combinedExtensions.emplace(ext);
	}
	for(auto& ext : combinedExtensions)
		ret += string("#extension ") + ext + " : require\n";

	//SSBO inputs

	//The actual shader kernels
	for(size_t i=0; i<m_stages.size(); i++)
	{
		ret += "\n\n";

		ret += string("//BEGIN SHADER BLOCK ") + to_string(i) + "\n";

		string kernelName = string("shaderKernel_stage") + to_string(i);
		ret += str_replace("__kernel__", kernelName, m_stages[i]->m_shader->m_glslSource);

		ret += string("//END SHADER BLOCK ") + to_string(i) + "\n";
		ret += "\n\n";
	}

	return ret;
}
