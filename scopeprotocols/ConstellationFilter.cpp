/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "../scopehal/scopehal.h"
#include "ConstellationFilter.h"
#include <algorithm>
#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ConstellationFilter::ConstellationFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_height(1)
	, m_width(1)
	, m_xscale(0)
	, m_nominalRangeI(0.5)
	, m_nominalRangeQ(0.5)
	, m_nominalCenterI(0)
	, m_nominalCenterQ(0)
	, m_modulation("Modulation")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_CONSTELLATION);

	m_xAxisUnit = Unit(Unit::UNIT_MICROVOLTS);

	CreateInput("i");
	CreateInput("q");
	CreateInput("clk");

	m_parameters[m_modulation] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_modulation].AddEnumValue("None", MOD_NONE);
	m_parameters[m_modulation].AddEnumValue("QAM-4 / QPSK", MOD_QAM4);
	m_parameters[m_modulation].AddEnumValue("QAM-9 / 2D-PAM3", MOD_QAM9);
	m_parameters[m_modulation].AddEnumValue("QAM-16", MOD_QAM16);
	m_parameters[m_modulation].SetIntVal(MOD_NONE);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ConstellationFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ConstellationFilter::GetProtocolName()
{
	return "Constellation";
}

float ConstellationFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_inputs[0].GetVoltageRange();
}

float ConstellationFilter::GetOffset(size_t /*stream*/)
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ConstellationFilter::ClearSweeps()
{
	SetData(NULL, 0);
}

void ConstellationFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	if(!VerifyAllInputsOK())
	{
		//if input goes momentarily bad, don't delete output - just stop updating
		return;
	}

	auto din_i = GetInputWaveform(0);
	auto din_q = GetInputWaveform(1);
	auto clk = GetInputWaveform(2);

	//Recompute the nominal constellation point locations
	RecomputeNominalPoints();

	//Sample the I/Q input
	SparseAnalogWaveform samples_i;
	SparseAnalogWaveform samples_q;
	SampleOnAnyEdgesBase(din_i, clk, samples_i);
	SampleOnAnyEdgesBase(din_q, clk, samples_q);

	size_t inlen = min(samples_i.size(), samples_q.size());

	//Generate the output waveform
	auto cap = dynamic_cast<ConstellationWaveform*>(GetData(0));
	if(!cap)
		cap = ReallocateWaveform();
	cap->PrepareForCpuAccess();

	//Recompute scales
	float xscale = m_width / GetVoltageRange(0);
	float xmid = m_width / 2;
	float yscale = m_height / GetVoltageRange(0);
	float ymid = m_height / 2;

	//Actual integration loop
	//TODO: vectorize, GPU, or both?
	auto data = cap->GetAccumData();
	for(size_t i=0; i<inlen; i++)
	{
		ssize_t x = static_cast<ssize_t>(round(xmid + xscale * samples_i.m_samples[i]));
		ssize_t y = static_cast<ssize_t>(round(ymid + yscale * samples_q.m_samples[i]));

		//bounds check
		if( (x < 0) || (x >= (ssize_t)m_width) || (y < 0) || (y >= (ssize_t)m_height) )
			continue;

		//fill
		data[y*m_width + x] ++;
	}

	//Count total number of symbols we've integrated
	cap->IntegrateSymbols(inlen);
	cap->Normalize();
}

void ConstellationFilter::RecomputeNominalPoints()
{
	m_points.clear();

	auto mod = m_parameters[m_modulation].GetIntVal();
	switch(mod)
	{
		//2x2 square
		case MOD_QAM4:

			for(int i=-1; i<=1; i += 2)
			{
				for(int q=-1; q<=1; q += 2)
				{
					m_points.push_back(ConstellationPoint(
						(m_nominalCenterI + i*m_nominalRangeI) * 1e6,	//convert V to uV
						m_nominalCenterQ + q*m_nominalRangeQ,
						i,
						q));
				}
			}

			break;

		//3x3 square
		case MOD_QAM9:

			for(int i=-1; i<=1; i++)
			{
				for(int q=-1; q<=1; q++)
				{
					m_points.push_back(ConstellationPoint(
						(m_nominalCenterI + i*m_nominalRangeI) * 1e6,	//convert V to uV
						m_nominalCenterQ + q*m_nominalRangeQ,
						i,
						q));
				}
			}

			break;

		//4x4 square
		case MOD_QAM16:

			for(float i=-1; i<=1; i += 2.0/3)
			{
				for(float q=-1; q<=1; q += 2.0/3)
				{
					m_points.push_back(ConstellationPoint(
						(m_nominalCenterI + i*m_nominalRangeI) * 1e6,	//convert V to uV
						m_nominalCenterQ + q*m_nominalRangeQ,
						i,
						q));
				}
			}


			break;

		//Nothing
		default:
		case MOD_NONE:
			break;
	}
}

ConstellationWaveform* ConstellationFilter::ReallocateWaveform()
{
	auto cap = new ConstellationWaveform(m_width, m_height);
	cap->m_timescale = 1;
	SetData(cap, 0);
	return cap;
}

vector<string> ConstellationFilter::EnumActions()
{
	vector<string> ret;
	ret.push_back("Normalize");
	return ret;
}

bool ConstellationFilter::PerformAction(const string& id)
{
	if(id == "Normalize")
	{
		size_t order = 1;
		auto mod = m_parameters[m_modulation].GetIntVal();
		switch(mod)
		{
			case MOD_QAM4:
				order = 2;
				break;

			case MOD_QAM9:
				order = 3;
				break;

			case MOD_QAM16:
				order = 4;
				break;

			//can't autoscale if no constellation to fit!
			case MOD_NONE:
			default:
				return true;
		}

		//TODO: handle uniform inputs too
		auto din_i = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(0));
		auto din_q = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(1));
		if(din_i && din_q)
		{
			//Calculate range of input
			float halfrange = GetVoltageRange(0)/2;
			float mid = GetOffset(0);
			float ivmin = mid - halfrange;
			float ivmax = mid + halfrange;
			float qvmin = ivmin;
			float qvmax = ivmax;

			//Print out extrema
			Unit yunit(Unit::UNIT_VOLTS);
			LogTrace("I range: (%s, %s)\n", yunit.PrettyPrint(ivmin).c_str(), yunit.PrettyPrint(ivmax).c_str());
			LogTrace("Q range: (%s, %s)\n", yunit.PrettyPrint(qvmin).c_str(), yunit.PrettyPrint(qvmax).c_str());

			//Take a histogram and find the top N peaks (should be roughly evenly distributed)
			const int64_t nbins = 128;
			auto ihist = MakeHistogram(din_i, ivmin, ivmax, nbins);
			auto qhist = MakeHistogram(din_q, qvmin, qvmax, nbins);

			float ismin, ismax, qsmin, qsmax;
			GetMinMaxSymbols(ihist, ivmin, ismin, ismax, (ivmax - ivmin) / nbins, order, nbins);
			GetMinMaxSymbols(qhist, ivmin, qsmin, qsmax, (qvmax - qvmin) / nbins, order, nbins);
			LogTrace("I symbol range: (%s, %s)\n", yunit.PrettyPrint(ismin).c_str(), yunit.PrettyPrint(ismax).c_str());
			LogTrace("Q symbol range: (%s, %s)\n", yunit.PrettyPrint(qsmin).c_str(), yunit.PrettyPrint(qsmax).c_str());

			m_nominalCenterI = (ismin + ismax) / 2;
			m_nominalCenterQ = (qsmin + qsmax) / 2;

			m_nominalRangeI = (ismax - ismin) / 2;
			m_nominalRangeQ = (qsmax - qsmin) / 2;
		}
	}
	return true;
}

void ConstellationFilter::GetMinMaxSymbols(
	vector<size_t>& hist,
	float vmin,
	float& vmin_out,
	float& vmax_out,
	float binsize,
	size_t order,
	ssize_t nbins)
{
	//Search radius for bins (for now hard code, TODO make this adaptive?)
	const int64_t searchrad = 5;
	ssize_t nend = nbins - 1;
	vector<Peak> peaks;
	for(ssize_t i=searchrad; i<(nbins - searchrad); i++)
	{
		//Locate the peak
		ssize_t left = std::max((ssize_t)searchrad, (ssize_t)(i - searchrad));
		ssize_t right = std::min((ssize_t)(i + searchrad), (ssize_t)nend);

		float target = hist[i];
		bool is_peak = true;
		for(ssize_t j=left; j<=right; j++)
		{
			if(i == j)
				continue;
			if(hist[j] >= target)
			{
				//Something higher is to our right.
				//It's higher than anything from left to j. This makes it a candidate peak.
				//Restart our search from there.
				if(j > i)
					i = j-1;

				is_peak = false;
				break;
			}
		}
		if(!is_peak)
			continue;

		//Do a weighted average of our immediate neighbors to fine tune our position
		ssize_t fine_rad = 10;
		left = std::max((ssize_t)1, i - fine_rad);
		right = std::min(i + fine_rad, nend);
		double total = 0;
		double count = 0;
		for(ssize_t j=left; j<=right; j++)
		{
			total += j*hist[j];
			count += hist[j];
		}
		peaks.push_back(Peak(round(total / count), target, 1));
	}

	//Sort the peak table by height and pluck out the requested count, use these as our levels
	std::sort(peaks.rbegin(), peaks.rend(), std::less<Peak>());
	vector<float> levels;
	if(peaks.size() < order)
	{
		LogDebug("Requested PAM-%zu but only found %zu peaks, cannot proceed\n", order, peaks.size());
		vmin_out = vmin;
		vmax_out = vmin + binsize*nbins;
		return;
	}

	for(size_t i=0; i<order; i++)
		levels.push_back((peaks[i].m_x * binsize) + vmin);

	//Now sort the levels by voltage to get symbol values from lowest to highest
	std::sort(levels.begin(), levels.end());

	//and save the output levels
	vmin_out = levels[0];
	vmax_out = levels[order-1];
}
