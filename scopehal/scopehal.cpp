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
	@brief Implementation of global functions
 */
#include "scopehal.h"
#include <gtkmm/drawingarea.h>

using namespace std;

string GetDefaultChannelColor(int i)
{
	const int NUM_COLORS = 12;
	static const char* colorTable[NUM_COLORS]=
	{
		"#ffa0a0",
		"#a0ffff",
		"#ffd0a0",
		"#a0d0ff",
		"#ffffa0",
		"#a0a0ff",
		"#ffa0d0",
		"#d0ffa0",
		"#d0a0ff",
		"#a0ffa0",
		"#ffa0ff",
		"#a0ffd0",
	};
	
	return colorTable[i % NUM_COLORS];
}

/**
	@brief Draws a string

	@param x X coordinate
	@param y Y position
	@param cr Cairo context
	@param str String to draw
	@param bBig Font size selector (small or large)
 */
void DrawString(float x, float y, const Cairo::RefPtr<Cairo::Context>& cr, string str, bool bBig)
{
	cr->save();

		Glib::RefPtr<Pango::Layout> tlayout = Pango::Layout::create (cr);
		cr->move_to(x, y);
		string desc = "sans normal 8";
		if(bBig)
			desc = "sans normal 10";
		Pango::FontDescription font(desc);
		font.set_weight(Pango::WEIGHT_NORMAL);
		tlayout->set_font_description(font);
		tlayout->set_text(str);
		tlayout->update_from_cairo_context(cr);
		tlayout->show_in_cairo_context(cr);

	cr->restore();
}

void GetStringWidth(const Cairo::RefPtr<Cairo::Context>& cr, std::string str, bool bBig, int& width, int& height)
{
	Glib::RefPtr<Pango::Layout> tlayout = Pango::Layout::create (cr);
	string desc = "sans normal 8";
	if(bBig)
		desc = "sans normal 10";
	Pango::FontDescription font(desc);
	font.set_weight(Pango::WEIGHT_NORMAL);
	tlayout->set_font_description(font);
	tlayout->set_text(str);

	tlayout->get_pixel_size(width, height);
}

/**
	@brief Converts a vector bus signal into a scalar (up to 64 bits wide)
 */
uint64_t ConvertVectorSignalToScalar(vector<bool> bits)
{
	uint64_t rval = 0;
	for(auto b : bits)
		rval = (rval << 1) | b;
	return rval;
}
