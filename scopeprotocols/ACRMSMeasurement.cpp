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
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ACRMSMeasurement::GetProtocolName()
{
	return "AC RMS Measurement";
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

	if(uadin)
	{
		for (size_t i = 0; i < length; i++)
		{
			temp += ((uadin->m_samples[i] - average) * (uadin->m_samples[i] - average));
		}
	}
	else if(sadin)
	{
		for (size_t i = 0; i < length; i++)
		{
			temp += ((sadin->m_samples[i] - average) * (sadin->m_samples[i] - average));			
		}
	}

	temp /= length;
	temp = sqrt(temp);

	//Create the output
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	cap->m_samples.push_back(temp);

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
}
