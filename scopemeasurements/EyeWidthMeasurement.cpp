/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of EyeWidthMeasurement
 */

#include "scopemeasurements.h"
#include "EyeWidthMeasurement.h"
#include "../scopeprotocols/EyeDecoder2.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction/destruction

EyeWidthMeasurement::EyeWidthMeasurement()
	: FloatMeasurement(TYPE_TIME)
{
	//Configure for a single input
	m_signalNames.push_back("Vin");
	m_channels.push_back(NULL);
}

EyeWidthMeasurement::~EyeWidthMeasurement()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

Measurement::MeasurementType EyeWidthMeasurement::GetMeasurementType()
{
	return Measurement::MEAS_HORZ;
}

string EyeWidthMeasurement::GetMeasurementName()
{
	return "Eye Width";
}

bool EyeWidthMeasurement::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && dynamic_cast<EyeDecoder2*>(channel) != NULL )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Measurement processing

bool EyeWidthMeasurement::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
		return false;
	auto chan = dynamic_cast<EyeDecoder2*>(m_channels[0]);
	auto din = dynamic_cast<EyeCapture2*>(chan->GetData());
	if(din == NULL)
		return false;

	float* fdata = din->GetData();
	int64_t w = chan->GetWidth();

	//Measure center 10% of a UI
	int64_t ycenter = chan->GetHeight() / 2;	//vertical midpoint of the eye
	int64_t xcenter = w / 2;					//horizontal midpoint of the eye

	int64_t rad = chan->GetHeight() / 20;		//1/20 of the eye height
	int64_t bot = ycenter - rad/2;
	int64_t top = ycenter + rad/2;

	int64_t left = 0;
	int64_t right = w-1;
	for(int64_t y = bot; y <= top; y++)
	{
		for(int64_t dx = 0; dx < xcenter; dx ++)
		{
			//left
			int64_t x = xcenter - dx;
			if(fdata[y*w + x] > FLT_EPSILON)
				left = max(left, x);

			//right
			x = xcenter + dx;
			if(fdata[y*w + x] > FLT_EPSILON)
				right = min(right, x);
		}
	}

	int64_t dx = right - left;
	double width_ps = 2 * chan->GetUIWidth();
	double ps_per_pixel = width_ps / w;
	int64_t ps = ps_per_pixel * dx;
	m_value = 1.0e-12 * ps;
	return true;
}
