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
#include "TimeOutsideLevelMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TimeOutsideLevelMeasurement::TimeOutsideLevelMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("din");

	m_highlevel = "High Level";
	m_parameters[m_highlevel] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));

	m_lowlevel = "Low Level";
	m_parameters[m_lowlevel] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));

	m_measurement_typename = "Measurement Type";
	m_parameters[m_measurement_typename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_measurement_typename].AddEnumValue("High Level", HIGH_LEVEL);
	m_parameters[m_measurement_typename].AddEnumValue("Low Level", LOW_LEVEL);
	m_parameters[m_measurement_typename].AddEnumValue("Both", BOTH);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TimeOutsideLevelMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TimeOutsideLevelMeasurement::GetProtocolName()
{
	return "Time Outside Value";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TimeOutsideLevelMeasurement::Refresh()
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

	float highlevel = m_parameters[m_highlevel].GetFloatVal();
	float lowlevel = m_parameters[m_lowlevel].GetFloatVal();

	int64_t hightime = 0;
	int64_t lowtime = 0;

	int64_t temp1 = 0;
	int64_t temp2 = 0;
	size_t length = uadin->m_samples.size();

	MeasurementType measurement_type = (MeasurementType)m_parameters[m_measurement_typename].GetIntVal();

	bool processhigh = (measurement_type == HIGH_LEVEL) || (measurement_type == BOTH);
	bool processlow = (measurement_type == LOW_LEVEL) || (measurement_type == BOTH); 

	if (uadin)
	{
		if (processhigh == true)
		{
			size_t i = 0;

			while (i < length)
			{
				if ((uadin->m_samples[i] > highlevel) && (temp1 == 0))
				{
					temp1 = i;
				}

				if ((((uadin->m_samples[i] <= highlevel) && (temp2 == 0)) || (i == (length - 1))) && (temp1 != 0))
				{
					temp2 = i;
					hightime += (temp2 - temp1);
					temp2 = 0;
					temp1 = 0;
				}

				i++;
			}
		}

		if (processlow == true)
		{
			size_t i = 0;

			while (i < length)
			{
				if ((uadin->m_samples[i] < lowlevel) && (temp1 == 0))
				{
					temp1 = i;
				}

				if ((((uadin->m_samples[i] >= lowlevel) && (temp2 == 0)) || (i == (length - 1))) && (temp1 != 0))
				{
					temp2 = i;
					lowtime += (temp2 - temp1);
					temp2 = 0;
					temp1 = 0;
				}

				i++;
			}
		}
	}
	else if (sadin)
	{
		for(size_t i = 0; i < length; i++)
		{
			if ((processhigh == true) && (sadin->m_samples[i] > highlevel))
			{
				hightime += sadin->m_durations[i];
			}

			if ((processlow == true) && (sadin->m_samples[i] < lowlevel))
			{
				lowtime += sadin->m_durations[i];
			}
		}
	}

	int64_t totaltime = (hightime + lowtime) * din->m_timescale;

	//Create the output
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();
	cap->m_samples.push_back(totaltime);

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
}