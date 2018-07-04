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
	@brief Declaration of AnalogRenderer
 */

#ifndef AnalogRenderer_h
#define AnalogRenderer_h

class ChannelRenderer;

/**
	@brief Renderer for an analog channel
 */
class AnalogRenderer : public ChannelRenderer
{
public:
	AnalogRenderer(OscilloscopeChannel* channel);

	virtual void RenderStartCallback(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int width,
		int visleft,
		int visright,
		std::vector<time_range>& ranges);

	virtual void RenderSampleCallback(
		const Cairo::RefPtr<Cairo::Context>& cr,
		size_t i,
		float xstart,
		float xend,
		int visleft,
		int visright);

	virtual void RenderEndCallback(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int width,
		int visleft,
		int visright,
		std::vector<time_range>& ranges);

	float m_yscale;
	float m_yoffset;

protected:
	std::map<float, float> m_gridmap;
	float pixels_to_volts(float p, bool offset = true);
	float volts_to_pixels(float v, bool offset = true);

public:
	static float PickStepSize(float volts_per_half_span, int min_steps = 2, int max_steps = 4);

	static void DrawVerticalAxisLabels(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int visright,
		float ytop,
		float plotheight,
		std::map<float, float>& gridmap,
		bool show_units = true);
};

#endif
