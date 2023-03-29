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

#include "../scopehal/scopehal.h"
#include "SNRFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SNRFilter::SNRFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_COUNTS), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);
	CreateInput("in");

	SetData(nullptr, 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SNRFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SNRFilter::GetProtocolName()
{
	return "SNR";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

template<class T>
float DoSNR(T* din)
{
	size_t len = din->m_samples.size();
	
	double average = 0;
	double denomenator = 0;

	for(size_t i = 0; i < len; i++)
	{
		size_t length;
		if constexpr (std::is_same<T, UniformAnalogWaveform>::value)
		{
			length = 1;
		}
		else
		{
			length = din->m_durations[i];
		}

		average += din->m_samples[i] * length;
		denomenator += length;
	}

	average /= denomenator;

	double stddev = 0;

	for(size_t i = 0; i < len; i++)
	{
		size_t length;
		if constexpr (std::is_same<T, UniformAnalogWaveform>::value)
		{
			length = 1;
		}
		else
		{
			length = din->m_durations[i];
		}

		stddev += pow(din->m_samples[i] - average, 2) * length;
	}

	stddev = sqrt(stddev / denomenator);

	return average / stddev;
}

void SNRFilter::Refresh()
{
	auto w = GetInput(0).GetData();

	auto sdata = dynamic_cast<SparseAnalogWaveform*>(w);
	auto udata = dynamic_cast<UniformAnalogWaveform*>(w);

	float result;

	if (sdata)
		result = DoSNR(sdata);
	else if (udata)
		result = DoSNR(udata);
	else
		return;

	m_streams[0].m_value = result;
}
