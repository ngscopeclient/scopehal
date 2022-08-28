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

#include "../scopehal/scopehal.h"
#include "JitterFilter.h"
#include <random>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

JitterFilter::JitterFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_stdevname("Rj Stdev")
	, m_pjfreqname("Pj Frequency")
	, m_pjamplitudename("Pj Amplitude")

{
	AddDigitalStream("data");
	CreateInput("din");

	m_parameters[m_stdevname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_FS));
	m_parameters[m_stdevname].SetFloatVal(5000);

	m_parameters[m_pjfreqname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_pjfreqname].SetFloatVal(10 * 1000 * 1000);

	m_parameters[m_pjamplitudename] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_FS));
	m_parameters[m_pjamplitudename].SetFloatVal(3000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool JitterFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string JitterFilter::GetProtocolName()
{
	return "Jitter";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void JitterFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto udin = dynamic_cast<UniformDigitalWaveform*>(din);
	din->PrepareForCpuAccess();

	size_t len = din->size();

	float pjfreq = m_parameters[m_pjfreqname].GetIntVal();
	float stdev = m_parameters[m_stdevname].GetFloatVal();
	float pjamp = m_parameters[m_pjamplitudename].GetFloatVal();

	minstd_rand rng(rand());
	normal_distribution<> noise(0, stdev);

	//Copy the initial configuration over
	auto cap = SetupEmptySparseDigitalOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();
	cap->Resize(len);
	if(sdin)
		cap->m_samples.CopyFrom(sdin->m_samples);
	else
		cap->m_samples.CopyFrom(udin->m_samples);
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;

	float startPhase = fmodf(rand(), M_PI);
	float radians_per_fs = 2 * M_PI * pjfreq / FS_PER_SECOND;

	//Add the noise
	//gcc 8.x / 9.x have false positive here (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99536)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
	for(size_t i=0; i<len; i++)
	{
		size_t tstart = GetOffsetScaled(sdin, udin, i);

		size_t rj = noise(rng);
		size_t pj = sin(tstart * radians_per_fs + startPhase) * pjamp;
		size_t tj = rj + pj;

		//Add jitter to the start time
		cap->m_offsets[i] = tstart + tj;
		cap->m_durations[i] = GetDurationScaled(sdin, udin, i);

		//Update duration of previous sample
		if(i > 0)
			cap->m_durations[i-1] = cap->m_offsets[i] - cap->m_offsets[i-1];
	}
#pragma GCC diagnostic pop
}
