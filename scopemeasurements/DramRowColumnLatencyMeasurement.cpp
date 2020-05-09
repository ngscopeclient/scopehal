/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of DramRowColumnLatencyMeasurement
 */

#include "scopemeasurements.h"
#include "DramRowColumnLatencyMeasurement.h"
#include "../scopeprotocols/DDR3Decoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction/destruction

DramRowColumnLatencyMeasurement::DramRowColumnLatencyMeasurement()
	: FloatMeasurement(TYPE_TIME)
{
	//Configure for a single input
	m_signalNames.push_back("RAM");
	m_channels.push_back(NULL);
}

DramRowColumnLatencyMeasurement::~DramRowColumnLatencyMeasurement()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

Measurement::MeasurementType DramRowColumnLatencyMeasurement::GetMeasurementType()
{
	return Measurement::MEAS_PROTO;
}

string DramRowColumnLatencyMeasurement::GetMeasurementName()
{
	return "DRAM Trcd";
}

bool DramRowColumnLatencyMeasurement::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (dynamic_cast<DDR3Decoder*>(channel) != NULL) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Measurement processing

bool DramRowColumnLatencyMeasurement::Refresh()
{
	m_value = INT_MAX;

	//Get the input data
	if(m_channels[0] == NULL)
		return false;
	DDR3Capture* din = dynamic_cast<DDR3Capture*>(m_channels[0]->GetData());
	if(din == NULL || (din->GetDepth() == 0))
		return false;

	//Measure delay from activating a row in a bank until a read or write to the same bank
	int64_t lastAct[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	for(size_t i=0; i<din->GetDepth(); i++)
	{
		auto sample = din->m_samples[i];

		//Discard invalid bank IDs
		if( (sample.m_sample.m_bank < 0) || (sample.m_sample.m_bank > 8) )
			continue;

		//If it's an activate, update the last activation time
		if(sample.m_sample.m_stype == DDR3Symbol::TYPE_ACT)
			lastAct[sample.m_sample.m_bank] = sample.m_offset * din->m_timescale;

		//If it's a read or write, measure the latency
		else if( (sample.m_sample.m_stype == DDR3Symbol::TYPE_WR) |
			(sample.m_sample.m_stype == DDR3Symbol::TYPE_WRA) |
			(sample.m_sample.m_stype == DDR3Symbol::TYPE_RD) |
			(sample.m_sample.m_stype == DDR3Symbol::TYPE_RDA) )
		{
			int64_t tcol = sample.m_offset * din->m_timescale;

			//If the activate command is before the start of the capture, ignore this event
			int64_t tact = lastAct[sample.m_sample.m_bank];
			if(tact == 0)
				continue;

			//Valid access, measure the latency
			int64_t latency = tcol - tact;
			if(latency < m_value)
				m_value = latency;
		}
	}

	//convert ps to sec
	m_value *= 1e-12f;

	return true;
}
