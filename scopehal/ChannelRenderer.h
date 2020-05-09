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
	@brief Declaration of ChannelRenderer
 */

#ifndef ChannelRenderer_h
#define ChannelRenderer_h

#include <cairomm/context.h>
#include <gtkmm/drawingarea.h>

class OscilloscopeChannel;

/**
	@brief Renders a single channel
 */
class ChannelRenderer
{
public:
	ChannelRenderer(OscilloscopeChannel* channel);
	virtual ~ChannelRenderer();

	int m_height;
	int m_ypos;

	int m_padding;
	int m_width;

	static void RenderComplexSignal(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int visleft, int visright,
		float xstart, float xend, float xoff,
		float ystart, float ymid, float ytop,
		std::string str,
		Gdk::Color color);

	static void MakePathSignalBody(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float xstart, float xoff, float xend, float ybot, float ymid, float ytop);

protected:
	/**
		@brief Standard colors for protocol decoder decode overlays.

		Do not change ordering, add new items to the end only.
	 */
	enum
	{
		COLOR_DATA,			//protocol data
		COLOR_CONTROL,		//generic control sequences
		COLOR_ADDRESS,		//addresses or device IDs
		COLOR_PREAMBLE,		//preambles, start bits, and other constant framing
		COLOR_CHECKSUM_OK,	//valid CRC/checksum
		COLOR_CHECKSUM_BAD,	//invalid CRC/checksum
		COLOR_ERROR,		//malformed traffic
		COLOR_IDLE,			//downtime between frames

		STANDARD_COLOR_COUNT
	} standard_color;

	static Gdk::Color m_standardColors[STANDARD_COLOR_COUNT];

protected:
	OscilloscopeChannel* m_channel;
};

#endif
