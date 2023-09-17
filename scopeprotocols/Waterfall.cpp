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
#include "Waterfall.h"
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WaterfallWaveform::WaterfallWaveform(size_t width, size_t height)
	: DensityFunctionWaveform(width, height)
{
}

WaterfallWaveform::~WaterfallWaveform()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Waterfall::Waterfall(const string& color)
	: Filter(color, CAT_RF)
	, m_width(1)
	, m_height(1)
	, m_maxwidth("Max width")
{
	AddStream(Unit(Unit::UNIT_DBM), "data", Stream::STREAM_TYPE_WATERFALL);
	m_xAxisUnit = Unit(Unit::UNIT_HZ);

	m_parameters[m_maxwidth] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_maxwidth].SetIntVal(131072);

	//Set up channels
	CreateInput("Spectrum");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool Waterfall::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) &&
		(stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
		(stream.m_channel->GetXAxisUnits() == Unit::UNIT_HZ)
		)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float Waterfall::GetOffset(size_t /*stream*/)
{
	return 0;
}

float Waterfall::GetVoltageRange(size_t /*stream*/)
{
	return 1;
}

string Waterfall::GetProtocolName()
{
	return "Waterfall";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Waterfall::ClearPersistence()
{
	SetData(nullptr, 0);
}

void Waterfall::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();
	size_t inlen = din->size();

	//Figure out how wide we want the input capture to be
	size_t maxwidth = m_parameters[m_maxwidth].GetIntVal();
	size_t capwidth = min(maxwidth, inlen);

	//Reallocate if input size changed, or we don't have an input capture at all
	auto cap = dynamic_cast<WaterfallWaveform*>(GetData(0));
	if( (cap == nullptr) || (m_width != capwidth) || (m_width != cap->GetWidth()) || (m_height != cap->GetHeight()) )
	{
		cap = new WaterfallWaveform(capwidth, m_height);
		m_width = capwidth;
		SetData(cap, 0);
	}

	//Figure out the frequency span of the input
	int64_t spanIn = din->m_timescale * inlen;

	//Recalculate timescale
	cap->m_timescale = spanIn / capwidth;

	//Update timestamps
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	//Move the whole waterfall down by one row
	//TODO: can we just rotate indexes or something to make this more efficient?
	float* data = cap->GetData();
	for(size_t y=0; y < m_height-1 ; y++)
		memcpy(data + y*m_width, data + (y+1)*m_width, m_width * sizeof(float));

	//Zero the new row
	float* prow = data + (m_height-1)*m_width;
	memset(prow, 0, m_width*sizeof(float));

	//Add the new data, downsampling if needed, then normalize to full scale range
	float vmin = 1.0 / 255.0;
	float vrange = m_inputs[0].GetVoltageRange();	//db from min to max scale
	float vfs = vrange/2 - m_inputs[0].GetOffset();
	for(size_t x=0; x<m_width; x++)
	{
		//TODO: account for triggerPhase of input!
		size_t binMin = (x * cap->m_timescale) / din->m_timescale;
		size_t binMax = ( ((x+1) * cap->m_timescale) / din->m_timescale ) - 1;

		float maxAmplitude = vmin;
		for(size_t i=binMin; (i <= binMax) && (i <= inlen); i++)
		{
			float v = 1 - ( (din->m_samples[i] - vfs) / -vrange);
			maxAmplitude = max(maxAmplitude, v);
		}
		prow[x] = maxAmplitude;
	}

	cap->MarkModifiedFromCpu();
}
