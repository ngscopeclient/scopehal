/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of DDR3Renderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "DDR3Renderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DDR3Renderer::DDR3Renderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color DDR3Renderer::GetColor(int i)
{
	DDR3Capture* capture = dynamic_cast<DDR3Capture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const DDR3Symbol& s = capture->m_samples[i].m_sample;

		switch(s.m_stype)
		{
			case DDR3Symbol::TYPE_MRS:
			case DDR3Symbol::TYPE_REF:
			case DDR3Symbol::TYPE_PRE:
			case DDR3Symbol::TYPE_PREA:
				return m_standardColors[COLOR_CONTROL];

			case DDR3Symbol::TYPE_ACT:
			case DDR3Symbol::TYPE_WR:
			case DDR3Symbol::TYPE_WRA:
			case DDR3Symbol::TYPE_RD:
			case DDR3Symbol::TYPE_RDA:
				return m_standardColors[COLOR_ADDRESS];

			case DDR3Symbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string DDR3Renderer::GetText(int i)
{
	DDR3Capture* capture = dynamic_cast<DDR3Capture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const DDR3Symbol& s = capture->m_samples[i].m_sample;

		switch(s.m_stype)
		{
			case DDR3Symbol::TYPE_MRS:
				return "MRS";

			case DDR3Symbol::TYPE_REF:
				return "REF";

			case DDR3Symbol::TYPE_PRE:
				return "PRE";

			case DDR3Symbol::TYPE_PREA:
				return "PREA";

			case DDR3Symbol::TYPE_ACT:
				return "ACT";

			case DDR3Symbol::TYPE_WR:
				return "WR";

			case DDR3Symbol::TYPE_WRA:
				return "WRA";

			case DDR3Symbol::TYPE_RD:
				return "RD";

			case DDR3Symbol::TYPE_RDA:
				return "RDA";

			case DDR3Symbol::TYPE_ERROR:
			default:
				return "ERR";
		}
	}
	return "";
}
