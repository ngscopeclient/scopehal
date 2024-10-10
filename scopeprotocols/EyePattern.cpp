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
#include "EyePattern.h"
#include <algorithm>
#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyePattern::EyePattern(const string& color)
	: Filter(color, CAT_ANALYSIS)
	, m_height(1)
	, m_width(1)
	, m_xoff(0)
	, m_xscale(0)
	, m_lastClockAlign(ALIGN_CENTER)
	, m_saturationName("Saturation Level")
	, m_centerName("Center Voltage")
	, m_maskName("Mask")
	, m_polarityName("Clock Edge")
	, m_vmodeName("Vertical Scale Mode")
	, m_rangeName("Vertical Range")
	, m_clockAlignName("Clock Alignment")
	, m_rateModeName("Bit Rate Mode")
	, m_rateName("Bit Rate")
{
	AddStream(Unit(Unit::UNIT_COUNTS), "data", Stream::STREAM_TYPE_EYE);
	AddStream(Unit(Unit::UNIT_RATIO_SCI), "hitrate", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_UI), "uisIntegrated", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_SAMPLEDEPTH), "samplesIntegrated", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("din");
	CreateInput("clk");

	m_parameters[m_saturationName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_saturationName].SetFloatVal(1);

	m_parameters[m_centerName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_centerName].SetFloatVal(0);

	m_parameters[m_maskName] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_maskName].SetFileName("");
	m_parameters[m_maskName].m_fileFilterMask = "*.yml";
	m_parameters[m_maskName].m_fileFilterName = "YAML files (*.yml)";

	m_parameters[m_polarityName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_polarityName].AddEnumValue("Rising", CLOCK_RISING);
	m_parameters[m_polarityName].AddEnumValue("Falling", CLOCK_FALLING);
	m_parameters[m_polarityName].AddEnumValue("Both", CLOCK_BOTH);
	m_parameters[m_polarityName].SetIntVal(CLOCK_BOTH);

	m_parameters[m_vmodeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_vmodeName].AddEnumValue("Auto", RANGE_AUTO);
	m_parameters[m_vmodeName].AddEnumValue("Fixed", RANGE_FIXED);

	m_parameters[m_rangeName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_rangeName].SetFloatVal(0.25);

	m_parameters[m_clockAlignName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_clockAlignName].AddEnumValue("Center", ALIGN_CENTER);
	m_parameters[m_clockAlignName].AddEnumValue("Edge", ALIGN_EDGE);
	m_parameters[m_clockAlignName].SetIntVal(ALIGN_CENTER);

	m_parameters[m_rateModeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_rateModeName].AddEnumValue("Auto", MODE_AUTO);
	m_parameters[m_rateModeName].AddEnumValue("Fixed", MODE_FIXED);
	m_parameters[m_rateModeName].SetIntVal(MODE_AUTO);

	m_parameters[m_rateName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_rateName].SetIntVal(1250000000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EyePattern::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EyePattern::GetProtocolName()
{
	return "Eye pattern";
}

float EyePattern::GetVoltageRange(size_t /*stream*/)
{
	if(m_parameters[m_vmodeName].GetIntVal() == RANGE_AUTO)
		return m_inputs[0].GetVoltageRange();
	else
		return m_parameters[m_rangeName].GetFloatVal();
}

float EyePattern::GetOffset(size_t /*stream*/)
{
	return -m_parameters[m_centerName].GetFloatVal();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EyePattern::ClearSweeps()
{
	SetData(NULL, 0);
}

void EyePattern::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	LogIndenter li;

	if(!VerifyAllInputsOK())
	{
		//if input goes momentarily bad, don't delete output - just stop updating
		//SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto waveform = GetInputWaveform(0);
	auto clock = GetInputWaveform(1);

	waveform->PrepareForCpuAccess();
	clock->PrepareForCpuAccess();

	SetYAxisUnits(GetInput(0).GetYAxisUnits(), 0);

	//If center of the eye was changed, reset existing eye data
	auto cap = dynamic_cast<EyeWaveform*>(GetData(0));
	double center = m_parameters[m_centerName].GetFloatVal();
	if(cap)
	{
		if(fabs(cap->GetCenterVoltage() - center) > 0.001)
		{
			SetData(NULL, 0);
			cap = NULL;
		}
	}

	//If clock alignment was changed, reset existing eye data
	ClockAlignment clock_align = static_cast<ClockAlignment>(m_parameters[m_clockAlignName].GetIntVal());
	if(m_lastClockAlign != clock_align)
	{
		SetData(NULL, 0);
		cap = NULL;
		m_lastClockAlign = clock_align;
	}

	//Load the mask, if needed
	string maskpath = m_parameters[m_maskName].GetFileName();
	if(maskpath != m_mask.GetFileName())
		m_mask.Load(maskpath);

	//Initialize the capture
	//TODO: timestamps? do we need those?
	if(cap == NULL)
		cap = ReallocateWaveform();
	cap->m_saturationLevel = m_parameters[m_saturationName].GetFloatVal();
	int64_t* data = cap->GetAccumData();

	//Find all toggles in the clock
	vector<int64_t> clock_edges;
	auto sclk = dynamic_cast<SparseDigitalWaveform*>(clock);
	auto uclk = dynamic_cast<UniformDigitalWaveform*>(clock);
	switch(m_parameters[m_polarityName].GetIntVal())
	{
		case CLOCK_RISING:
			FindRisingEdges(sclk, uclk, clock_edges);
			break;

		case CLOCK_FALLING:
			FindFallingEdges(sclk, uclk, clock_edges);
			break;

		case CLOCK_BOTH:
			FindZeroCrossings(sclk, uclk, clock_edges);
			break;
	}

	//If no clock edges, don't change anything
	if(clock_edges.empty())
		return;

	//Calculate the nominal UI width
	if(cap->m_uiWidth < FLT_EPSILON)
		RecalculateUIWidth();

	//Shift the clock by half a UI if it's edge aligned
	//All of the eye creation logic assumes a center aligned clock.
	if(clock_align == ALIGN_EDGE)
	{
		for(size_t i=0; i<clock_edges.size(); i++)
			clock_edges[i] += cap->m_uiWidth / 2;
	}

	//Recompute scales
	float eye_width_fs = 2 * cap->m_uiWidth;
	m_xscale = m_width * 1.0 / eye_width_fs;
	m_xoff = -round(cap->m_uiWidth);

	//Precompute some scaling factors
	float yscale = m_height / GetVoltageRange(0);
	float ymid = m_height / 2;
	float yoff = -center*yscale + ymid;
	float xtimescale = waveform->m_timescale * m_xscale;

	//Process the eye
	size_t cend = clock_edges.size() - 1;
	size_t wend = waveform->size()-1;
	int32_t ymax = m_height - 1;
	int32_t xmax = m_width - 1;
	auto swfm = dynamic_cast<SparseAnalogWaveform*>(waveform);
	auto uwfm = dynamic_cast<UniformAnalogWaveform*>(waveform);
	if(m_xscale > FLT_EPSILON)
	{
		//Optimized inner loop for uniformly sampled waveforms
		if(uwfm)
		{
			#ifdef __x86_64__
			if(g_hasAvx2)
				DensePackedInnerLoopAVX2(uwfm, clock_edges, data, wend, cend, xmax, ymax, xtimescale, yscale, yoff);
			else
			#endif
				DensePackedInnerLoop(uwfm, clock_edges, data, wend, cend, xmax, ymax, xtimescale, yscale, yoff);
		}

		//Normal main loop
		else
			SparsePackedInnerLoop(swfm, clock_edges, data, wend, cend, xmax, ymax, xtimescale, yscale, yoff);
	}

	//Count total number of UIs we've integrated
	cap->IntegrateUIs(clock_edges.size(), waveform->size());
	cap->Normalize();
	m_streams[2].m_value = cap->GetTotalUIs();
	m_streams[3].m_value = cap->GetTotalSamples();

	//If we have an eye mask, prepare it for processing
	if(m_mask.GetFileName() != "")
		DoMaskTest(cap);
}

#ifdef __x86_64__
__attribute__((target("avx2")))
void EyePattern::DensePackedInnerLoopAVX2(
	UniformAnalogWaveform* waveform,
	vector<int64_t>& clock_edges,
	int64_t* data,
	size_t wend,
	size_t cend,
	int32_t xmax,
	int32_t ymax,
	float xtimescale,
	float yscale,
	float yoff
	)
{
	auto cap = dynamic_cast<EyeWaveform*>(GetData(0));
	int64_t width = cap->GetUIWidth();
	int64_t halfwidth = width/2;

	size_t iclock = 0;

	size_t wend_rounded = wend - (wend % 8);

	//Splat some constants into vector regs
	__m256i vxoff 		= _mm256_set1_epi32((int)m_xoff);
	__m256 vxscale 		= _mm256_set1_ps(m_xscale);
	__m256 vxtimescale	= _mm256_set1_ps(xtimescale);
	__m256 vyoff 		= _mm256_set1_ps(yoff);
	__m256 vyscale 		= _mm256_set1_ps(yscale);
	__m256 vaccum		= _mm256_set1_ps(EYE_ACCUM_SCALE);
	__m256i vwidth		= _mm256_set1_epi32(m_width);

	float* samples = (float*)&waveform->m_samples[0];

	//Main unrolled loop, 8 samples per iteration
	size_t i = 0;
	uint32_t bufmax = m_width * (m_height - 1);
	for(; i<wend_rounded && iclock < cend; i+= 8)
	{
		//Figure out timestamp of this sample within the UI.
		//This doesn't vectorize well, but it's pretty fast.
		int32_t offset[8] __attribute__((aligned(32))) = {0};
		for(size_t j=0; j<8; j++)
		{
			size_t k = i+j;

			//Find time of this sample.
			//If it's past the end of the current UI, move to the next clock edge
			int64_t tstart = k * waveform->m_timescale + waveform->m_triggerPhase;
			offset[j] = tstart - clock_edges[iclock];
			if(offset[j] < 0)
				continue;
			size_t nextclk = iclock + 1;
			int64_t tnext = clock_edges[nextclk];
			if(tstart >= tnext)
			{
				//Move to the next clock edge
				iclock ++;
				if(iclock >= cend)
					break;

				//Figure out the offset to the next edge
				offset[j] = tstart - tnext;
			}

			//Drop anything past half a UI if the next clock edge is a long ways out
			//(this is needed for irregularly sampled data like DDR RAM)
			int64_t ttnext = tnext - tstart;
			if( (offset[j] > halfwidth) && (ttnext > width) )
				offset[j] = -INT_MAX;
		}

		//Interpolate X position
		__m256i voffset		= _mm256_load_si256((__m256i*)offset);
		voffset 			= _mm256_sub_epi32(voffset, vxoff);
		__m256 foffset		= _mm256_cvtepi32_ps(voffset);
		foffset				= _mm256_mul_ps(foffset, vxscale);
		__m256 fround		= _mm256_round_ps(foffset, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
		__m256 fdx			= _mm256_sub_ps(foffset, fround);
		fdx					= _mm256_div_ps(fdx, vxtimescale);
		__m256 vxfloor		= _mm256_floor_ps(foffset);
		__m256i vxfloori	= _mm256_cvtps_epi32(vxfloor);

		//Load waveform data
		__m256 vcur			= _mm256_loadu_ps(samples + i);
		__m256 vnext		= _mm256_loadu_ps(samples + i + 1);

		//Interpolate voltage
		__m256 vdv			= _mm256_sub_ps(vnext, vcur);
		__m256 ynom			= _mm256_mul_ps(vdv, fdx);
		ynom				= _mm256_add_ps(vcur, ynom);
		ynom				= _mm256_mul_ps(ynom, vyscale);
		ynom				= _mm256_add_ps(ynom, vyoff);
		__m256 vyfloor		= _mm256_floor_ps(ynom);
		__m256 vyfrac		= _mm256_sub_ps(ynom, vyfloor);
		__m256i vyfloori	= _mm256_cvtps_epi32(vyfloor);

		//Calculate how much of the pixel's intensity to put in each row
		__m256 vbin2f		= _mm256_mul_ps(vyfrac, vaccum);
		__m256i vbin2i		= _mm256_cvtps_epi32(vbin2f);

		//Final address calculation
		__m256i voff		= _mm256_mullo_epi32(vyfloori, vwidth);
		voff				= _mm256_add_epi32(voff, vxfloori);

		//Save stuff for output loop
		int32_t pixel_x_round[8]	__attribute__((aligned(32)));
		int32_t bin2[8]				__attribute__((aligned(32)));
		uint32_t off[8]				__attribute__((aligned(32)));
		_mm256_store_si256((__m256i*)pixel_x_round, vxfloori);
		_mm256_store_si256((__m256i*)bin2, vbin2i);
		_mm256_store_si256((__m256i*)off, voff);

		//Final output loop. Doesn't vectorize well
		for(size_t j=0; j<8; j++)
		{
			//Abort if this pixel is out of bounds
			if( (pixel_x_round[j] > xmax) || (off[j] >= bufmax) )
				continue;

			//Plot each point (this only draws the right half of the eye, we copy to the left later)
			data[off[j]]	 		+= EYE_ACCUM_SCALE - bin2[j];
			data[off[j] + m_width]	+= bin2[j];
		}
	}

	//Catch any stragglers
	for(; i<wend && iclock < cend; i++)
	{
		//Find time of this sample.
		//If it's past the end of the current UI, move to the next clock edge
		int64_t tstart = i * waveform->m_timescale + waveform->m_triggerPhase;
		int64_t offset = tstart - clock_edges[iclock];
		if(offset < 0)
			continue;
		size_t nextclk = iclock + 1;
		int64_t tnext = clock_edges[nextclk];
		if(tstart >= tnext)
		{
			//Move to the next clock edge
			iclock ++;
			if(iclock >= cend)
				break;

			//Figure out the offset to the next edge
			offset = tstart - tnext;
		}

		//Interpolate position
		float pixel_x_f = (offset - m_xoff) * m_xscale;
		float pixel_x_fround = floor(pixel_x_f);
		float dx_frac = (pixel_x_f - pixel_x_fround ) / xtimescale;

		//Early out if off end of plot
		int32_t pixel_x_round = floor(pixel_x_f);
		if(pixel_x_round > xmax)
			continue;

		//Drop anything past half a UI if the next clock edge is a long ways out
		//(this is needed for irregularly sampled data like DDR RAM)
		int64_t ttnext = tnext - tstart;
		if( (offset > halfwidth) && (ttnext > width) )
			continue;

		//Interpolate voltage, early out if clipping
		float dv = waveform->m_samples[i+1] - waveform->m_samples[i];
		float nominal_voltage = waveform->m_samples[i] + dv*dx_frac;
		float nominal_pixel_y = nominal_voltage*yscale + yoff;
		int32_t y1 = static_cast<int32_t>(nominal_pixel_y);
		if( (y1 >= ymax) || (y1 < 0) )
			continue;

		//Calculate how much of the pixel's intensity to put in each row
		float yfrac = nominal_pixel_y - floor(nominal_pixel_y);
		int32_t bin2 = yfrac * EYE_ACCUM_SCALE;
		int64_t* pix = data + y1*m_width + pixel_x_round;

		//Plot each point (this only draws the right half of the eye, we copy to the left later)
		pix[0] 		 += EYE_ACCUM_SCALE - bin2;
		pix[m_width] += bin2;
	}
}
#endif /* __x86_64__ */

void EyePattern::DensePackedInnerLoop(
	UniformAnalogWaveform* waveform,
	vector<int64_t>& clock_edges,
	int64_t* data,
	size_t wend,
	size_t cend,
	int32_t xmax,
	int32_t ymax,
	float xtimescale,
	float yscale,
	float yoff
	)
{
	auto cap = dynamic_cast<EyeWaveform*>(GetData(0));
	int64_t width = cap->GetUIWidth();
	int64_t halfwidth = width/2;

	size_t iclock = 0;
	for(size_t i=0; i<wend && iclock < cend; i++)
	{
		//Find time of this sample.
		//If it's past the end of the current UI, move to the next clock edge
		int64_t tstart = i * waveform->m_timescale + waveform->m_triggerPhase;
		int64_t offset = tstart - clock_edges[iclock];
		if(offset < 0)
			continue;
		size_t nextclk = iclock + 1;
		int64_t tnext = clock_edges[nextclk];
		if(tstart >= tnext)
		{
			//Move to the next clock edge
			iclock ++;
			if(iclock >= cend)
				break;

			//Figure out the offset to the next edge
			offset = tstart - tnext;
		}

		//Interpolate position
		float pixel_x_f = (offset - m_xoff) * m_xscale;
		float pixel_x_fround = floor(pixel_x_f);
		float dx_frac = (pixel_x_f - pixel_x_fround ) / xtimescale;

		//Drop anything past half a UI if the next clock edge is a long ways out
		//(this is needed for irregularly sampled data like DDR RAM)
		int64_t ttnext = tnext - tstart;
		if( (offset > halfwidth) && (ttnext > width) )
			continue;

		//Early out if off end of plot
		int32_t pixel_x_round = floor(pixel_x_f);
		if(pixel_x_round > xmax)
			continue;

		//Interpolate voltage, early out if clipping
		float dv = waveform->m_samples[i+1] - waveform->m_samples[i];
		float nominal_voltage = waveform->m_samples[i] + dv*dx_frac;
		float nominal_pixel_y = nominal_voltage*yscale + yoff;
		int32_t y1 = static_cast<int32_t>(nominal_pixel_y);
		if( (y1 >= ymax) || (y1 < 0) )
			continue;

		//Calculate how much of the pixel's intensity to put in each row
		float yfrac = nominal_pixel_y - floor(nominal_pixel_y);
		int32_t bin2 = yfrac * 64;
		int64_t* pix = data + y1*m_width + pixel_x_round;

		//Plot each point (this only draws the right half of the eye, we copy to the left later)
		pix[0] 		 += 64 - bin2;
		pix[m_width] += bin2;
	}
}

void EyePattern::SparsePackedInnerLoop(
	SparseAnalogWaveform* waveform,
	vector<int64_t>& clock_edges,
	int64_t* data,
	size_t wend,
	size_t cend,
	int32_t xmax,
	int32_t ymax,
	float xtimescale,
	float yscale,
	float yoff
	)
{
	auto cap = dynamic_cast<EyeWaveform*>(GetData(0));
	int64_t width = cap->GetUIWidth();
	int64_t halfwidth = width/2;

	size_t iclock = 0;
	for(size_t i=0; i<wend && iclock < cend; i++)
	{
		//Find time of this sample.
		//If it's past the end of the current UI, move to the next clock edge
		int64_t tstart = waveform->m_offsets[i] * waveform->m_timescale + waveform->m_triggerPhase;
		int64_t offset = tstart - clock_edges[iclock];
		if(offset < 0)
			continue;
		size_t nextclk = iclock + 1;
		int64_t tnext = clock_edges[nextclk];
		if(tstart >= tnext)
		{
			//Move to the next clock edge
			iclock ++;
			if(iclock >= cend)
				break;

			//Figure out the offset to the next edge
			offset = tstart - tnext;
		}

		//Drop anything past half a UI if the next clock edge is a long ways out
		//(this is needed for irregularly sampled data like DDR RAM)
		int64_t ttnext = tnext - tstart;
		if( (offset > halfwidth) && (ttnext > width) )
			continue;

		//Interpolate position
		int64_t dt = waveform->m_offsets[i+1] - waveform->m_offsets[i];
		float pixel_x_f = (offset - m_xoff) * m_xscale;
		float pixel_x_fround = floor(pixel_x_f);
		float dx_frac = (pixel_x_f - pixel_x_fround ) / (dt * xtimescale );

		//Early out if off end of plot
		int32_t pixel_x_round = floor(pixel_x_f);
		if(pixel_x_round > xmax)
			continue;

		//Interpolate voltage, early out if clipping
		float dv = waveform->m_samples[i+1] - waveform->m_samples[i];
		float nominal_voltage = waveform->m_samples[i] + dv*dx_frac;
		float nominal_pixel_y = nominal_voltage*yscale + yoff;
		int32_t y1 = static_cast<int32_t>(nominal_pixel_y);
		if( (y1 >= ymax) || (y1 < 0) )
			continue;

		//Calculate how much of the pixel's intensity to put in each row
		float yfrac = nominal_pixel_y - floor(nominal_pixel_y);
		int32_t bin2 = yfrac * 64;
		int64_t* pix = data + y1*m_width + pixel_x_round;

		//Plot each point (this only draws the right half of the eye, we copy to the left later)
		pix[0] 		 += 64 - bin2;
		pix[m_width] += bin2;
	}
}

EyeWaveform* EyePattern::ReallocateWaveform()
{
	auto cap = new EyeWaveform(m_width, m_height, m_parameters[m_centerName].GetFloatVal(), EyeWaveform::EYE_NORMAL);
	cap->m_timescale = 1;
	SetData(cap, 0);
	return cap;
}

void EyePattern::RecalculateUIWidth()
{
	auto cap = dynamic_cast<EyeWaveform*>(GetData(0));
	if(!cap)
		cap = ReallocateWaveform();

	//If manual override, don't look at anything else
	if(m_parameters[m_rateModeName].GetIntVal() == MODE_FIXED)
	{
		cap->m_uiWidth = FS_PER_SECOND * 1.0 / m_parameters[m_rateName].GetIntVal();
		return;
	}

	auto clock = GetInputWaveform(1);
	if(!clock)
		return;

	//Find all toggles in the clock
	vector<int64_t> clock_edges;
	auto sclk = dynamic_cast<SparseDigitalWaveform*>(clock);
	auto uclk = dynamic_cast<UniformDigitalWaveform*>(clock);
	switch(m_parameters[m_polarityName].GetIntVal())
	{
		case CLOCK_RISING:
			FindRisingEdges(sclk, uclk, clock_edges);
			break;

		case CLOCK_FALLING:
			FindFallingEdges(sclk, uclk, clock_edges);
			break;

		case CLOCK_BOTH:
			FindZeroCrossings(sclk, uclk, clock_edges);
			break;
	}

	//If no clock edges, don't change anything
	if(clock_edges.empty())
		return;

	//Find width of each UI
	vector<int64_t> ui_widths;
	for(size_t i=0; i<clock_edges.size()-1; i++)
		ui_widths.push_back(clock_edges[i+1] - clock_edges[i]);

	//Need to average at least ten UIs to get meaningful data
	size_t nuis = ui_widths.size();
	if(nuis > 10)
	{
		//Sort, discard the top and bottom 10%, and average the rest to calculate nominal width
		sort(ui_widths.begin(), ui_widths.end());
		size_t navg = 0;
		int64_t total = 0;
		for(size_t i = nuis/10; i <= nuis*9/10; i++)
		{
			total += ui_widths[i];
			navg ++;
		}

		cap->m_uiWidth = (1.0 * total) / navg;
	}
}

/**
	@brief Checks the current capture against the eye mask
 */
void EyePattern::DoMaskTest(EyeWaveform* cap)
{
	auto rate = m_mask.CalculateHitRate(
		cap,
		m_width,
		m_height,
		GetVoltageRange(0),
		m_xscale,
		m_xoff);

	m_streams[1].m_value = rate;
	cap->SetMaskHitRate(rate);
}
