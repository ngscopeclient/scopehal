/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of EyeMask, EyeMaskPoint, and EyeMaskPolygon
 */
#ifndef EyeMask_h
#define EyeMask_h

#include <cairomm/cairomm.h>

class EyeDecoder2;
class EyeWaveform;

/**
	@brief A single point within an EyeMaskPolygon
 */
class EyeMaskPoint
{
public:
	EyeMaskPoint()
	{}

	EyeMaskPoint(float t, float v)
	: m_time(t)
	, m_voltage(v)
	{}

	float m_time;		//either ps or UIs, depending on EyeMask units
	float m_voltage;	//volts
};

/**
	@brief A single polygon within an EyeMask
 */
class EyeMaskPolygon
{
public:
	std::vector<EyeMaskPoint> m_points;
};

/**
	@brief A mask used for checking eye patterns
 */
class EyeMask
{
public:
	EyeMask();
	virtual ~EyeMask();

	bool Load(std::string path);
	bool Load(const YAML::Node& node);

	std::string GetFileName() const
	{ return m_fname; }

	std::string GetMaskName() const
	{ return m_maskname; }

	float GetAllowedHitRate() const
	{ return m_hitrate; }

	void RenderForDisplay(
		Cairo::RefPtr<Cairo::Context> cr,
		EyeWaveform* waveform,
		float xscale,
		float xoff,
		float yscale,
		float yoff,
		float height) const;

	void RenderForAnalysis(
		Cairo::RefPtr<Cairo::Context> cr,
		EyeWaveform* waveform,
		float xscale,
		float xoff,
		float yscale,
		float yoff,
		float height) const;

	bool empty() const
	{ return m_polygons.empty(); }

protected:
	void RenderInternal(
		Cairo::RefPtr<Cairo::Context> cr,
		EyeWaveform* waveform,
		float xscale,
		float xoff,
		float yscale,
		float yoff,
		float height) const;

	std::string m_fname;
	std::vector<EyeMaskPolygon> m_polygons;

	float m_hitrate;

	//true = time measured in UIs
	//false = time measured in ps
	bool m_timebaseIsRelative;

	std::string m_maskname;
};

#endif
