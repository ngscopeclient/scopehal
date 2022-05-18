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
#include "DramClockFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DramClockFilter::DramClockFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, CAT_CLOCK)
{
	//Set up channels
	ClearStreams();
	AddStream(Unit(Unit::UNIT_COUNTS), "RD");
	AddStream(Unit(Unit::UNIT_COUNTS), "WR");

	//Set up channels
	CreateInput("CMD");
	CreateInput("CLK");
	CreateInput("DQS");

	m_dqsthreshname = "DQS Threshold";
	m_parameters[m_dqsthreshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_dqsthreshname].SetFloatVal(1.6);

	m_burstname = "Burst Length";
	m_parameters[m_burstname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_burstname].AddEnumValue("2", 2);
	m_parameters[m_burstname].AddEnumValue("4", 4);
	m_parameters[m_burstname].AddEnumValue("8", 8);
	m_parameters[m_burstname].SetIntVal(8);

	m_casname = "CAS# Latency";
	m_parameters[m_casname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_casname].SetFloatVal(2);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DramClockFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<SDRAMWaveform*>(stream.m_channel->GetData(stream.m_stream)) != NULL ) )
		return true;
	if( (i == 1) && (dynamic_cast<DigitalWaveform*>(stream.m_channel->GetData(stream.m_stream)) != NULL ) )
		return true;
	if( (i == 2) && (dynamic_cast<AnalogWaveform*>(stream.m_channel->GetData(stream.m_stream)) != NULL ) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DramClockFilter::GetProtocolName()
{
	return "DRAM Clocks";
}

bool DramClockFilter::NeedsConfig()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DramClockFilter::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto cmd = dynamic_cast<SDRAMWaveform*>(GetInputWaveform(0));
	auto clk = GetDigitalInputWaveform(1);
	auto dqs = GetAnalogInputWaveform(2);

	//Find edges in the DQS signal (double rate so we want both polarity)
	//TODO: support differential DQS for DDR2/3
	vector<int64_t> edges;
	float thresh = m_parameters[m_dqsthreshname].GetFloatVal();
	FindZeroCrossings(dqs, thresh, edges);

	//Find edges in the CLK signal
	//TODO: support analog clock too?
	vector<int64_t> clkedges;
	FindZeroCrossings(clk, clkedges);

	//Create output waveforms
	auto rdclk = new DigitalWaveform;
	auto wrclk = new DigitalWaveform;
	rdclk->m_timescale 			= 1;
	wrclk->m_timescale 			= 1;
	SetData(rdclk, 0);
	SetData(wrclk, 1);

	//Copy timestamps
	rdclk->m_startTimestamp 	= dqs->m_startTimestamp;
	wrclk->m_startTimestamp 	= dqs->m_startTimestamp;
	rdclk->m_startFemtoseconds	= dqs->m_startFemtoseconds;
	wrclk->m_startFemtoseconds	= dqs->m_startFemtoseconds;

	//Create initial all-zero samples at start of both clocks
	wrclk->m_samples.push_back(false);
	wrclk->m_durations.push_back(1);
	wrclk->m_offsets.push_back(0);
	rdclk->m_samples.push_back(false);
	rdclk->m_durations.push_back(1);
	rdclk->m_offsets.push_back(0);

	//Extract some parameters
	int bl = m_parameters[m_burstname].GetIntVal();
	float tcas_cycles = m_parameters[m_casname].GetFloatVal();
	int tcas_halfcycles = round(tcas_cycles * 2);

	int64_t tdqs = 0;
	size_t idqs = 0;
	size_t dqslen = edges.size();

	int64_t tclk = 0;
	size_t iclk = 0;
	size_t clklen = clkedges.size();

	Unit fs(Unit::UNIT_FS);

	//Loop over the command bus transactions and find the corresponding DQS pulses for each read/write burst
	size_t len = cmd->m_samples.size();
	for(size_t i=0; i<len; i++)
	{
		int64_t tnow = cmd->m_offsets[i] * cmd->m_timescale + cmd->m_triggerPhase;
		auto s = cmd->m_samples[i];
		switch(s.m_stype)
		{
			//Writes
			case SDRAMSymbol::TYPE_WR:
			case SDRAMSymbol::TYPE_WRA:
				{
					//Find the first DQS edge after the clock edge
					while(idqs < dqslen)
					{
						tdqs = edges[idqs];

						if(tdqs > tnow)
							break;
						else
							idqs ++;
					}

					//Now to add the burst.
					//Create samples for each DQS pulse
					for(int j=0; j<bl; j++)
					{
						//Extend the last sample to our start point
						size_t last = wrclk->m_samples.size() - 1;
						wrclk->m_durations[last] = tdqs - wrclk->m_offsets[last];

						//LogDebug("Write clock pulse at tdqs=%s\n", fs.PrettyPrint(tdqs).c_str());

						//Create a new sample for this pulse
						wrclk->m_samples.push_back(j & 1 ? false : true);
						wrclk->m_durations.push_back(1);
						wrclk->m_offsets.push_back(tdqs);

						//Advance to the next DQS edge
						idqs ++;
						if(idqs >= dqslen)
							break;
						tdqs = edges[idqs];
					}
				}
				break;

			//Reads
			case SDRAMSymbol::TYPE_RD:
			case SDRAMSymbol::TYPE_RDA:
				{
					//Throw away CLK edges until we're lined up with the beginning of the read burst
					while(iclk < clklen)
					{
						tclk = clkedges[iclk];

						if(tclk >= tnow)
							break;
						else
							iclk ++;
					}

					//Move forward by the CAS latency
					iclk += tcas_halfcycles;
					if(iclk >= clklen)
						break;
					tclk = clkedges[iclk];

					//Find the first DQS edge after the clock edge
					//TODO: is this actually correct?
					while(idqs < dqslen)
					{
						tdqs = edges[idqs];

						if(tdqs > tclk)
							break;
						else
							idqs ++;
					}
					if(idqs >= dqslen)
						break;

					//Now to add the burst.
					//Create samples for each DQS pulse
					for(int j=0; j<bl; j++)
					{
						//Extend the last sample to our start point
						size_t last = rdclk->m_samples.size() - 1;
						rdclk->m_durations[last] = tdqs - rdclk->m_offsets[last];

						//LogDebug("Read clock pulse at tdqs=%s\n", fs.PrettyPrint(tdqs).c_str());

						//Create a new sample for this pulse
						rdclk->m_samples.push_back(j & 1 ? false : true);
						rdclk->m_durations.push_back(1);
						rdclk->m_offsets.push_back(tdqs);

						//Advance to the next DQS edge
						idqs ++;
						if(idqs >= dqslen)
							break;
						tdqs = edges[idqs];
					}
				}
				break;

			//Ignore anything else
			default:
				break;
		}
	}

	//Stretch last zero sample to end of capture
	size_t ilast = dqs->m_samples.size() - 1;
	size_t tlast = (dqs->m_offsets[ilast] + dqs->m_durations[ilast])*dqs->m_timescale + dqs->m_triggerPhase;
	size_t last = wrclk->m_samples.size() - 1;
	wrclk->m_durations[last] = tlast - wrclk->m_offsets[last];
	last = rdclk->m_samples.size() - 1;
	rdclk->m_durations[last] = tlast - rdclk->m_offsets[last];

	//Add a bunch of 1fs zero samples to pad end of capture
	for(size_t i=0; i<5; i++)
	{
		last = wrclk->m_samples.size() - 1;
		wrclk->m_samples.push_back(0);
		wrclk->m_durations.push_back(1);
		wrclk->m_offsets.push_back(wrclk->m_offsets[last] + 1);

		last = rdclk->m_samples.size() - 1;
		rdclk->m_samples.push_back(0);
		rdclk->m_durations.push_back(1);
		rdclk->m_offsets.push_back(rdclk->m_offsets[last] + 1);
	}
}
