/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
 */

#include "scopehal.h"
#include "EyeWaveform.h"
#include "EyeMask.h"

#ifndef CANVAS_ITY_IMPLEMENTATION
#define CANVAS_ITY_IMPLEMENTATION
#include "../canvas_ity/src/canvas_ity.hpp"
#endif

#include <time.h>

/* ----------------------------------------------------------------------- */
/*
  Easy embeddable cross-platform high resolution timer function. For each 
  platform we select the high resolution timer. You can call the 'ns()' 
  function in your file after embedding this. 
*/
#include <stdint.h>
#if defined(__linux)
#  define HAVE_POSIX_TIMER
#  include <time.h>
#  ifdef CLOCK_MONOTONIC
#     define CLOCKID CLOCK_MONOTONIC
#  else
#     define CLOCKID CLOCK_REALTIME
#  endif
#elif defined(__APPLE__)
#  define HAVE_MACH_TIMER
#  include <mach/mach_time.h>
#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif
static uint64_t ns() {
  static uint64_t is_init = 0;
#if defined(__APPLE__)
    static mach_timebase_info_data_t info;
    if (0 == is_init) {
      mach_timebase_info(&info);
      is_init = 1;
    }
    uint64_t now;
    now = mach_absolute_time();
    now *= info.numer;
    now /= info.denom;
    return now;
#elif defined(__linux)
    static struct timespec linux_rate;
    if (0 == is_init) {
      clock_getres(CLOCKID, &linux_rate);
      is_init = 1;
    }
    uint64_t now;
    struct timespec spec;
    clock_gettime(CLOCKID, &spec);
    now = spec.tv_sec * 1.0e9 + spec.tv_nsec;
    return now;
#elif defined(_WIN32)
    static LARGE_INTEGER win_frequency;
    if (0 == is_init) {
      QueryPerformanceFrequency(&win_frequency);
      is_init = 1;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t) ((1e9 * now.QuadPart)  / win_frequency.QuadPart);
#endif
}
/* ----------------------------------------------------------------------- */

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

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
	@brief Loads the YAML file
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
	@brief Checks a raw eye pattern dataset against the mask
 */
float EyeMask::CalculateHitRate(
	EyeWaveform* cap,
	size_t width,
	size_t height,
	float fullscalerange,
	float xscale,
	float xoff
	) const
{
	//TODO: performance optimization, don't re-render mask every waveform, only when we resize

	auto time1 = ns();
	printf("Start time: %f µs\n", time1/1e3);

	canvas_ity::canvas canvas( width, height ); // width and height could be reversed


	auto time11 = ns();
	printf("%f µs\n", (time11-time1)/1e3);

	canvas.set_color( canvas_ity::fill_style, 0.0f, 0.0f, 0.0f, 1.0f);
    canvas.fill();
	
	auto time12 = ns();
	printf("%f µs\n", (time12-time1)/1e3);

	//Software rendering
	float yscale = height / fullscalerange;
	float yoff = 0.0f;

	canvas.set_color( canvas_ity::fill_style, 0.0f, 0.0f, 0.0f, 1.0f );

	canvas.move_to( -1e5, 0 ); 
	canvas.line_to( 1e5, 0 );

	canvas.line_to( 1e5, height );
	canvas.line_to(-1e5, height);


	auto time13 = ns();
	printf("%f µs\n", (time13-time1)/1e3);

	canvas.fill();

	canvas.set_color( canvas_ity::fill_style, 1.0f, 1.0f, 1.0f, 1.0f );

	auto time2 = ns();
	printf("%f µs\n", (time2-time1)/1e3);

	//Draw each polygon
	for(auto poly : m_polygons)
	{
		for(size_t i=0; i<poly.m_points.size(); i++)
		{
			auto point = poly.m_points[i];

			//Convert from ps to UI if needed
			float time = point.m_time;
			if(m_timebaseIsRelative)
				time *= cap->GetUIWidth();

			float x = (time - xoff) * xscale;

			float y = height/2 - ( (point.m_voltage + yoff) * yscale );

			if(i == 0) // TODO: Probably not necessary and can always run line_to(x, y)
				canvas.move_to(x, y); // Set to starting point for line if first run
			else
				canvas.line_to(x, y); // Draw line to next coord
		}
		canvas.fill(); // fill the resultant line defined polygon with the current color (white)
	}

	auto time3 = ns();
	printf("%f µs\n", (time3-time2)/1e3);

	//Test each pixel of the eye pattern against the mask
	float nmax = 0;
	int stride = sizeof(unsigned char) * 4; // TODO: Check this for correctness

	vector<unsigned char> image_data(width*height);

	canvas.get_image_data(image_data.data(), width, height, stride, 0,0);

	auto time4 = ns();
	printf("%f µs\n", (time4-time3)/1e3);


	if(cap->GetType() == EyeWaveform::EYE_NORMAL)
	{
		auto accum = cap->GetAccumData();

		auto data = &image_data[0];
		for(size_t y=0; y<height; y++)
		{

			auto timey = ns();
			//printf("%ld %f µs\n", y, (timey-time4)/1e3);
			auto row = data + (y*stride);
			auto eyerow = accum + (y*width);
			for(size_t x=0; x<width; x++)
			{
				//If mask pixel isn't black, count violations
				uint32_t pix = row[x];
				if( (pix & 0xff) != 0)
				{
					float rate = (eyerow[x] * 1.0f / cap->GetTotalUIs());
					if(rate > nmax)
						nmax = rate;
				}
			}
		}
	}
	else //if(cap->GetType() == EyeWaveform::EYE_BER)
	{
		auto accum = cap->GetData();
		
		auto data = &image_data[0];

		for(size_t y=0; y<height; y++)
		{
			auto row = data + (y*stride);
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
	}

	return nmax;
}
