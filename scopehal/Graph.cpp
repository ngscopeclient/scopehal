/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2018 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of Graph
 */

#include "scopehal.h"
#include "Graph.h"
#include <cairomm/context.h>

using namespace std;

void DrawStringVertical(float x, float y, const Cairo::RefPtr<Cairo::Context>& cr, string str, bool bBig);
void GetStringWidth(const Cairo::RefPtr<Cairo::Context>& cr, std::string str, bool bBig, int& width, int& height);

Series* Graphable::GetSeries(std::string name)
{
	if(m_series.find(name) == m_series.end())
		m_series[name] = new Series;
	return m_series[name];
}

bool Graphable::visible()
{
	return true;
}

Graphable::~Graphable()
{
	for(auto x : m_series)
		delete x.second;
	m_series.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Graph::Graph()
: m_lmargin(70)
, m_rmargin(20)
, m_tmargin(10)
, m_bmargin(20)
{
	//Set our timer
	sigc::slot<bool> slot = sigc::bind(sigc::mem_fun(*this, &Graph::OnTimer), 1);
	sigc::connection conn = Glib::signal_timeout().connect(slot, 100);

	m_minScale = 0;
	m_maxScale = 100;
	m_scaleBump = 10;
	m_units = "%";
	m_unitScale = 1;

	//Redlines default to off scale
	m_minRedline = -1;
	m_maxRedline = 101;
}

Graph::~Graph()
{
}

bool Graph::on_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
	Glib::RefPtr<Gdk::Window> window = get_bin_window();
	if(window)
	{
		//Get dimensions
		Gtk::Allocation allocation = get_allocation();
		m_width = allocation.get_width();
		m_height = allocation.get_height();

		//Grab time
		m_now = GetTime();

		cr->save();

			//Calculate dimensions
			m_bottom = m_height - m_bmargin;
			m_top = m_tmargin;
			m_left = m_lmargin;
			m_right = m_width - m_rmargin;
			m_bodywidth = m_right - m_left;
			m_bodyheight = m_bottom - m_top;
			m_pheight = m_bodyheight / (m_maxScale - m_minScale);

			//Calculate size of legend
			int legendvspace = 5;
			int lineheight = 0;
			int legendw = 0;
			int legendh = 0;
			for(size_t i=0; i<m_series.size(); i++)
			{
				int w, h;
				GetStringWidth(cr, m_series[i]->m_name, false, w, h);
				if(w > legendw)
					legendw = w;
				lineheight = legendvspace + h;
				legendh += lineheight;
			}

			//Clip to window area
			cr->rectangle(0, 0, m_width, m_height);
			cr->clip();

			//Fill background
			cr->set_source_rgb(1.0, 1.0, 1.0);
			cr->rectangle(m_left, m_top, m_bodywidth, m_bodyheight);
			cr->fill();

			//Draw red lines for limits
			cr->set_source_rgb(1, 0.8, 0.8);
			if(m_minRedline > m_minScale)
			{
				cr->move_to(m_left, valueToPosition(m_minRedline));
				cr->line_to(m_right, valueToPosition(m_minRedline));
				cr->line_to(m_right, m_bottom);
				cr->line_to(m_left, m_bottom);
				cr->fill();
			}
			if(m_maxRedline < m_maxScale)
			{
				cr->move_to(m_left, valueToPosition(m_maxRedline));
				cr->line_to(m_right, valueToPosition(m_maxRedline));
				cr->line_to(m_right, m_top);
				cr->line_to(m_left, m_top);
				cr->fill();
			}

			//Draw axes
			cr->set_line_width(1.0);
			cr->set_source_rgb(0, 0, 0);
			cr->move_to(m_left + 0.5, m_top);
			cr->line_to(m_left + 0.5, m_bottom + 0.5);
			cr->line_to(m_right + 0.5, m_bottom + 0.5);
			cr->stroke();

			//Draw grid
			vector<double> dashes;
			dashes.push_back(1);
			float pos = m_right;
			for(int dt=0; ; dt += 10)	//Vertical grid lines
			{
				//Get current position
				pos = timeToPosition(m_now - dt);
				if(pos <= m_left)
					break;

				//Draw line
				int ipos = static_cast<int>(pos);
				cr->set_dash(dashes, 0);
				cr->set_line_width(0.5);
				cr->move_to(ipos + 0.5, m_bottom + 0.5);
				cr->line_to(pos + 0.5, m_top);
				cr->stroke();
				cr->unset_dash();

				//Draw text
				char buf[32];
				sprintf(buf, "%d:%02d", dt / 60, dt % 60);
				cr->set_line_width(1.0);
				DrawString(pos - 20, m_bottom + 5, cr, buf, false);
			}
			for(float i=m_scaleBump; i<=m_maxScale; i += m_scaleBump)		//Horizontal grid lines
			{
				//Get current position
				float pos = valueToPosition(i);

				//Draw line
				int ipos = static_cast<int>(pos);
				cr->set_dash(dashes, 0);
				cr->set_line_width(0.5);
				cr->move_to(m_left, ipos + 0.5);
				cr->line_to(m_right, ipos + 0.5);
				cr->stroke();
				cr->stroke();
				cr->unset_dash();

				//Draw text
				char buf[32];
				sprintf(buf, "%.0f %s", i * m_unitScale, m_units.c_str());
				if(m_unitScale < 0.1)
					sprintf(buf, "%.1f %s", i * m_unitScale, m_units.c_str());
				if(m_unitScale < 0.01)
					sprintf(buf, "%.2f %s", i * m_unitScale, m_units.c_str());
				if(m_unitScale < 0.001)
					sprintf(buf, "%.3f %s", i * m_unitScale, m_units.c_str());
				cr->set_line_width(1.0);
				DrawString(m_left - 60, pos - 5, cr, buf, false);
			}

			//Draw Y axis title
			DrawStringVertical(10, m_bodyheight / 2, cr, m_yAxisTitle, false);

			//Draw lines for each child
			for(size_t i=0; i<m_series.size(); i++)
			{
				Graphable* pNode = m_series[i];
				if( pNode->visible() && (pNode->m_series.find(m_seriesName) != pNode->m_series.end()) )
					DrawSeries(pNode->m_series[m_seriesName], cr, pNode->m_color);
			}

			//Draw legend background
			int legendmargin = 2;
			int legendoffset = 2;
			int legendright = m_left + legendw + 2*legendmargin + legendoffset;
			cr->set_source_rgb(1, 1, 1);
			cr->move_to(m_left + legendoffset,	m_top + legendoffset);
			cr->line_to(m_left + legendoffset,	legendh + 2*legendmargin + m_top + legendoffset);
			cr->line_to(legendright, 			legendh + 2*legendmargin + m_top + legendoffset);
			cr->line_to(legendright,			m_top + legendoffset);
			cr->fill();

			//Draw text
			int y = legendmargin + lineheight + legendoffset;
			for(size_t i=0; i<m_series.size(); i++)
			{
				Graphable* pSeries = m_series[i];
				Gdk::Color& color = pSeries->m_color;
				cr->set_source_rgb(color.get_red_p(), color.get_green_p(), color.get_blue_p());

				DrawString(
					m_left + legendmargin + legendoffset,
					y,
					cr,
					pSeries->m_name,
					false);

				y += lineheight;
			}

		cr->restore();
	}

	return true;
}

void Graph::DrawSeries(Series* pSeries, const Cairo::RefPtr<Cairo::Context>& cr, Gdk::Color color)
{
	//Draw it
	cr->set_source_rgb(color.get_red_p(), color.get_green_p(), color.get_blue_p());

	cr->save();

	cr->rectangle(m_left, m_top, m_bodywidth, m_bodyheight);
	cr->clip();

	//Draw the line
	Series::iterator lit = pSeries->begin();
	float y_prev1 = valueToPosition(lit->value);
	float y_prev2 = y_prev1;
	cr->move_to(timeToPosition(lit->time), y_prev1);
	++lit;
	for(; lit!=pSeries->end(); ++lit)
	{
		float x = timeToPosition(lit->time);
		float y = valueToPosition(lit->value);
		if(x < 0)
		{
			cr->move_to(0, y);
			continue;
		}

		//Calculate moving average
		float ya = (y + y_prev1 + y_prev2) / 3;
		cr->line_to(x, ya);

		//Shift back
		y_prev2 = y_prev1;
		y_prev1 = y;
	}
	cr->stroke();

	cr->restore();
}

float Graph::valueToPosition(float val)
{
	return m_top + (m_maxScale - val)*m_pheight;
}

float Graph::timeToPosition(double time)
{
	return m_right - ((m_now - time) * 10);
}

bool Graph::OnTimer(int nTimer)
{
	if(nTimer == 1)
	{
		//Update view
		queue_draw();
	}

	//false to stop timer
	return true;
}

double GetTime()
{
	timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	double d = static_cast<double>(t.tv_nsec) / 1E9f;
	d += t.tv_sec;
	return d;
}

void DrawStringVertical(float x, float y, const Cairo::RefPtr<Cairo::Context>& cr, string str, bool bBig)
{
	cr->save();

		cr->set_line_width(1.0);

		Glib::RefPtr<Pango::Layout> tlayout = Pango::Layout::create (cr);
		string desc = "sans normal 8";
		if(bBig)
			desc = "sans normal 10";
		Pango::FontDescription font(desc);
		font.set_weight(Pango::WEIGHT_LIGHT);
		tlayout->set_font_description(font);
		tlayout->set_text(str);

		Pango::Rectangle ink, logical;
		tlayout->get_extents(ink, logical);

		float delta = (logical.get_width()/2) / Pango::SCALE;
		cr->move_to(x, y + delta);
		cr->rotate(- M_PI / 2);

		tlayout->update_from_cairo_context(cr);
		tlayout->show_in_cairo_context(cr);
		cr->stroke();

	cr->restore();
}
