/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "CANDecoder.h"
#include "CANBitmaskFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CANBitmaskFilter::CANBitmaskFilter(const string& color)
	: Filter(color, CAT_BUS)
	, m_initValue("Initial Value")
	, m_busAddress("Bus Address")
	, m_bitmask("Pattern Bitmask")
	, m_pattern("Pattern Target")
{
	AddDigitalStream("data");

	CreateInput("din");

	m_parameters[m_initValue] = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_initValue].SetIntVal(0);

	m_parameters[m_busAddress] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_parameters[m_busAddress].SetIntVal(0);

	m_parameters[m_bitmask] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_parameters[m_bitmask].SetIntVal(0);

	m_parameters[m_pattern] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_parameters[m_pattern].SetIntVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CANBitmaskFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<CANWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CANBitmaskFilter::GetProtocolName()
{
	return "CAN Bitmask";
}

Filter::DataLocation CANBitmaskFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CANBitmaskFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	ClearErrors();
	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}

	auto din = dynamic_cast<CANWaveform*>(GetInputWaveform(0));
	auto len = din->size();

	//Make output waveform
	auto cap = SetupEmptySparseDigitalOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_DLC,
		STATE_DATA
	} state = STATE_IDLE;

	//Initial sample at time zero
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(0);
	cap->m_samples.push_back(static_cast<bool>(m_parameters[m_initValue].GetIntVal()));

	int64_t mask = m_parameters[m_bitmask].GetIntVal();
	int64_t pattern = m_parameters[m_pattern].GetIntVal();
	auto targetaddr = m_parameters[m_busAddress].GetIntVal() ;

	//Process the CAN packet stream
	//TODO: support CAN-FD which can have longer frames (up to 64 bytes)?
	int64_t framestart = 0;
	int64_t payload = 0;
	size_t bytesleft = 0;
	for(size_t i=0; i<len; i++)
	{
		auto& s = din->m_samples[i];

		switch(state)
		{
			//Look for a CAN ID (ignore anything else)
			case STATE_IDLE:
				if(s.m_stype == CANSymbol::TYPE_ID)
				{
					//ID match?
					if(targetaddr == s.m_data)
					{
						framestart = din->m_offsets[i] * din->m_timescale;
						payload = 0;
						state = STATE_DLC;
					}

					//otherwise ignore the frame, not interesting
				}
				break;

			//Look for the DLC so we know how many bytes to read
			case STATE_DLC:
				if(s.m_stype == CANSymbol::TYPE_DLC)
				{
					bytesleft = s.m_data;
					state = STATE_DATA;
				}

				break;

			//Read the actual data bytes, MSB first
			case STATE_DATA:
				if(s.m_stype == CANSymbol::TYPE_DATA)
				{
					//Grab the data byte
					payload = (payload << 8) | s.m_data;

					//Are we done with the frame?
					bytesleft --;
					if(bytesleft == 0)
					{
						//Extend the previous sample to the start of this frame
						size_t nlast = cap->m_offsets.size() - 1;
						cap->m_durations[nlast] = framestart - cap->m_offsets[nlast];

						//Check the bitmask and add a new sample
						cap->m_offsets.push_back(framestart);
						cap->m_durations.push_back(0);

						if( (payload & mask) == pattern )
							cap->m_samples.push_back(true);
						else
							cap->m_samples.push_back(false);

						state = STATE_IDLE;
					}
				}

				//Discard anything else
				else
					state = STATE_IDLE;
				break;
		}

		//If we see a SOF previous frame was truncated, reset
		if(s.m_stype == CANSymbol::TYPE_SOF)
			state = STATE_IDLE;
	}

	//Extend the last sample to the end of the capture
	size_t nlast = cap->m_offsets.size() - 1;
	cap->m_durations[nlast] = (din->m_offsets[len-1] * din->m_timescale) - cap->m_offsets[nlast];

	//Add three padding samples (do we still have this rendering bug??)
	int64_t tlast = cap->m_offsets[nlast];
	bool vlast = cap->m_samples[nlast];
	for(size_t i=0; i<2; i++)
	{
		cap->m_offsets.push_back(tlast + i);
		cap->m_durations.push_back(1);
		cap->m_samples.push_back(vlast);
	}

	//Done updating
	cap->MarkModifiedFromCpu();
}
