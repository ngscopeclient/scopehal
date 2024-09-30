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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of SetupHoldMeasurement
 */

#include "../scopehal/scopehal.h"
#include "SetupHoldMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SetupHoldMeasurement::SetupHoldMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_vih(m_parameters["Vih"])
	, m_vil(m_parameters["Vil"])
	, m_edgemode(m_parameters["Clock Edge"])
{
	AddStream(Unit(Unit::UNIT_FS), "tsetup", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_FS), "thold", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("data");
	CreateInput("clock");

	m_vih = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_vih.SetFloatVal(2.0);

	m_vil = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_vil.SetFloatVal(1.3);

	m_edgemode = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_edgemode.AddEnumValue("Rising", EDGE_RISING);
	m_edgemode.AddEnumValue("Falling", EDGE_FALLING);
	m_edgemode.AddEnumValue("Both", EDGE_BOTH);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SetupHoldMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void SetupHoldMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "SetupHold(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string SetupHoldMeasurement::GetProtocolName()
{
	return "Setup / Hold";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SetupHoldMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		m_streams[0].m_value = 0;
		m_streams[1].m_value = 0;
		return;
	}

	float vih = m_vih.GetFloatVal();
	float vil = m_vil.GetFloatVal();
	auto mode = static_cast<EdgeMode>(m_edgemode.GetIntVal());

	//Get the input data
	auto wdata = GetInputWaveform(0);
	auto wclk = GetInputWaveform(1);
	wdata->PrepareForCpuAccess();
	wclk->PrepareForCpuAccess();

	//For now, assume inputs are always uniform
	auto udata = dynamic_cast<UniformAnalogWaveform*>(wdata);
	auto uclk = dynamic_cast<UniformAnalogWaveform*>(wclk);
	if(!udata || !uclk)
	{
		m_streams[0].m_value = 0;
		m_streams[1].m_value = 0;
		return;
	}

	//Find the timestamps of clock and data edges
	bool clockMatchRising = (mode == EDGE_RISING) || (mode == EDGE_BOTH);
	bool clockMatchFalling = (mode == EDGE_FALLING) || (mode == EDGE_BOTH);
	auto clkedges = GetEdgeTimestamps(uclk, vil, vih, clockMatchRising, clockMatchFalling);
	auto datedges = GetEdgeTimestamps(udata, vil, vih, true, true);

	//Loop over the clock edges and look for data edges before/after each
	size_t nclk = clkedges.size();
	size_t ndat = datedges.size();

	int64_t minsetup = INT64_MAX;
	int64_t minhold = INT64_MAX;
	size_t idat = 0;
	for(size_t iclk = 0; iclk < nclk; iclk ++)
	{
		auto clockStart = clkedges[iclk].first;
		auto clockEnd = clkedges[iclk].second;

		//Search forward to find the last data edge BEFORE our clock edge
		//(used for calculating setup time)
		bool dataFound = false;
		int64_t dataEnd = 0;
		for(; idat < ndat; idat ++)
		{
			//If the data edge ends after our current clock edge starts, stop searching
			auto dstart = datedges[idat].first;
			auto dend = datedges[idat].second;
			if(dend > clockStart)
			{
				//If the data and clock edges overlap, we have no margin at all!
				//More formally: if data start and/or end is between clock start and end, no margin
				if( ( (dstart >= clockStart) && (dstart <= clockEnd) ) ||
					( (dend >= clockStart) && (dend <= clockEnd) ) )
				{
					minsetup = 0;
				}
				break;
			}

			//If it ends before our *previous* clock edge starts, it's too early, keep looking
			if( (iclk > 0) && (clkedges[iclk-1].first > dend) )
				continue;

			//It's a hit, keep it
			dataFound = true;
			dataEnd = dend;
		}
		if(dataFound)
		{
			//Calculate setup time: data valid to clock invalid
			int64_t tsu = clockStart - dataEnd;
			/*LogDebug("Data valid at %s, clock invalid at %s, setup time = %s\n",
				Unit(Unit::UNIT_FS).PrettyPrint(dataEnd).c_str(),
				Unit(Unit::UNIT_FS).PrettyPrint(clockStart).c_str(),
				Unit(Unit::UNIT_FS).PrettyPrint(tsu).c_str());*/
			minsetup = min(tsu, minsetup);

			//TODO: waveform output?
		}

		//Continue searching forward to find the first data edge AFTER the clock edge
		dataFound = false;
		int64_t dataStart = 0;
		for(; idat < ndat; idat ++)
		{
			//If the data and clock edges overlap, we have no margin at all!
			//More formally: if data start and/or end is between clock start and end, no margin
			auto dstart = datedges[idat].first;
			auto dend = datedges[idat].second;
			if( ( (dstart >= clockStart) && (dstart <= clockEnd) ) ||
				( (dend >= clockStart) && (dend <= clockEnd) ) )
			{
				minhold = 0;
				break;
			}

			//If the data edge starts after our current clock edge ends, stop searching
			if(dstart > clockEnd)
			{
				//If the data edge starts after the *next* clock edge starts, it's outside our UI
				//Ignore it
				if(iclk+1 < nclk)
				{
					if(dstart > clkedges[iclk+1].first)
						break;
				}

				dataFound = true;
				dataStart = dstart;
				break;
			}
		}

		if(dataFound)
		{
			//Calculate hold time: clock valid to data invalid
			int64_t th = dataStart - clockEnd;
			/*LogDebug("Clock valid at %s, data invalid at %s, hold time = %s\n",
				Unit(Unit::UNIT_FS).PrettyPrint(clockEnd).c_str(),
				Unit(Unit::UNIT_FS).PrettyPrint(dataStart).c_str(),
				Unit(Unit::UNIT_FS).PrettyPrint(th).c_str());*/
			minhold = min(th, minhold);

			//TODO: waveform output?
		}

	}

	m_streams[0].m_value = minsetup;
	m_streams[1].m_value = minhold;
}

/**
	@brief Returns a vector of (edge start, edge end) timestamps

	@param wfm				Input signal
	@param vil				Logic low threshold
	@param vih				Logic high threshold
	@param matchRising		True to match rising edges
	@param matchFalling		True to match falling edges
 */
vector< pair<int64_t, int64_t> > SetupHoldMeasurement::GetEdgeTimestamps(
	UniformAnalogWaveform* wfm,
	float vil,
	float vih,
	bool matchRising,
	bool matchFalling)
{
	vector< pair<int64_t, int64_t> > ret;

	enum bitstate_t
	{
		STATE_UNKNOWN_WAS_LOW,
		STATE_UNKNOWN_WAS_HIGH,
		STATE_LOW,
		STATE_HIGH,
	};

	//Assign the first state
	bitstate_t state;
	if(wfm->m_samples[0] < vil)
		state = STATE_LOW;
	else if(wfm->m_samples[0] > vih)
		state = STATE_HIGH;
	else
		state = STATE_UNKNOWN_WAS_LOW;

	//Main loop looking for edges
	int64_t edgestart = 0;
	auto size = wfm->size();
	for(size_t i = 1; i < size; i ++)
	{
		float vin = wfm->m_samples[i];
		int64_t tstamp = GetOffsetScaled(wfm, i);

		float lerpHi = InterpolateTime(wfm, i-1, vih);
		float lerpLo = InterpolateTime(wfm, i-1, vil);

		int64_t thi = tstamp + lerpHi * wfm->m_timescale;
		int64_t tlo = tstamp + lerpLo * wfm->m_timescale;

		switch(state)
		{
			//Look for rising edges
			case STATE_UNKNOWN_WAS_LOW:

				if(vin > vih)
				{
					if(matchRising)
						ret.push_back( pair<int64_t, int64_t>(edgestart, thi));

					state = STATE_HIGH;
				}

				break;

			case STATE_UNKNOWN_WAS_HIGH:

				if(vin < vil)
				{
					if(matchFalling)
						ret.push_back( pair<int64_t, int64_t>(edgestart, tlo));

					state = STATE_LOW;
				}

				break;

			case STATE_LOW:

				//Look for Vil level crossing
				if(vin > vil)
				{
					state = STATE_UNKNOWN_WAS_LOW;
					edgestart = tlo;
				}

				break;

			case STATE_HIGH:

				//Look for Vih level crossing
				if(vin < vih)
				{
					state = STATE_UNKNOWN_WAS_HIGH;
					edgestart = thi;
				}

				break;
		}
	}

	return ret;
}
