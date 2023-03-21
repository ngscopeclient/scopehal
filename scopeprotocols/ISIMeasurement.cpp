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

#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ISIMeasurement::ISIMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);

	//Set up channels
	CreateInput("DDJ");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ISIMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<DDJMeasurement*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ISIMeasurement::GetProtocolName()
{
	return "ISI";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ISIMeasurement::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		m_streams[0].m_value = 0;
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto ddj = dynamic_cast<DDJMeasurement*>(GetInput(0).m_channel);
	float* table = ddj->GetDDJTable();

	//Check all of the bins and find total jitter for rising and falling edges.
	//Note that the table has LSB most recent, so 10...... is a rising edge and 01...... is a falling edge.
	//We check for zero in case the table is incomplete (this should not drag the mean down).
	float rising_min = FLT_MAX;
	float rising_max = -FLT_MAX;
	float falling_min = FLT_MAX;
	float falling_max = -FLT_MAX;
	for(int i=0; i<256; i++)
	{
		if(table[i] == 0)
			continue;

		if(i & 0x80)
		{
			rising_min = min(rising_min, table[i]);
			rising_max = max(rising_max, table[i]);
		}
		else
		{
			falling_min = min(falling_min, table[i]);
			falling_max = max(falling_max, table[i]);
		}
	}

	float rising_pp = rising_max - falling_min;
	float falling_pp = falling_max - falling_min;

	m_streams[0].m_value = max(rising_pp, falling_pp);
}
