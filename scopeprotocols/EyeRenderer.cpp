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
	@brief Implementation of EyeRenderer
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/AnalogRenderer.h"
#include "../scopehal/ProtocolDecoder.h"
#include "EyeRenderer.h"
#include "EyeDecoder.h"
#include <gdkmm.h>
#include <gdkmm/pixbuf.h>

using namespace std;

struct RGBQUAD
{
	uint8_t rgbRed;
	uint8_t rgbGreen;
	uint8_t rgbBlue;
	uint8_t rgbAlpha;
};

static const RGBQUAD g_eyeColorScale[256] =
{
	{   0,   0,   0, 0   },     {   4,   2,  20, 255 },     {   7,   4,  35, 255 },     {   9,   5,  45, 255 },
    {  10,   6,  53, 255 },     {  11,   7,  60, 255 },     {  13,   7,  66, 255 },     {  14,   8,  71, 255 },
    {  14,   8,  75, 255 },     {  16,  10,  80, 255 },     {  16,  10,  85, 255 },     {  17,  10,  88, 255 },
    {  18,  11,  92, 255 },     {  19,  11,  95, 255 },     {  19,  12,  98, 255 },     {  20,  12, 102, 255 },
    {  20,  13, 104, 255 },     {  20,  13, 107, 255 },     {  21,  13, 110, 255 },     {  21,  13, 112, 255 },
    {  23,  14, 114, 255 },     {  23,  14, 117, 255 },     {  23,  14, 118, 255 },     {  23,  14, 121, 255 },
    {  23,  15, 122, 255 },     {  24,  15, 124, 255 },     {  24,  15, 126, 255 },     {  24,  14, 127, 255 },
    {  25,  15, 129, 255 },     {  25,  15, 130, 255 },     {  25,  16, 131, 255 },     {  26,  16, 132, 255 },
    {  26,  15, 134, 255 },     {  27,  16, 136, 255 },     {  26,  16, 136, 255 },     {  26,  16, 137, 255 },
    {  27,  16, 138, 255 },     {  26,  16, 138, 255 },     {  26,  16, 140, 255 },     {  27,  16, 141, 255 },
    {  27,  16, 141, 255 },     {  28,  17, 142, 255 },     {  27,  17, 142, 255 },     {  27,  16, 143, 255 },
    {  28,  17, 144, 255 },     {  28,  17, 144, 255 },     {  28,  17, 144, 255 },     {  28,  17, 144, 255 },
    {  28,  17, 144, 255 },     {  28,  17, 145, 255 },     {  28,  17, 145, 255 },     {  28,  17, 145, 255 },
    {  28,  17, 145, 255 },     {  30,  17, 144, 255 },     {  32,  17, 143, 255 },     {  34,  17, 142, 255 },
    {  35,  16, 140, 255 },     {  37,  17, 139, 255 },     {  38,  16, 138, 255 },     {  40,  17, 136, 255 },
    {  42,  16, 136, 255 },     {  44,  16, 134, 255 },     {  46,  17, 133, 255 },     {  47,  16, 133, 255 },
    {  49,  16, 131, 255 },     {  51,  16, 130, 255 },     {  53,  17, 129, 255 },     {  54,  16, 128, 255 },
    {  56,  16, 127, 255 },     {  58,  16, 126, 255 },     {  60,  16, 125, 255 },     {  62,  16, 123, 255 },
    {  63,  16, 122, 255 },     {  65,  16, 121, 255 },     {  67,  16, 120, 255 },     {  69,  16, 119, 255 },
    {  70,  16, 117, 255 },     {  72,  16, 116, 255 },     {  74,  16, 115, 255 },     {  75,  15, 114, 255 },
    {  78,  16, 113, 255 },     {  79,  16, 112, 255 },     {  81,  16, 110, 255 },     {  83,  15, 110, 255 },
    {  84,  15, 108, 255 },     {  86,  16, 108, 255 },     {  88,  15, 106, 255 },     {  90,  15, 105, 255 },
    {  91,  16, 103, 255 },     {  93,  15, 103, 255 },     {  95,  15, 102, 255 },     {  96,  15, 100, 255 },
    {  98,  15, 100, 255 },     { 100,  15,  98, 255 },     { 101,  15,  97, 255 },     { 104,  15,  96, 255 },
    { 106,  15,  95, 255 },     { 107,  15,  94, 255 },     { 109,  14,  92, 255 },     { 111,  14,  92, 255 },
    { 112,  15,  90, 255 },     { 114,  14,  89, 255 },     { 116,  15,  87, 255 },     { 118,  14,  87, 255 },
    { 119,  14,  86, 255 },     { 121,  14,  85, 255 },     { 123,  14,  83, 255 },     { 124,  14,  83, 255 },
    { 126,  15,  81, 255 },     { 128,  14,  80, 255 },     { 130,  14,  78, 255 },     { 132,  14,  77, 255 },
    { 134,  14,  76, 255 },     { 137,  14,  74, 255 },     { 139,  14,  73, 255 },     { 141,  14,  71, 255 },
    { 143,  13,  70, 255 },     { 146,  13,  68, 255 },     { 148,  14,  67, 255 },     { 150,  13,  65, 255 },
    { 153,  14,  64, 255 },     { 155,  14,  62, 255 },     { 157,  13,  61, 255 },     { 159,  13,  60, 255 },
    { 162,  13,  58, 255 },     { 165,  13,  56, 255 },     { 166,  13,  55, 255 },     { 169,  13,  54, 255 },
    { 171,  13,  52, 255 },     { 173,  13,  50, 255 },     { 176,  13,  48, 255 },     { 179,  12,  47, 255 },
    { 181,  12,  45, 255 },     { 183,  12,  45, 255 },     { 185,  12,  43, 255 },     { 188,  13,  41, 255 },
    { 190,  12,  40, 255 },     { 192,  12,  38, 255 },     { 194,  13,  37, 255 },     { 197,  12,  35, 255 },
    { 199,  12,  33, 255 },     { 201,  12,  32, 255 },     { 204,  12,  30, 255 },     { 206,  12,  29, 255 },
    { 209,  12,  28, 255 },     { 211,  12,  26, 255 },     { 213,  12,  25, 255 },     { 216,  12,  23, 255 },
    { 218,  11,  22, 255 },     { 221,  12,  20, 255 },     { 223,  11,  18, 255 },     { 224,  11,  17, 255 },
    { 227,  11,  16, 255 },     { 230,  11,  14, 255 },     { 231,  11,  12, 255 },     { 234,  12,  11, 255 },
    { 235,  13,  10, 255 },     { 235,  15,  11, 255 },     { 235,  17,  11, 255 },     { 235,  19,  11, 255 },
    { 236,  21,  10, 255 },     { 236,  23,  10, 255 },     { 237,  24,  10, 255 },     { 237,  26,  10, 255 },
    { 236,  28,   9, 255 },     { 237,  30,  10, 255 },     { 237,  32,   9, 255 },     { 238,  34,   9, 255 },
    { 238,  35,   9, 255 },     { 238,  38,   8, 255 },     { 239,  39,   9, 255 },     { 239,  42,   8, 255 },
    { 240,  44,   9, 255 },     { 240,  45,   8, 255 },     { 240,  47,   8, 255 },     { 240,  49,   8, 255 },
    { 241,  51,   7, 255 },     { 241,  53,   8, 255 },     { 241,  55,   7, 255 },     { 241,  57,   7, 255 },
    { 242,  58,   7, 255 },     { 242,  60,   7, 255 },     { 242,  62,   6, 255 },     { 243,  64,   6, 255 },
    { 244,  66,   6, 255 },     { 243,  68,   5, 255 },     { 244,  69,   6, 255 },     { 244,  71,   6, 255 },
    { 245,  74,   6, 255 },     { 245,  76,   5, 255 },     { 245,  79,   5, 255 },     { 246,  82,   5, 255 },
    { 246,  85,   5, 255 },     { 247,  87,   4, 255 },     { 247,  90,   4, 255 },     { 248,  93,   3, 255 },
    { 249,  96,   4, 255 },     { 248,  99,   3, 255 },     { 249, 102,   3, 255 },     { 250, 105,   3, 255 },
    { 250, 107,   2, 255 },     { 250, 110,   2, 255 },     { 251, 113,   2, 255 },     { 252, 115,   1, 255 },
    { 252, 118,   2, 255 },     { 253, 121,   1, 255 },     { 253, 124,   1, 255 },     { 253, 126,   1, 255 },
    { 254, 129,   0, 255 },     { 255, 132,   0, 255 },     { 255, 135,   0, 255 },     { 255, 138,   1, 255 },
    { 254, 142,   3, 255 },     { 253, 145,   4, 255 },     { 253, 148,   6, 255 },     { 252, 151,   9, 255 },
    { 252, 155,  11, 255 },     { 251, 158,  12, 255 },     { 251, 161,  14, 255 },     { 250, 163,  15, 255 },
    { 251, 165,  16, 255 },     { 250, 167,  17, 255 },     { 250, 169,  18, 255 },     { 250, 170,  19, 255 },
    { 250, 172,  20, 255 },     { 249, 174,  21, 255 },     { 249, 177,  22, 255 },     { 248, 178,  23, 255 },
    { 248, 180,  24, 255 },     { 247, 182,  25, 255 },     { 247, 184,  26, 255 },     { 247, 185,  27, 255 },
    { 247, 188,  27, 255 },     { 247, 191,  26, 255 },     { 248, 194,  25, 255 },     { 249, 197,  24, 255 },
    { 248, 200,  22, 255 },     { 249, 203,  21, 255 },     { 249, 205,  20, 255 },     { 250, 209,  18, 255 },
    { 250, 212,  18, 255 },     { 250, 214,  16, 255 },     { 251, 217,  15, 255 },     { 251, 221,  14, 255 },
    { 251, 223,  13, 255 },     { 251, 226,  12, 255 },     { 252, 229,  11, 255 },     { 253, 231,   9, 255 },
    { 253, 234,   9, 255 },     { 253, 237,   7, 255 },     { 253, 240,   6, 255 },     { 253, 243,   5, 255 },
    { 254, 246,   4, 255 },     { 254, 248,   3, 255 },     { 255, 251,   1, 255 },     { 255, 254,   1, 255 }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction
EyeRenderer::EyeRenderer(OscilloscopeChannel* channel)
: ChannelRenderer(channel)
{
	m_height = 384;
	m_padding = 25;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

void EyeRenderer::Render(
	const Cairo::RefPtr<Cairo::Context>& cr,
	int width,
	int visleft,
	int visright,
	vector<time_range>& ranges)
{
	float ytop = m_ypos + m_padding;
	float ybot = m_ypos + m_height - m_padding;
	float plotheight = m_height - 2*m_padding;
	float halfheight = plotheight/2;
	float ymid = halfheight + ytop;
	float xmid = (visright - visleft)/2 + visleft;

	float x_padding = 165;
	float plot_width = (visright - visleft) - 2*x_padding;
	float plotleft = xmid - plot_width/2;
	float plotright = xmid + plot_width/2;

	//Shift a bit so we're close to the voltage scale at right.
	//This will leave space at our left side for info text.
	float rshift = 90;
	plotleft += rshift;
	plotright += rshift;
	xmid += rshift;

	RenderStartCallback(cr, width, visleft, visright, ranges);
	cr->save();

	EyeDecoder* channel = dynamic_cast<EyeDecoder*>(m_channel);
	EyeCapture* capture = dynamic_cast<EyeCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		//Save time scales
		int64_t ui_width = channel->GetUIWidth();
		float pixels_per_ui = plot_width / 2;

		//Calculate how high our waveform is
		float waveheight = capture->m_maxVoltage - capture->m_minVoltage;
		float yscale = plotheight / waveheight;

		//Align midpoint of waveform to midpoint of our plot
		float yoffset = ((waveheight/2) + capture->m_minVoltage);

		//Decide what size divisions to use
		float y_grid = AnalogRenderer::PickStepSize(waveheight/2, 3, 5);

		//Draw the grid and axis labels
		float yzero = ymid + yscale*yoffset;
		float x_gridpitch = 0.125;	//in UIs
		RenderGrid(
			cr,
			xmid,
			plotleft,
			plotright,
			plotheight,
			visright,
			pixels_per_ui,
			yzero,
			yscale,
			ytop,
			ybot,
			x_gridpitch,
			y_grid,
			capture);

		//Draw the decision thresholds
		RenderDecisionThresholds(cr, yzero, yscale, plotleft, plotright, capture);

		//Draw the actual eye pattern
		int64_t maxcount = 0;
		float saturation = 0.4;
		RenderEyeBitmap(cr, plot_width, plotleft, plotheight, yscale, ytop, ui_width, maxcount, saturation, capture);

		//Draw the color ramp at the left of the display
		RenderColorLegend(cr, visleft, ytop, plotheight, maxcount, saturation);

		//Draw text info at the left of the display
		RenderLeftSideInfobox(cr, visleft, ytop, channel->GetUIWidthFractional(), capture);

		//Draw eye opening info at each decision point
		RenderEyeOpenings(cr, xmid, yzero, yscale, ui_width, capture);

		//Draw labels on rising/falling edges
		RenderRiseFallTimes(cr, plot_width, xmid, yzero, yscale, capture);
	}

	cr->restore();
	RenderEndCallback(cr, width, visleft, visright, ranges);
}

/**
	@brief Draw the rise/fall time values
 */
void EyeRenderer::RenderRiseFallTimes(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float plotwidth,
		float xmid,
		float yzero,
		float yscale,
		EyeCapture* capture)
{
	for(auto it : capture->m_riseFallTimes)
	{
		//Look up the original voltage levels
		float startingVoltage = capture->m_signalLevels[it.first.first];
		float endingVoltage = capture->m_signalLevels[it.first.second];
		bool rising = startingVoltage < endingVoltage;

		//Figure out where we're drawing vertically (midpoint of the transition)
		float vmid = startingVoltage + (endingVoltage - startingVoltage)/2;
		float y = yzero - yscale*vmid;

		//Figure out where we're drawing horizontally (edge of the eye)
		//TODO: pick this better
		float x;
		if(rising)
			x = xmid - plotwidth/4;
		else
			x = xmid + plotwidth/4;

		//Format
		char tmp[128];
		if(rising)
			snprintf(tmp, sizeof(tmp), "Rise (10-90%%): %.2f ns", it.second * capture->m_timescale * 1e-3);
		else
			snprintf(tmp, sizeof(tmp), "Fall (90-10%%): %.2f ns", it.second * capture->m_timescale * 1e-3);

		//and draw
		int swidth;
		int sheight;
		GetStringWidth(cr, tmp, false, swidth, sheight);
		x -= swidth/2;
		y -= sheight/2;

		cr->set_source_rgba(0, 0, 0, 0.75);
		cr->rectangle(x, y, swidth, sheight);
		cr->fill();

		cr->set_source_rgba(1, 1, 1, 1);
		DrawString(x , y, cr, tmp, false);
	}
}

/**
	@brief Draw the labels for the eye opening markings
 */
void EyeRenderer::RenderEyeOpenings(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float xmid,
		float yzero,
		float yscale,
		float ui_width,
		EyeCapture* capture)
{
	int swidth;
	int sheight;
	char str[512];
	for(size_t i=0; i < capture->m_eyeWidths.size(); i++)
	{
		float v = capture->m_decisionPoints[i];
		int width = capture->m_eyeWidths[i];

		float width_ui = width * 1.0f / ui_width;
		float width_ns = width * 1e-3f * capture->m_timescale;

		snprintf(str, sizeof(str), "W = %.2f UI / %.3f ns\nH = %.1f mV",
			width_ui,
			width_ns,
			capture->m_eyeHeights[i]*1000);
		GetStringWidth(cr, str, false, swidth, sheight);

		float x = xmid - swidth/2;
		float y = VoltsToPixels(v, yzero, yscale) - sheight/2;

		cr->set_source_rgba(0, 0, 0, 0.75);
		cr->rectangle(x, y, swidth, sheight);
		cr->fill();

		cr->set_source_rgba(1, 1, 1, 1);
		DrawString(x, y, cr, str, false);
	}
}

/**
	@brief Draw the cyan lines for the threshold levels
 */
void EyeRenderer::RenderDecisionThresholds(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float yzero,
		float yscale,
		float plotleft,
		float plotright,
		EyeCapture* capture)
{
	char str[256];
	int swidth;
	int sheight;
	for(auto v : capture->m_decisionPoints)
	{
		float y = yzero - v*yscale;

		//Draw the line
		cr->set_source_rgba(0.0, 1.0, 1.0, 1.0);
		cr->move_to(plotleft, y);
		cr->line_to(plotright, y);
		cr->stroke();

		//Draw the label
		snprintf(str, sizeof(str), "%.1f mV", v*1000);
		GetStringWidth(cr, str, false, swidth, sheight);

		float tx = plotright - swidth;
		float ty = y - sheight/2;

		cr->set_source_rgba(0, 0, 0, 0.75);
		cr->rectangle(tx, ty, swidth, sheight);
		cr->fill();

		cr->set_source_rgba(1, 1, 1, 1);
		DrawString(tx, ty, cr, str, false);
	}
}

/**
	@brief Draw the main bitmap of the eye diagram
 */
void EyeRenderer::RenderEyeBitmap(
		const Cairo::RefPtr<Cairo::Context>& cr,
		float plot_width,
		float plotleft,
		float plotheight,
		float yscale,
		float ytop,
		int64_t ui_width,
		int64_t& maxcount,
		float saturation,
		EyeCapture* capture)
{
	int row_width = ui_width*2;

	if(row_width > 32768)
	{
		LogWarning("Excessive oversampling. Cairo cannot render bitmaps more than 32768 pixels across.\n");
		return;
	}

	//Create pixel value histogram
	int pixel_count = ui_width * m_height;
	int64_t* histogram = new int64_t[pixel_count];
	for(int i=0; i<pixel_count; i++)
		histogram[i] = 0;

	//Compute the histogram
	for(size_t i=0; i<capture->GetDepth(); i++)
	{
		int64_t tstart = capture->GetSampleStart(i);
		auto sample = (*capture)[i];

		int ystart = yscale * (capture->m_maxVoltage - sample.m_voltage);	//vertical flip
		if(ystart >= m_height)
			ystart = m_height-1;
		if(ystart < 0)
			ystart = 0;

		int64_t& pix = histogram[tstart + ystart*ui_width];
		pix += sample.m_count;
		if(pix > maxcount)
			maxcount = pix;
	}
	if(maxcount == 0)
	{
		LogError("No pixels\n");
		delete[] histogram;
		return;
	}

	//Scale things to that we get a better coverage of the color range
	float cmax = maxcount * saturation;

	//Convert to RGB values
	RGBQUAD* pixels = new RGBQUAD[pixel_count * 4];
	for(int y=0; y<plotheight; y++)
	{
		for(int x=-ui_width/2; x<=ui_width/2; x++)
		{
			int x_rotated = x;
			if(x < 0)
				x_rotated += ui_width;

			int npix = (int)ceil((255.0f * histogram[y*ui_width + x_rotated]) / cmax);
			if(npix > 255)
				npix = 255;

			pixels[y*row_width + x + ui_width/2]	= g_eyeColorScale[npix];
			pixels[y*row_width + x + ui_width*3/2]	= g_eyeColorScale[npix];
		}
	}

	//Fill empty rows with the row above
	for(int y=1; y<plotheight; y++)
	{
		bool empty = true;
		for(int x=0; x<ui_width; x++)
		{
			if(histogram[y*ui_width + x] != 0)
			{
				empty = false;
				break;
			}
		}

		if(empty)
			memcpy(pixels + y*row_width, pixels + (y-1)*row_width, row_width*sizeof(RGBQUAD));
	}

	//Create the actual pixmap
	Glib::RefPtr< Gdk::Pixbuf > pixbuf = Gdk::Pixbuf::create_from_data(
		reinterpret_cast<unsigned char*>(pixels),
		Gdk::COLORSPACE_RGB,
		true,
		8,
		row_width,
		plotheight,
		row_width * 4);
	Cairo::RefPtr< Cairo::ImageSurface > surface =
		Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, row_width, plotheight);
	Cairo::RefPtr< Cairo::Context > context = Cairo::Context::create(surface);
	Gdk::Cairo::set_source_pixbuf(context, pixbuf, 0.0, 0.0);
	context->paint();

	//Render the bitmap over our background and grid
	cr->save();
		cr->begin_new_path();
		cr->translate(plotleft, ytop);
		cr->scale(plot_width / row_width, 1);
		cr->set_source(surface, 0.0, 0.0);
		cr->rectangle(0, 0, row_width, plotheight);
		cr->clip();
		cr->paint();
	cr->restore();

	delete[] pixels;
	delete[] histogram;
}

/**
	@brief Draws the grid on the background of the plot
 */
void EyeRenderer::RenderGrid(
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
		EyeCapture* capture)
{
	//Solid center lines
	cr->set_source_rgba(0.7, 0.7, 0.7, 1.0);
	if( (0 >= capture->m_minVoltage) && (0 <= capture->m_maxVoltage) )
	{
		cr->move_to(plotleft, yzero);
		cr->line_to(plotright, yzero);
	}
	cr->move_to(xmid, ybot);
	cr->line_to(xmid, ytop);
	cr->stroke();

	//Dotted lines above and below center
	vector<double> dashes;
	dashes.push_back(2);
	dashes.push_back(2);
	cr->set_dash(dashes, 0);

		map<float, float> gridmap;
		if( (0 >= capture->m_minVoltage) && (0 <= capture->m_maxVoltage) )
			gridmap[0] = yzero;
		for(float dv=y_grid; ; dv += y_grid)
		{
			float ypos = VoltsToPixels(dv, yzero, yscale);
			float yneg = VoltsToPixels(-dv, yzero, yscale);

			if(ypos >= ytop)
			{
				gridmap[dv] = ypos;
				cr->move_to(plotleft, ypos);
				cr->line_to(plotright + 15, ypos);
			}

			if(yneg <= ybot)
			{
				gridmap[-dv] = yneg;
				cr->move_to(plotleft, yneg);
				cr->line_to(plotright + 15, yneg);
			}

			if( (dv > fabs(capture->m_maxVoltage)) && (dv > fabs(capture->m_minVoltage)) )
				break;
		}

		//and left/right of center
		for(float dt = 0; dt < 1.1; dt += x_gridpitch)
		{
			float dx = dt * pixels_per_ui;

			cr->move_to(xmid - dx, ybot);
			cr->line_to(xmid - dx, ytop);

			cr->move_to(xmid + dx, ybot);
			cr->line_to(xmid + dx, ytop);
		}

		cr->stroke();

	cr->unset_dash();

	//Draw text for the X axis labels
	char tmp[32];
	for(float dt = 0; dt < 1.1; dt += x_gridpitch * 2)
	{
		float dx = dt * pixels_per_ui;

		cr->move_to(xmid - dx, ybot);
		cr->line_to(xmid - dx, ybot + 20);

		cr->move_to(xmid + dx, ybot);
		cr->line_to(xmid + dx, ybot + 20);

		cr->set_source_rgba(0.7, 0.7, 0.7, 1.0);
		cr->set_dash(dashes, 0);
		cr->stroke();
		cr->unset_dash();

		cr->set_source_rgba(1.0, 1.0, 1.0, 1.0);
		snprintf(tmp, sizeof(tmp), "%.2f UI", dt);
		DrawString(xmid + dx + 5, ybot + 5, cr, tmp, false);

		if(dt != 0)
		{
			snprintf(tmp, sizeof(tmp), "%.2f UI", -dt);
			DrawString(xmid - dx + 5, ybot + 5, cr, tmp, false);
		}
	}

	//Draw text for the Y axis labels
	AnalogRenderer::DrawVerticalAxisLabels(cr, visright, ytop, plotheight, gridmap);
}

/**
	@brief Draws the text at the left side of the plot with eye metadata
 */
void EyeRenderer::RenderLeftSideInfobox(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int visleft,
		float ytop,
		double ui_width,
		EyeCapture* capture)
{
	float y = ytop;
	int swidth;
	int sheight;
	char str[512];

	//Text positioning
	float textleft = visleft + 100;
	float numleft = textleft + 75;
	float rowspacing = 2;

	//Number of points in the capture
	string label = "Points:";
	snprintf(str, sizeof(str), "%ld", capture->m_sampleCount);
	GetStringWidth(cr, label, false, swidth, sheight);
	DrawString(textleft, y, cr, label, false);
	DrawString(numleft, y, cr, str, false);
	y += sheight + rowspacing;

	//Sample rate
	label = "Timebase:";
	snprintf(str, sizeof(str), "%.1f GS/s", 1e3f / capture->m_timescale);
	GetStringWidth(cr, label, false, swidth, sheight);
	DrawString(textleft, y, cr, label, false);
	DrawString(numleft, y, cr, str, false);
	y += sheight + rowspacing;

	//Modulation
	label = "Modulation:";
	snprintf(str, sizeof(str), "%d levels", (int)capture->m_signalLevels.size());
	GetStringWidth(cr, label, false, swidth, sheight);
	DrawString(textleft, y, cr, label, false);
	DrawString(numleft, y, cr, str, false);
	y += sheight + rowspacing;

	//Voltage levels (right aligned)
	for(int i = capture->m_signalLevels.size()-1; i >= 0; i--)
	{
		float v = capture->m_signalLevels[i];

		snprintf(str, sizeof(str), "%6.1f mV", v*1000);
		GetStringWidth(cr, str, false, swidth, sheight);
		DrawString(numleft + 70 - swidth, y, cr, str, false);
		y += sheight + rowspacing;
	}

	//UI width
	label = "UI width:";
	snprintf(str, sizeof(str), "%.3f ns", ui_width * 1e-3 * capture->m_timescale);
	GetStringWidth(cr, label, false, swidth, sheight);
	DrawString(textleft, y, cr, label, false);
	DrawString(numleft, y, cr, str, false);
	y += sheight + rowspacing;

	//Symbol rate
	label = "Symbol rate:";
	snprintf(str, sizeof(str), "%.3f Mbd", 1e6 / (ui_width * capture->m_timescale));
	GetStringWidth(cr, label, false, swidth, sheight);
	DrawString(textleft, y, cr, label, false);
	DrawString(numleft, y, cr, str, false);
	y += sheight + rowspacing;
}

/**
	@brief Draws the color ramp scale at the left side of the plot
 */
void EyeRenderer::RenderColorLegend(
		const Cairo::RefPtr<Cairo::Context>& cr,
		int visleft,
		float ytop,
		float plotheight,
		int64_t maxcount,
		float saturation)
{
	//Create the pixmap
	Glib::RefPtr< Gdk::Pixbuf > ramp_pixbuf = Gdk::Pixbuf::create_from_data(
		reinterpret_cast<const unsigned char*>(g_eyeColorScale),
		Gdk::COLORSPACE_RGB,
		true,
		8,
		1,
		256,
		4);
	Cairo::RefPtr< Cairo::ImageSurface > ramp_surface =
		Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 1, 256);
	Cairo::RefPtr< Cairo::Context > ramp_context = Cairo::Context::create(ramp_surface);
	Gdk::Cairo::set_source_pixbuf(ramp_context, ramp_pixbuf, 0.0, 0.0);
	ramp_context->paint();

	//Render the bitmap
	for(int i=0; i<20; i++)
	{
		cr->save();
			cr->begin_new_path();
			cr->translate(visleft + 10 + i, ytop + plotheight);
			cr->scale(1, -plotheight / 256);
			cr->set_source(ramp_surface, 0.0, 0.0);
			cr->rectangle(0, 0, 1, 256);
			cr->clip();
			cr->paint();
		cr->restore();
	}

	//and the text labels
	map<float, float> legendmap;
	for(float f=0; f<=1.1; f+= 0.125)
		legendmap[maxcount*f*saturation] = ytop + plotheight*(1 - f) + 10;
	AnalogRenderer::DrawVerticalAxisLabels(cr, visleft + 95, ytop, plotheight, legendmap, false);
}

void EyeRenderer::RenderSampleCallback(
	const Cairo::RefPtr<Cairo::Context>& /*cr*/,
	size_t /*i*/,
	float /*xstart*/,
	float /*xend*/,
	int /*visleft*/,
	int /*visright*/)
{
	//Unused, but we have to override it
}
