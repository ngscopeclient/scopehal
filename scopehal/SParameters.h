/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

#include <complex>

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

	SParameterPoint(float f, std::complex<float> c)
	: m_frequency(f)
	, m_amplitude(abs(c))
	, m_phase(arg(c))
	{
	}

	float	m_frequency;	//Hz
	float	m_amplitude;	//magnitude
	float	m_phase;		//radians from -pi to +pi

	std::complex<float> ToComplex()
	{ return std::polar(m_amplitude, m_phase); }
};

/**
	@brief A single S-parameter array
 */
class SParameterVector
{
public:
	SParameterVector()
	{}

	/**
		@brief Creates an S-parameter vector from analog waveforms in dB / degree format
	 */
	template<class T>
	SParameterVector(const T* wmag, const T* wang)
	{
		AssertTypeIsAnalogWaveform(wmag);
		AssertTypeIsAnalogWaveform(wang);

		ConvertFromWaveforms(wmag, wang);
	}

	/**
		@brief Loads the vector from a pair of waveforms in mag/angle format.

		The waveforms may be sparse or uniformly sampled, but must be sampled at the same frequencies.
	 */
	template<class T>
	__attribute__((noinline))
	void ConvertFromWaveforms(const T* wmag, const T* wang)
	{
		if( (wmag == nullptr) || (wang == nullptr) )
		{
			LogError("Null input supplied to SParameterVector::ConvertFromWaveforms\n");
			return;
		}

		size_t len = std::min(wmag->size(), wang->size());
		m_points.resize(len);
		m_points.PrepareForCpuAccess();

		float ascale = M_PI / 180;
		for(size_t i=0; i<len; i++)
		{
			m_points[i] = SParameterPoint(
				GetOffsetScaled(wmag, i),
				pow(10, wmag->m_samples[i] / 20),
				wang->m_samples[i] * ascale);
		}

		m_points.MarkModifiedFromCpu();
	}

	void ConvertToWaveforms(SparseAnalogWaveform* wmag, SparseAnalogWaveform* wang);

	SParameterPoint InterpolatePoint(float frequency) const;
	float InterpolateMagnitude(float frequency) const;
	float InterpolateAngle(float frequency) const;

	AcceleratorBuffer<SParameterPoint> m_points;

	void resize(size_t nsize)
	{ m_points.resize(nsize); }

	float GetGroupDelay(size_t bin) const;

	size_t size() const
	{ return m_points.size(); }

	SParameterPoint& operator[](size_t i)
	{ return m_points[i]; }

protected:
	float InterpolatePhase(float phase_lo, float phase_hi, float frac) const;
};

typedef std::pair<int, int> SPair;

/**
	@brief A set of S-parameters.
 */
class SParameters
{
public:
	SParameters();
	virtual ~SParameters();

	void Clear();
	void Allocate(int nports = 2);

	bool empty() const
	{ return m_params.empty(); }

	/**
		@brief Sample a single point from a single S-parameter
	 */
	SParameterPoint SamplePoint(int to, int from, float frequency)
	{ return m_params[ SPair(to, from) ]->InterpolatePoint(frequency); }

	SParameterVector& operator[] (SPair pair)
	{ return *m_params[pair]; }

	const SParameterVector& operator[] (SPair pair) const
	{ return *(m_params.find(pair)->second); }

	friend class TouchstoneParser;

	enum FreqUnit
	{
		FREQ_HZ,
		FREQ_KHZ,
		FREQ_MHZ,
		FREQ_GHZ
	};

	enum ParameterFormat
	{
		FORMAT_MAG_ANGLE,
		FORMAT_DBMAG_ANGLE,
		FORMAT_REAL_IMAGINARY
	};

	void SaveToFile(const std::string& path, ParameterFormat format = FORMAT_MAG_ANGLE, FreqUnit freqUnit = FREQ_GHZ);

	size_t GetNumPorts() const
	{ return m_nports; }

protected:
	std::map< SPair , SParameterVector*> m_params;

	size_t m_nports;
};

#endif
