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
#include "J1939PDUDecoder.h"
#include "J1939AnalogDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

J1939AnalogDecoder::J1939AnalogDecoder(const string& color)
	: Filter(color, CAT_BUS)
	, m_initValue("Initial Value")
	, m_pgn("PGN")
	, m_bitpos("Starting Bit")
	, m_unit("Unit")
	, m_scale("Scale")
	, m_offset("Offset")
	, m_format("Format")
	, m_scalemode("Scale mode")
{
	AddStream(Unit(Unit::UNIT_COUNTS), "data", Stream::STREAM_TYPE_ANALOG);

	CreateInput("j1939");

	m_parameters[m_initValue] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_initValue].SetIntVal(0);

	m_parameters[m_pgn] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_pgn].SetIntVal(0);

	m_parameters[m_bitpos] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_bitpos].SetIntVal(0);

	m_parameters[m_offset] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_offset].SetFloatVal(0);

	m_parameters[m_scale] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_scale].SetFloatVal(1);

	m_parameters[m_unit] = FilterParameter::UnitSelector();
	m_parameters[m_unit].SetIntVal(Unit::UNIT_COUNTS);
	m_parameters[m_unit].signal_changed().connect(sigc::mem_fun(*this, &J1939AnalogDecoder::OnUnitChanged));

	m_parameters[m_format] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_format].AddEnumValue("Unsigned 16-bit", FORMAT_UINT16);
	m_parameters[m_format].AddEnumValue("Signed 16-bit", FORMAT_INT16);
	m_parameters[m_format].AddEnumValue("Unsigned 8-bit", FORMAT_UINT8);
	m_parameters[m_format].AddEnumValue("Signed 8-bit", FORMAT_INT8);
	m_parameters[m_format].SetIntVal(FORMAT_UINT16);

	m_parameters[m_scalemode] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_scalemode].AddEnumValue("Multiply", SCALE_MULT);
	m_parameters[m_scalemode].AddEnumValue("Divide", SCALE_DIV);
	m_parameters[m_scalemode].SetIntVal(FORMAT_UINT16);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool J1939AnalogDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<J1939PDUWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string J1939AnalogDecoder::GetProtocolName()
{
	return "J1939 Analog";
}

Filter::DataLocation J1939AnalogDecoder::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void J1939AnalogDecoder::OnUnitChanged()
{
	Unit unit(static_cast<Unit::UnitType>(m_parameters[m_unit].GetIntVal()));

	SetYAxisUnits(unit, 0);
	m_parameters[m_offset].SetUnit(unit);
	m_parameters[m_scale].SetUnit(unit);
}

void J1939AnalogDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("J1939AnalogDecoder::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearErrors();
	auto din = dynamic_cast<J1939PDUWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");
		else
			AddErrorMessage("Invalid input", "Expected a J1939 PDU waveform");

		SetData(nullptr, 0);
		return;
	}

	auto len = din->size();

	//Make output waveform
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_DATA,
	} state = STATE_IDLE;

	//Initial sample at time zero
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(0);
	cap->m_samples.push_back(m_parameters[m_initValue].GetFloatVal());

	auto format = static_cast<format_t>(m_parameters[m_format].GetIntVal());
	auto scalemode = static_cast<scalemode_t>(m_parameters[m_scalemode].GetIntVal());
	auto bitpos = m_parameters[m_bitpos].GetIntVal();
	auto scale = m_parameters[m_scale].GetFloatVal();
	auto offset = m_parameters[m_offset].GetFloatVal();
	auto targetaddr = m_parameters[m_pgn].GetIntVal();
	if(scalemode == SCALE_DIV)
		scale = 1.0 / scale;

	//TODO: support >8 byte packets
	int64_t framestart = 0;
	uint64_t payload = 0;
	for(size_t i=0; i<len; i++)
	{
		auto& s = din->m_samples[i];

		switch(state)
		{
			//Look for a PGN (ignore anything else)
			case STATE_IDLE:
				if(s.m_stype == J1939PDUSymbol::TYPE_PGN)
				{
					//ID match?
					if(targetaddr == s.m_data)
					{
						framestart = din->m_offsets[i] * din->m_timescale;
						payload = 0;
						state = STATE_DATA;
					}

					//otherwise ignore the frame, not interesting
				}
				break;

			//Read the actual data bytes, MSB first
			case STATE_DATA:
				if(s.m_stype == J1939PDUSymbol::TYPE_DATA)
				{
					//Grab the data byte
					payload = (payload << 8) | s.m_data;

					//Extend the previous sample to the start of this frame
					size_t nlast = cap->m_offsets.size() - 1;
					cap->m_durations[nlast] = framestart - cap->m_offsets[nlast];
				}

				//Starting a new frame? This one is over, add the new sample
				else if(s.m_stype == J1939PDUSymbol::TYPE_PRI)
				{
					//Set up timing
					cap->m_offsets.push_back(framestart);
					cap->m_durations.push_back(0);

					//Cast appropriately
					float v = 0;
					auto bitval = payload >> bitpos;
					switch(format)
					{
						case FORMAT_UINT16:
							v = bitval & 0xffff;
							break;

						case FORMAT_UINT8:
							v = bitval & 0xff;
							break;

						case FORMAT_INT16:
							v = static_cast<int16_t>(bitval & 0xffff);
							break;

						case FORMAT_INT8:
							v = static_cast<int8_t>(bitval & 0xff);
							break;
					}

					cap->m_samples.push_back((v * scale) + offset);

					state = STATE_IDLE;
				}

				//Ignore anything else
				break;

			default:
				break;
		}

		//If we see a SOF previous frame was truncated, reset
		if(s.m_stype == J1939PDUSymbol::TYPE_PRI)
			state = STATE_IDLE;
	}

	//Extend the last sample to the end of the capture
	size_t nlast = cap->m_offsets.size() - 1;
	cap->m_durations[nlast] = (din->m_offsets[len-1] * din->m_timescale) - cap->m_offsets[nlast];

	//Add three padding samples (do we still have this rendering bug??)
	int64_t tlast = cap->m_offsets[nlast];
	auto vlast = cap->m_samples[nlast];
	for(size_t i=0; i<2; i++)
	{
		cap->m_offsets.push_back(tlast + i);
		cap->m_durations.push_back(1);
		cap->m_samples.push_back(vlast);
	}

	//Done updating
	cap->MarkModifiedFromCpu();
}
