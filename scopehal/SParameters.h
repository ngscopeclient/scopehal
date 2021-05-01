/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of SParameters and related classes
 */
#ifndef SParameters_h
#define SParameters_h

/**
	@brief A single point in an S-parameter dataset
 */
class SParameterPoint
{
public:
	SParameterPoint()
	{}

	SParameterPoint(float f, float a, float p)
	: m_frequency(f)
	, m_amplitude(a)
	, m_phase(p)
	{
	}

	float	m_frequency;	//Hz
	float	m_amplitude;	//magnitude
	float	m_phase;		//radians from -pi to +pi
};

/**
	@brief A single S-parameter array
 */
class SParameterVector
{
public:

	SParameterPoint InterpolatePoint(float frequency) const;

	std::vector<SParameterPoint> m_points;

	float GetGroupDelay(size_t bin);

	size_t size()
	{ return m_points.size(); }

	SParameterVector& operator *=(const SParameterVector& rhs);

	SParameterPoint& operator[](size_t i)
	{ return m_points[i]; }
};

typedef std::pair<int, int> SPair;

/**
	@brief A set of S-parameters.

	For now, only supports full 2-port.
 */
class SParameters
{
public:
	SParameters();
	virtual ~SParameters();

	void Clear();

	bool empty() const
	{ return m_params.empty(); }

	/**
		@brief Sample a single point from a single S-parameter
	 */
	SParameterPoint SamplePoint(int to, int from, float frequency)
	{ return m_params[ SPair(to, from) ]->InterpolatePoint(frequency); }

	SParameters& operator *=(const SParameters& rhs);

	SParameterVector& operator[] (SPair pair)
	{ return *m_params[pair]; }

	friend class TouchstoneParser;

protected:
	void Allocate();

	std::map< SPair , SParameterVector*> m_params;
};

#endif
