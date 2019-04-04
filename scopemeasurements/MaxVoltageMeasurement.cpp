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
	@brief Declaration of MaxVoltageMeasurement
 */

#include "scopemeasurements.h"
#include "MaxVoltageMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction/destruction

MaxVoltageMeasurement::MaxVoltageMeasurement()
{
	//Configure for a single input
	m_signalNames.push_back("Vin");
	m_channels.push_back(NULL);
}

MaxVoltageMeasurement::~MaxVoltageMeasurement()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string MaxVoltageMeasurement::GetMeasurementName()
{
	return "Maximum";
}

bool MaxVoltageMeasurement::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

string MaxVoltageMeasurement::GetValueAsString()
{
	char tmp[128];
	if(m_value > 1)
		snprintf(tmp, sizeof(tmp), "%.3f V", m_value);
	else
		snprintf(tmp, sizeof(tmp), "%.2f mV", m_value * 1000);

	return tmp;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Measurement processing

bool MaxVoltageMeasurement::Refresh()
{
	m_value = FLT_MIN;

	//Get the input data
	if(m_channels[0] == NULL)
		return false;
	AnalogCapture* din = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	if(din == NULL || (din->GetDepth() == 0))
		return false;

	//Loop over samples and find the maximum
	for(auto sample : *din)
	{
		if((float)sample > m_value)
			m_value = sample;
	}

	return true;
}
