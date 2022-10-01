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

#include "scopeprotocols.h"
#include "AreaMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AreaMeasurement::AreaMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("din");

	m_measurement_typename = "Measurement Type";
	m_parameters[m_measurement_typename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_measurement_typename].AddEnumValue("Average", AVERAGE_AREA);
	m_parameters[m_measurement_typename].AddEnumValue("Per Cycle", CYCLE_AREA);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool AreaMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string AreaMeasurement::GetProtocolName()
{
	return "Area Under Curve";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void AreaMeasurement::Refresh()
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
	auto length = din->size();
	float area = 0;
	float c = 0;

	MeasurementType measurement_type = (MeasurementType)m_parameters[m_measurement_typename].GetIntVal();

	if (measurement_type == AVERAGE_AREA)
	{
		if(uadin)
		{
			//Create the output as a uniform waveform with single sample
			auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0, true);
			cap->PrepareForCpuAccess();

			//Perform summation using Kahan Summation Algorithm
			for (size_t i = 0; i < length; i++)
			{
				float y = (fabs(uadin->m_samples[i]) * din->m_timescale) - c;
				volatile float t = area + y;
				volatile float z = t - area;
				c = z - y;
				area = t;

				cap->m_samples.push_back(area / FS_PER_SECOND);
			}

			SetData(cap, 0);
			cap->MarkModifiedFromCpu();
		}
		else if(sadin)
		{
			//Create the output as a sparse waveform
			auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
			cap->PrepareForCpuAccess();

			//Perform summation using Kahan Summation Algorithm
			for (size_t i = 0; i < length; i++)
			{
				float y = (fabs(uadin->m_samples[i]) * sadin->m_durations[i] * din->m_timescale) - c;
				volatile float t = area + y;
				volatile float z = t - area;
				c = z - y;
				area = t;

				//Push values to the waveform
				cap->m_offsets.push_back(sadin->m_offsets[i]);
				cap->m_durations.push_back(sadin->m_durations[i]);
				cap->m_samples.push_back(area / FS_PER_SECOND);
			}

			SetData(cap, 0);
			cap->MarkModifiedFromCpu();
		}
	}
	else if (measurement_type == CYCLE_AREA)
	{
		float average = GetAvgVoltage(sadin, uadin);
		vector<int64_t> edges;

		//Auto-threshold analog signals at average of the full scale range
		if(uadin)
			FindZeroCrossings(uadin, average, edges);
		else if(sadin)
			FindZeroCrossings(sadin, average, edges);

		//We need at least one full cycle of the waveform
		if(edges.size() < 2)
		{
			SetData(NULL, 0);
			return;
		}

		//Create the output as a sparse waveform
		auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
		cap->PrepareForCpuAccess();

		size_t elen = edges.size();

		//Calculate area for every cycle and put values in the sparse output waveform
		for(size_t i = 0; i < (elen - 2); i += 2)
		{
			//Measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
			int64_t start = edges[i] / din->m_timescale;
			int64_t end = edges[i + 2] / din->m_timescale;
			int64_t j = 0;

			if(uadin)
			{
				//Perform summation using Kahan Summation Algorithm
				for(j = start; (j <= end) && (j < (int64_t)length); j++)
				{
					float y = fabs(uadin->m_samples[j]) - c;
					volatile float t = area + y;
					volatile float z = t - area;
					c = z - y;
					area = t;
				}
			}
			else if(sadin)
			{
				//Perform summation using Kahan Summation Algorithm
				for(j = start; (j <= end) && (j < (int64_t)length); j++)
				{
					float y = ((fabs(sadin->m_samples[j]) * sadin->m_durations[j])) - c;
					volatile float t = area + y;
					volatile float z = t - area;
					c = z - y;
					area = t;
				}
			}

			//Get the difference between the end and start of cycle. This would be the number of samples
			//on which area measurement was performed
			int64_t delta = j - start - 1;

			if (delta != 0)
			{
				//Push values to the waveform
				cap->m_offsets.push_back(start);
				cap->m_durations.push_back(delta);
				cap->m_samples.push_back((area * din->m_timescale)/FS_PER_SECOND);
			}

			area = 0;
		}

		SetData(cap, 0);
		cap->MarkModifiedFromCpu();
	}
}
