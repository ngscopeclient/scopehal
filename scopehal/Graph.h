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
	@brief Declaration of Graph and related classes
 */

#ifndef Graph_h
#define Graph_h

#include <gtkmm.h>

class GraphPoint
{
public:
	double time;
	float value;

	GraphPoint(double t, float v)
	{ time=t; value=v;}
};

typedef std::list<GraphPoint> Series;

typedef std::map< std::string, Series* > SeriesMap;

class Graphable
{
public:
	Graphable(std::string name = "")
	: m_name(name)
	{}

	virtual ~Graphable();

	Series* GetSeries(std::string name);

	virtual bool visible();

	Gdk::Color m_color;
	std::string m_name;
	SeriesMap m_series;							//Summarized data series for this node
};

//The graph window itself
class Graph : public Gtk::Layout
{
public:
	Graph();
	~Graph();

	//Configurable by parent
	std::vector<Graphable*> m_series;
	std::string m_seriesName;

	float m_minScale;
	float m_maxScale;
	float m_scaleBump;

	std::string m_units;
	float m_unitScale;

	float m_maxRedline;
	float m_minRedline;

	std::string m_yAxisTitle;

protected:
	virtual bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr);

	void DrawSeries(Series* pSeries, const Cairo::RefPtr<Cairo::Context>& cr, Gdk::Color);

	float timeToPosition(double time);
	float valueToPosition(float val);

	bool OnTimer(int nTimer);

	/////////////////////////////////////////////////////////
	//Display data

	int m_top;
	int m_bottom;
	int m_left;
	int m_right;

	int m_width;
	int m_height;
	float m_pheight;

	float m_bodywidth;
	float m_bodyheight;

	const int m_lmargin;
	const int m_rmargin;
	const int m_tmargin;
	const int m_bmargin;

	double m_now;
};

double GetTime();

#endif
