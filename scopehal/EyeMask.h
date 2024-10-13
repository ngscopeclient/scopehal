/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of EyeMask, EyeMaskPoint, and EyeMaskPolygon
	@ingroup datamodel
 */

#ifndef EyeMask_h
#define EyeMask_h

#include "../canvas_ity/src/canvas_ity.hpp"

class EyeDecoder2;
class EyeWaveform;

/**
	@brief A single point within an EyeMaskPolygon
	@ingroup datamodel
 */
class EyeMaskPoint
{
public:

	///@brief Default constructor
	EyeMaskPoint()
		: m_time(0)
		, m_voltage(0)
	{}

	/**
		@brief Initialize a point from a timestamp and voltage

		@param t	Timestamp
		@param v	Voltage
	 */
	EyeMaskPoint(float t, float v)
	: m_time(t)
	, m_voltage(v)
	{}

	/**
		@brief X axis position of the point.

		Units are either fs or UIs, depending on EyeMask units
	 */
	float m_time;

	///@brief Y axis position of the point
	float m_voltage;
};

/**
	@brief A single polygon within an EyeMask
	@ingroup datamodel
 */
class EyeMaskPolygon
{
public:

	///@brief Set of vertices within the polygon
	std::vector<EyeMaskPoint> m_points;
};

/**
	@brief A mask used for checking eye patterns
	@ingroup datamodel
 */
class EyeMask
{

public:
	EyeMask();
	virtual ~EyeMask();

	bool Load(std::string path);
	bool Load(const YAML::Node& node);

	///@brief Get the filename of the mask
	std::string GetFileName() const
	{ return m_fname; }

	///@brief Get the display name of the eye pattern mask
	std::string GetMaskName() const
	{ return m_maskname; }

	/**
		@brief Get the allowed mask hit rate

		Most standards do not allow any mask hits, however some standards do permit a small hit ratio
	 */
	float GetAllowedHitRate() const
	{ return m_hitrate; }

	void RenderForAnalysis(
		EyeWaveform* waveform,
		float xscale,
		float xoff,
		float yscale,
		float yoff,
		float height) const;

	float CalculateHitRate(
		EyeWaveform* cap,
		size_t width,
		size_t height,
		float fullscalerange,
		float xscale,
		float xoff);

	///@brief Return true if there are no polygons in the mask
	bool empty() const
	{ return m_polygons.empty(); }

	///@brief Returns true if the timebase is in relative units (UI) and false if absolute (time)
	bool IsTimebaseRelative()
	{ return m_timebaseIsRelative; }

	///@brief Return the set of polygons in the mask
	const std::vector<EyeMaskPolygon>& GetPolygons() const
	{ return m_polygons; }

	///@brief Get the rendered width of the mask
	size_t GetWidth()
	{ return m_width; }

	///@brief Get the rendered height of the mask
	size_t GetHeight()
	{ return m_height; }

	//Helpers for unit testing
public:

	/**
		@brief Get the raw image data as RGBA32

		@param pixels	Empty std::vector to be filled with image data
	 */
	void GetPixels(std::vector<uint8_t>& pixels)
	{
		pixels.resize(m_width * m_height * 4);
		m_canvas->get_image_data(pixels.data(), m_width, m_height, m_width*4, 0,0);
	}

protected:

	///@brief Filename of the mask
	std::string m_fname;

	///@brief Set of polygons in the mask
	std::vector<EyeMaskPolygon> m_polygons;

	///@brief Most recent hit rate
	float m_hitrate;

	///@brief true = time measured in UIs || false = time measured in ps
	bool m_timebaseIsRelative;

	///@brief Human readable name of the mask (e.g. "XFI")
	std::string m_maskname;

	///@brief Canvas for rasterizing the mask
    std::unique_ptr< canvas_ity::canvas > m_canvas;

	///@brief Current width
    size_t m_width;

    ///@brief Current height
    size_t m_height;
};

#endif
