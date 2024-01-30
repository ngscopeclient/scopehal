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
#include "../scopehal/AlignedAllocator.h"
#include "BusHeatmapFilter.h"
#include "CANDecoder.h"
#include "SpectrogramFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BusHeatmapFilter::BusHeatmapFilter(const string& color)
	: Filter(color, CAT_BUS)
	, m_maxAddress("Max Address")
	, m_yBinSize("Y Bin Size")
	, m_xBinSize("X Bin Size")
{
	AddStream(Unit(Unit::UNIT_HEXNUM), "data", Stream::STREAM_TYPE_SPECTROGRAM);

	//Set up channels
	CreateInput("din");

	m_parameters[m_maxAddress] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_parameters[m_maxAddress].SetIntVal(2047);

	m_parameters[m_yBinSize] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_parameters[m_yBinSize].SetIntVal(1);

	m_parameters[m_xBinSize] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_xBinSize].SetIntVal(1000LL * 1000LL * 1000LL * 1000LL * 50); //50 ms

	SetVoltageRange(128, 0);
}

BusHeatmapFilter::~BusHeatmapFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool BusHeatmapFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	//for now only support canbus
	if( (i == 0) && (dynamic_cast<CANWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string BusHeatmapFilter::GetProtocolName()
{
	return "Bus Heatmap";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

FlowGraphNode::DataLocation BusHeatmapFilter::GetInputLocation()
{
	return LOC_DONTCARE;
}

void BusHeatmapFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}
	auto din = dynamic_cast<CANWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}

	//Extract parameters for density scaling
	int64_t xscale = m_parameters[m_xBinSize].GetIntVal();
	int64_t yscale = m_parameters[m_yBinSize].GetIntVal();
	int64_t maxy = m_parameters[m_maxAddress].GetIntVal();
	size_t ysize = (maxy + 1) / yscale;

	auto nlast = din->m_offsets.size() - 1;
	size_t nblocks = ( din->m_offsets[nlast] * din->m_timescale ) / xscale;

	//Create the output
	SpectrogramWaveform* cap = dynamic_cast<SpectrogramWaveform*>(GetData(0));
	if(cap)
	{
		if( (cap->GetBinSize() == yscale) &&
			(cap->GetWidth() == nblocks) &&
			(cap->GetHeight() == ysize) )
		{
			//same config, we can reuse it
		}

		//no, ignore it
		else
			cap = nullptr;
	}
	if(!cap)
	{
		cap = new SpectrogramWaveform(
			nblocks,
			ysize,
			yscale,
			0
			);
	}

	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->m_timescale = xscale;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	//Fill the buffer with zeroes
	size_t len = ysize * nblocks;
	auto& buf = cap->GetOutData();
	auto p = buf.GetCpuPointer();
	memset(p, 0, len*sizeof(float));

	//Integrate packets
	auto nin = din->m_offsets.size();
	float nmax = 0;
	for(size_t i=0; i<nin; i++)
	{
		//Only look at CAN ID packets, ignore anything else
		auto s = din->m_samples[i];
		if(s.m_stype != CANSymbol::TYPE_ID)
			continue;

		//Get X/Y histogram bins
		auto xbin = din->m_offsets[i] * din->m_timescale / xscale;
		auto ybin = s.m_data / yscale;

		//Increment the bin
		auto f = p[ybin*nblocks + xbin] ++;
		nmax = max(nmax, f);
	}

	//Normalize
	float norm = 1.0 / nmax;
	for(size_t i=0; i<len; i++)
		p[i] *= norm;

	//Done
	cap->MarkModifiedFromCpu();
}
