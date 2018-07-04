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
	@brief Declaration of AsciiRenderer
 */

#include "scopehal.h"
#include "ChannelRenderer.h"
#include "TextRenderer.h"
#include "AsciiRenderer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction
AsciiRenderer::AsciiRenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

std::string AsciiRenderer::GetText(int i)
{
	AsciiCapture* capture = dynamic_cast<AsciiCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const AsciiSample& sample = capture->m_samples[i];
		char sbuf[16] = {0};
		if(isprint(sample.m_sample))
			sbuf[0] = sample.m_sample;
		else if(sample.m_sample == '\r')		//special case common non-printable chars
			return "\\r";
		else if(sample.m_sample == '\n')
			return "\\n";
		else if(sample.m_sample == '\b')
			return "\\b";
		else
			snprintf(sbuf, sizeof(sbuf), "\\x%02x", 0xFF & sample.m_sample);
		return sbuf;
	}
	return "";
}
