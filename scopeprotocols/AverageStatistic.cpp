/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "scopeprotocols.h"

using namespace std;

void AverageStatistic::Clear()
{
	m_pastSums.clear();
	m_pastCounts.clear();
}

string AverageStatistic::GetStatisticName()
{
	return "Average";
}

bool AverageStatistic::Calculate(StreamDescriptor stream, double& value)
{
	//Start integrating from the past value, if we have one
	value = 0;
	size_t count = 0;
	if(m_pastSums.find(stream) != m_pastSums.end())
	{
		value = m_pastSums[stream];
		count = m_pastCounts[stream];
	}

	//Get input data
	auto w = stream.GetData();
	auto udata = dynamic_cast<UniformAnalogWaveform*>(w);
	auto sdata = dynamic_cast<SparseAnalogWaveform*>(w);

	//Add new sample data
	if(udata)
	{
		for(auto sample : udata->m_samples)
			value += sample;
	}
	else if(sdata)
	{
		for(auto sample : sdata->m_samples)
			value += sample;
	}
	count += w->size();

	//Average and save
	m_pastCounts[stream] = count;
	m_pastSums[stream] = value;

	value /= count;

	return true;
}
