/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                    *
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
	@brief Implementation of EyeMask
	@ingroup datamodel
 */

#include "scopehal.h"
#include "EyeWaveform.h"
#include "EyeMask.h"

#ifndef CANVAS_ITY_IMPLEMENTATION
#define CANVAS_ITY_IMPLEMENTATION
#include "../canvas_ity/src/canvas_ity.hpp"
#endif

#include <time.h>
#include <iostream>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

///@brief Initialize an empty mask
EyeMask::EyeMask()
	: m_hitrate(0)
	, m_timebaseIsRelative(false)
{
}

EyeMask::~EyeMask()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mask file parsing

/**
	@brief Loads an eye mask from a YAML file

	@param path		Filename of the file

	@return True on success, false on failure
 */
bool EyeMask::Load(string path)
{
	//Clear out any previous state
	m_polygons.clear();
	m_hitrate = 0;
	m_timebaseIsRelative = false;
	m_maskname = "";

	try
	{
		m_fname = path;
		auto docs = YAML::LoadAllFromFile(path);
		if(!Load(docs[0]))
			return false;
	}
	catch(const YAML::BadFile& ex)
	{
		return false;
	}

	return true;
}

/**
	@brief Loads the mask from a YAML node

	@param node	Root node of the YAML document

	@return True on success, false on failure
 */
bool EyeMask::Load(const YAML::Node& node)
{
	//Clear out any previous state
	m_polygons.clear();
	m_hitrate = 0;
	m_timebaseIsRelative = false;
	m_maskname = "";

	//Load protocol section
	auto proto = node["protocol"];
	for(auto it : proto)
	{
		auto name = it.first.as<string>();
		if(name == "name")
			m_maskname = it.second.as<string>();
	}

	//For now, ignore display limits

	//Load units
	auto units = node["units"];
	float yscale = 1;
	float timebaseScale = 1;
	for(auto it : units)
	{
		auto name = it.first.as<string>();
		if(name == "xscale")
		{
			auto scale = it.second.as<string>();
			if(scale == "ui")
				m_timebaseIsRelative = true;
			else if(scale == "ps")
			{
				m_timebaseIsRelative = false;
				timebaseScale = 1000;
			}
			else if(scale == "fs")
				m_timebaseIsRelative = false;
			else
				LogError("Unrecognized xscale \"%s\"\n", scale.c_str());
		}
		if(name == "yscale")
		{
			auto scale = it.second.as<string>();
			if(scale == "mv")
				yscale = 0.001f;
			else if(scale == "v")
				yscale = 0.001f;
			else
				LogError("Unrecognized yscale \"%s\"\n", scale.c_str());
		}
	}

	//Load pass conditions
	auto conditions = node["conditions"];
	for(auto it : conditions)
	{
		auto name = it.first.as<string>();
		if(name == "hitrate")
			m_hitrate = it.second.as<float>();
	}

	//Load actual mask polygons
	auto mask = node["mask"];
	for(auto p : mask)
	{
		EyeMaskPolygon poly;

		auto points = p["points"];
		for(auto v : points)
			poly.m_points.push_back(EyeMaskPoint(v["x"].as<float>() * timebaseScale, v["y"].as<float>() * yscale));

		m_polygons.push_back(poly);
	}

	return true;
}

/**
	@brief Renders the mask to an offscreen buffer we can use for hit testing
 */
void EyeMask::RenderForAnalysis(
		EyeWaveform* waveform,
		float xscale,
		float xoff,
		float yscale,
		float yoff,
		float height) const
{
	//clear background to blank
	m_canvas->clear_rectangle(0, 0, m_width, m_height);

	//Draw each polygon
	m_canvas->set_color( canvas_ity::fill_style, 1.0f, 1.0f, 1.0f, 1.0f );
	float ypixoff = height / 2;
	for(auto poly : m_polygons)
	{
		for(size_t i=0; i<poly.m_points.size(); i++)
		{
			auto point = poly.m_points[i];

			//Convert from ps to UI if needed
			float time = point.m_time;
			if(m_timebaseIsRelative)
				time *= waveform->GetUIWidth();

			float x = (time - xoff) * xscale;
			float y = ( (point.m_voltage + yoff) * -yscale ) + ypixoff;

			if(i == 0) // TODO: Probably not necessary and can always run line_to(x, y)
				m_canvas->move_to(x, y); // Set to starting point for line if first run
			else
				m_canvas->line_to(x, y); // Draw line to next coord
		}
		m_canvas->fill(); // fill the resultant line defined polygon with the current color (white)
	}
}

/**
	@brief Checks a raw eye pattern dataset against the mask
 */
float EyeMask::CalculateHitRate(
	EyeWaveform* cap,
	size_t width,
	size_t height,
	float fullscalerange,
	float xscale,
	float xoff
	)
{
	//TODO: GPU this?
	//For now, we're running on the CPU though. Make sure the data is here when we need it
	cap->GetAccumBuffer().PrepareForCpuAccess();

	if(!m_canvas || (m_width != width) || (m_height != height))
	{
		m_width = width;
		m_height = height;
		m_canvas = std::make_unique< canvas_ity::canvas >( width, height );

		//Software rendering
		float yscale = height / fullscalerange;
		RenderForAnalysis(
			cap,
			xscale,
			xoff,
			yscale,
			0,
			height);
	}

	//Test each pixel of the eye pattern against the mask

	vector<uint8_t> image_data(width*height*4);
	m_canvas->get_image_data(image_data.data(), width, height, m_width*4, 0,0);

	if(cap->GetType() == EyeWaveform::EYE_NORMAL)
	{
		auto accum = cap->GetAccumData();
		size_t nhits = 0;

		uint32_t* data = reinterpret_cast<uint32_t*>(&image_data[0]);
		for(size_t y=0; y<height; y++)
		{
			auto row = data + (y*width);
			auto eyerow = accum + (y*width);

			for(size_t x=0; x<width; x++)
			{
				//If mask pixel isn't black, count violations
				uint32_t pix = row[x];
				auto hits = eyerow[x];
				if(pix & 0xff)
					nhits += hits;
			}
		}

		//LogTrace("Total %zu hits out of %zu samples\n", nhits / EYE_ACCUM_SCALE, cap->GetTotalSamples());
		return nhits * 1.0 / (cap->GetTotalSamples() * EYE_ACCUM_SCALE);
	}
	else //if(cap->GetType() == EyeWaveform::EYE_BER)
	{
		auto accum = cap->GetData();
		float nmax = 0;

		uint32_t* data = reinterpret_cast<uint32_t*>(&image_data[0]);
		for(size_t y=0; y<height; y++)
		{
			auto row = data + (y*width);
			auto eyerow = accum + (y*width);
			for(size_t x=0; x<width; x++)
			{
				//If mask pixel isn't black, count violations
				uint32_t pix = row[x];
				if( (pix & 0xff) != 0)
				{
					//BER eyes don't need any preprocessing since the pixel values are already raw BER
					float rate = eyerow[x];
					if(rate > nmax)
						nmax = rate;
				}
			}
		}

		return nmax;
	}
}
