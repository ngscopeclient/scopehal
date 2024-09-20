/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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
	@brief Declaration of ConstellationWaveform
	@ingroup datamodel
 */

#ifndef ConstellationWaveform_h
#define ConstellationWaveform_h

#include "../scopehal/DensityFunctionWaveform.h"

/**
	@brief A constellation diagram
	@ingroup datamodel
 */
class ConstellationWaveform : public DensityFunctionWaveform
{
public:

	ConstellationWaveform(size_t width, size_t height);
	virtual ~ConstellationWaveform();

	//not copyable or assignable
	ConstellationWaveform(const ConstellationWaveform&) =delete;
	ConstellationWaveform& operator=(const ConstellationWaveform&) =delete;

	///@brief Returns the raw accumulator sample data
	int64_t* GetAccumData()
	{ return m_accumdata; }

	void Normalize();

	///@brief Returns the number of integrated symbols in the constellation
	size_t GetTotalSymbols()
	{ return m_totalSymbols; }

	/**
		@brief Marks the waveform as having integrated another batch of symbols

		@param symbols	Number of symbols integrated
	 */
	void IntegrateSymbols(size_t symbols)
	{ m_totalSymbols += symbols; }

	/**
		@brief Value to pre-normalize the waveform to before clipping to the range [0, 1]. Must be non-negative.

		The default of 1.0 means the input range is mapped losslessly to [0, 1].

		Values less than 1.0 result in the output never reaching full scale.

		Values greater than 1.0 allow clipping; for example a saturation level of 2.0 means the input is mapped to
		[0, 2] then clipped to [0, 1]. This is equivalent to mapping the low half of the input values to [0, 1] and
		saturating beyond that point.
	 */
	float m_saturationLevel;

protected:

	/**
		@brief Raw accumulator buffer, not normalized

		2D array of width*height values, each counting the number of hits at that pixel location
	 */
	int64_t* m_accumdata;

	///@brief The number of symbols which have been integrated so far
	size_t m_totalSymbols;
};

#endif
