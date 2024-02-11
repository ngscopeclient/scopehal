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
#include "PulseWidthMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PulseWidthMeasurement::PulseWidthMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG, Stream::STREAM_DO_NOT_INTERPOLATE);
	AddStream(Unit(Unit::UNIT_VOLTS), "Amplitude", Stream::STREAM_TYPE_ANALOG, Stream::STREAM_DO_NOT_INTERPOLATE);

	//Set up channels
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PulseWidthMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;

	if( (stream.GetType() == Stream::STREAM_TYPE_ANALOG) ||
		(stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PulseWidthMeasurement::GetProtocolName()
{
	return "Pulse Width";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PulseWidthMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto uadin = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sadin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uddin = dynamic_cast<UniformDigitalWaveform*>(din);
	auto sddin = dynamic_cast<SparseDigitalWaveform*>(din);
	vector<int64_t> edges;
	float average_voltage = 0;
	float max_value;
	size_t temp = 0;

	if(uadin)
		average_voltage = GetAvgVoltage(uadin);
	else if(sadin)
		average_voltage = GetAvgVoltage(sadin);

	//Auto-threshold analog signals at 50% of full scale range
	if(uadin)
		FindZeroCrossings(uadin, average_voltage, edges);
	else if(sadin)
		FindZeroCrossings(sadin, average_voltage, edges);

	//Just find edges in digital signals
	else if(uddin)
		FindZeroCrossings(uddin, edges);
	else
		FindZeroCrossings(sddin, edges);

	//We need at least one full cycle of the waveform to have a meaningful frequency
	if(edges.size() < 2)
	{
		SetData(NULL, 0);
		return;
	}

	//Create the output for pulse width waveform
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	//Create the output for amplitude waveform for analog inputs only
	auto cap1 = (uadin || sadin) ? SetupEmptySparseAnalogOutputWaveform(din, 1, true) : NULL;	
	if(cap1)
	{
		cap1->m_timescale = 1;
		cap1->PrepareForCpuAccess();
	}

	size_t elen = edges.size();
	for(size_t i=0; i < (elen - 2); i+= 2)
	{
		//measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
		int64_t start = edges[i];
		int64_t end = edges[i+1];

		int64_t delta = end - start;

		//Push pulse width information
		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(delta);
		cap->m_samples.push_back(delta);

		// Find amplitude information for the pulses
		if(uadin)
		{
			int64_t start_index = (start - din->m_triggerPhase) / din->m_timescale;
			int64_t end_index = (end - din->m_triggerPhase) / din->m_timescale;
			max_value = average_voltage;

			// Find out the maximum value of the pulse within boundary of the detected pulse
			for (int64_t j = start_index; j < end_index; j++)
			{
				if(uadin->m_samples[j] > max_value)
					max_value = uadin->m_samples[j];
			}

			//Push amplitude information
			cap1->m_offsets.push_back(start);
			cap1->m_durations.push_back(delta);
			cap1->m_samples.push_back(max_value);
		}
		else if (sadin)
		{
			int64_t start_offs = (start - din->m_triggerPhase) / din->m_timescale;
			int64_t end_offs = (end - din->m_triggerPhase) / din->m_timescale;
			max_value = average_voltage;

			//Parse the waveform to get to a detected pulse
			for (size_t j = temp; j < sadin->size(); j++)
			{
				// Find out maximum value of the pulse within boundary of the detected pulse
				if ((sadin->m_offsets[j] >= start_offs) && (sadin->m_offsets[j] <= end_offs))
				{
					if(sadin->m_samples[j] > max_value)
						max_value = sadin->m_samples[j];
				}

				//End of one pulse reached. Record j, so that next time we start from this index for next pulse if any
				else if (sadin->m_offsets[j] > end_offs)
				{
					temp = j;
					break;
				}
			}

			//Push amplitude information
			cap1->m_offsets.push_back(start);
			cap1->m_durations.push_back(delta);
			cap1->m_samples.push_back(max_value);
		}
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();

	if(sadin || uadin)
	{
		//Set amplitude output waveform
		SetData(cap1, 1);
		cap1->MarkModifiedFromCpu();
	}
	else if(uddin || sddin)
	{
		//Switch output waveform to digital
		m_streams[1].m_stype = Stream::STREAM_TYPE_DIGITAL;
		m_streams[1].m_flags = 0;

		//For digital inputs, amplitude information is same as input
		SetData(din, 1);
		din->MarkModifiedFromCpu();
	}

	if (GetVoltageRange(0) == 0)
		SetVoltageRange(10000000000000.0f, 0);
}
