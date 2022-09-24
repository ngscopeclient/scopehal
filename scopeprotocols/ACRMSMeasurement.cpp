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
#include "ACRMSMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ACRMSMeasurement::ACRMSMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("din");

	m_measurement_typename = "Measurement Type";
	m_parameters[m_measurement_typename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_measurement_typename].AddEnumValue("Average", AVERAGE_RMS);
	m_parameters[m_measurement_typename].AddEnumValue("Per Cycle", CYCLE_RMS);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ACRMSMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
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

string ACRMSMeasurement::GetProtocolName()
{
	return "AC RMS";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ACRMSMeasurement::Refresh()
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

	float average = GetAvgVoltage(sadin, uadin);
	auto length = din->size();
	float temp = 0;

	MeasurementType measurement_type = (MeasurementType)m_parameters[m_measurement_typename].GetIntVal();

	if (measurement_type == AVERAGE_RMS)
	{
		//Simply sum the squares of all values after subtracting the DC value
		if(uadin)
		{
			for (size_t i = 0; i < length; i++)
				temp += ((uadin->m_samples[i] - average) * (uadin->m_samples[i] - average));
		}
		else if(sadin)
		{
			for (size_t i = 0; i < length; i++)
				temp += ((sadin->m_samples[i] - average) * (sadin->m_samples[i] - average));
		}

		//Divide by total number of samples
		temp /= length;

		//Take square root to get the final AC RMS Value
		temp = sqrt(temp);

		//Create the output as a uniform waveform with single sample
		auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0, true);
		cap->m_timescale = 1;
		cap->PrepareForCpuAccess();

		//Push AC RMS value
		cap->m_samples.push_back(temp);

		SetData(cap, 0);
		cap->MarkModifiedFromCpu();
	}
	else if (measurement_type == CYCLE_RMS)
	{
		vector<int64_t> edges;

		//Auto-threshold analog signals at average of the full scale range
		if(uadin)
			FindZeroCrossings(uadin, average, edges);
		else if(sadin)
			FindZeroCrossings(sadin, average, edges);

		//We need at least one full cycle of the waveform to have a meaningful AC RMS Measurement
		if(edges.size() < 2)
		{
			SetData(NULL, 0);
			return;
		}

		//Create the output as a sparse waveform
		auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
		cap->PrepareForCpuAccess();

		size_t elen = edges.size();

		for(size_t i = 0; i < (elen - 2); i += 2)
		{
			//Measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
			int64_t start = edges[i] / din->m_timescale;
			int64_t end = edges[i + 2] / din->m_timescale;
			int64_t j = 0;

			//Simply sum the squares of all values in a cycle after subtracting the DC value
			if(uadin)
			{
				for(j = start; (j <= end) && (j < (int64_t)length); j++)
					temp += ((uadin->m_samples[j] - average) * (uadin->m_samples[j] - average));
			}
			else if(sadin)
			{
				for(j = start; (j <= end) && (j < (int64_t)length); j++)
					temp += ((sadin->m_samples[j] - average) * (sadin->m_samples[j] - average));
			}

			//Get the difference between the end and start of cycle. This would be the number of samples
			//on which AC RMS calculation was performed
			int64_t delta = j - start - 1;

			if (delta != 0)
			{
				//Divide by total number of samples for one cycle
				temp /= delta;

				//Take square root to get the final AC RMS Value of one cycle
				temp = sqrt(temp);

				//Push values to the waveform
				cap->m_offsets.push_back(start);
				cap->m_durations.push_back(delta);
				cap->m_samples.push_back(temp);
			}
		}

		SetData(cap, 0);
		cap->MarkModifiedFromCpu();
	}
}
