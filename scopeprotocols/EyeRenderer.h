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
	@brief Declaration of EyeRenderer
 */

#ifndef EyeRenderer_h
#define EyeRenderer_h

class ChannelRenderer;

/**
	@brief A pixel in an eye pattern
 */
class EyePatternPixel
{
public:
	float m_voltage;
	int64_t m_count;		//number of pixels with this value

	bool operator==(const EyePatternPixel& rhs) const
	{
		return (m_voltage == rhs.m_voltage) && (m_count == rhs.m_count);
	}
};

typedef OscilloscopeSample<EyePatternPixel> EyeSample;

class EyeCapture : public CaptureChannel<EyePatternPixel>
{
public:

	//Number of samples we've measured
	int64_t m_sampleCount;

	//Extrema of our waveform (highest/lowest voltage ever seen)
	float m_minVoltage;
	float m_maxVoltage;

	//Nominal levels of our signal.
	//Two entries for NRZ(I), three for MLT-3, four for PAM-4, etc
	std::vector<float> m_signalLevels;

	//Decision points (vertical center of each eye)
	//One entry for NRZ(I), two for MLT-3, three for PAM-4, etc
	std::vector<float> m_decisionPoints;

	//Width of the eye, in samples, at each decision point
	//For now, no BER tolerance (even a single point is enough to reduce the eye width)
	std::vector<int> m_eyeWidths;

	//Height of the eye at the center of each opening
	std::vector<float> m_eyeHeights;

	//Map of <code, code> -> rise/fall time (in samples)
	std::map< std::pair<int, int>, double > m_riseFallTimes;
};

/**
	@brief Renderer for an eye pattern
 */
class EyeRenderer : public ChannelRenderer
{
public:
	EyeRenderer(OscilloscopeChannel* channel);

	virtual void Render(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int width,
		int visleft,
		int visright,
		std::vector<time_range>& ranges);

protected:

	void RenderGrid(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float xmid,
		float plotleft,
		float plotright,
		float plotheight,
		float visright,
		float pixels_per_ui,
		float yzero,
		float yscale,
		float ytop,
		float ybot,
		float x_gridpitch,
		float y_grid,
		EyeCapture* capture);

	void RenderEyeOpenings(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float xmid,
		float yzero,
		float yscale,
		float ui_width,
		EyeCapture* capture);

	void RenderRiseFallTimes(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float plotwidth,
		float xmid,
		float yzero,
		float yscale,
		EyeCapture* capture);

	void RenderDecisionThresholds(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float yzero,
		float yscale,
		float plotleft,
		float plotright,
		EyeCapture* capture);

	void RenderEyeBitmap(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float plot_width,
		float plotleft,
		float plotheight,
		float yscale,
		float ytop,
		int64_t ui_width,
		int64_t& maxcount,
		float saturation,
		EyeCapture* capture);

	void RenderColorLegend(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int visleft,
		float ytop,
		float plotheight,
		int64_t maxcount,
		float saturation);

	void RenderLeftSideInfobox(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int visleft,
		float ytop,
		double ui_width,
		EyeCapture* capture);

	float VoltsToPixels(float v, float yzero, float yscale)
	{ return yzero + -v*yscale; }

	//unused
	virtual void RenderSampleCallback(
		const Cairo::RefPtr<Cairo::Context>& cr,
		size_t i,
		float xstart,
		float xend,
		int visleft,
		int visright);
};

#endif
