/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
#include "USB2PCSDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

USB2PCSDecoder::USB2PCSDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("PMA");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool USB2PCSDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<USB2PMADecoder*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void USB2PCSDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "USB2PCS(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string USB2PCSDecoder::GetProtocolName()
{
	return "USB 1.x/2.0 PCS";
}

bool USB2PCSDecoder::IsOverlay()
{
	return true;
}

bool USB2PCSDecoder::NeedsConfig()
{
	return true;
}

double USB2PCSDecoder::GetVoltageRange()
{
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void USB2PCSDecoder::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<USB2PMAWaveform*>(GetInputWaveform(0));
	size_t len = din->m_samples.size();

	//Make the capture and copy our time scales from the input
	auto cap = new USB2PCSWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	//Initialize the current sample to idle at the start of the capture
	int64_t offset = 0;

	DecodeState state = STATE_IDLE;
	BusSpeed speed = SPEED_1M;
	size_t ui_width = 0;

	//Decode stuff
	size_t count = 0;
	uint8_t data = 0;
	for(size_t i=0; i<len; i++)
	{
		switch(state)
		{
			case STATE_IDLE:
				RefreshIterationIdle(i, state, speed, ui_width, cap, din, count, offset);
				break;

			case STATE_SYNC:
				RefreshIterationSync(i, state, ui_width, cap, din, count, offset, data);
				break;

			case STATE_DATA:
				RefreshIterationData(
					i,
					i-1,
					state,
					ui_width,
					cap,
					din,
					count,
					offset,
					data);
				break;
		}
	}

	//Done
	SetData(cap, 0);
}

void USB2PCSDecoder::RefreshIterationIdle(
	size_t nin,
	DecodeState& state,
	BusSpeed& speed,
	size_t& ui_width,
	USB2PCSWaveform* cap,
	USB2PMAWaveform* din,
	size_t& count,
	int64_t& offset
	)
{
	const size_t ui_width_480 = 2083000;
	const size_t ui_width_12 = 83333000;
	const size_t ui_width_1 = 666666000;

	size_t sample_fs = din->m_durations[nin] * din->m_timescale;
	auto sin = din->m_samples[nin];

	switch(sin.m_type)
	{
		//If the line state is J again, we're still idle. Ignore it.
		case USB2PMASymbol::TYPE_J:
			break;

		//If we go to K, it's the start of a sync symbol.
		case USB2PMASymbol::TYPE_K:

			//Begin the sync
			offset = din->m_offsets[nin];

			//The length of the K indicates our clock speed
			if(sample_fs < (2 * ui_width_480) )
			{
				speed = SPEED_480M;
				ui_width = ui_width_480;
			}
			else if(sample_fs < (2 * ui_width_12) )
			{
				speed = SPEED_12M;
				ui_width = ui_width_12;
			}
			else
			{
				speed = SPEED_1M;
				ui_width = ui_width_1;
			}

			state = STATE_SYNC;
			count = 0;
			break;

		case USB2PMASymbol::TYPE_SE0:
			//TODO: This is either a detach, a reset, or a keepalive (EOP)
			break;

		//SE1 should never occur (error)
		case USB2PMASymbol::TYPE_SE1:

			//Add the error symbol
			cap->m_offsets.push_back(din->m_offsets[nin]);
			cap->m_durations.push_back(din->m_durations[nin]);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_ERROR, 0));
			break;
	}
}

void USB2PCSDecoder::RefreshIterationSync(
	size_t nin,
	DecodeState& state,
	size_t& ui_width,
	USB2PCSWaveform* cap,
	USB2PMAWaveform* din,
	size_t& count,
	int64_t& offset,
	uint8_t& data)
{
	size_t sample_fs = din->m_durations[nin] * din->m_timescale;
	float sample_width_ui = sample_fs * 1.0f / ui_width;

	//Keep track of our position in the sync sequence
	count ++;
	auto sin = din->m_samples[nin];

	//Odd numbered position
	if( (count == 1) || (count == 3) || (count == 5) )
	{
		//Should be one UI long, and a J. Complain if not.
		if( (sample_width_ui > 1.5) || (sample_width_ui < 0.5) ||
			(sin.m_type != USB2PMASymbol::TYPE_J))
		{
			//Sync until the error happened
			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(din->m_offsets[nin] - offset);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_SYNC, 0));

			//Then error symbol for this K
			cap->m_offsets.push_back(din->m_offsets[nin]);
			cap->m_durations.push_back(din->m_durations[nin]);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_ERROR, 0));

			//Go back
			state = STATE_IDLE;
			return;
		}
	}

	//Even numbered position, but not the last
	else if( (count == 2) || (count == 4) )
	{
		//Should be one UI long, and a K. Complain if not.
		if( (sample_width_ui > 1.5) || (sample_width_ui < 0.5) ||
			(sin.m_type != USB2PMASymbol::TYPE_K) )
		{
			//Sync until the error happened
			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(din->m_offsets[nin] - offset);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_SYNC, 0));

			//Then error symbol for this J
			cap->m_offsets.push_back(din->m_offsets[nin]);
			cap->m_durations.push_back(din->m_durations[nin]);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_ERROR, 0));

			//Go back
			state = STATE_IDLE;
			return;
		}
	}

	//Last position
	else
	{
		//Should be a K and at least two UIs long
		if( (sample_width_ui < 1.5) || (sin.m_type != USB2PMASymbol::TYPE_K) )
		{
			//Sync until the error happened
			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(din->m_offsets[nin] - offset);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_SYNC, 0));

			//Then error symbol for this J
			cap->m_offsets.push_back(din->m_offsets[nin]);
			cap->m_durations.push_back(din->m_durations[nin]);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_ERROR, 0));

			state = STATE_IDLE;
			return;
		}

		//If it's two UIs long, the start of the packet is a "0" data bit.
		//We end right at the boundary.
		if(round(sample_width_ui) == 2)
		{
			//Save the sync symbol
			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(din->m_offsets[nin] + din->m_durations[nin] - offset);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_SYNC, 0));

			//New sample starts right after us and contains no data so far
			offset = din->m_offsets[nin] + din->m_durations[nin];
			count = 0;
			data = 0;
		}

		//Packet begins with a "1" bit.
		//Start ends two UIs into the packet.
		else
		{
			//Save the sync symbol
			int64_t pdelta = 2*ui_width / din->m_timescale;
			int64_t pstart = din->m_offsets[nin] + pdelta;
			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(pstart - offset);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_SYNC, 0));

			//Start the new sample and add the 1 bit(s)
			size_t num_ones = round(sample_width_ui) - 2;
			size_t old_width = 2 * ui_width / din->m_timescale;
			offset = pstart + old_width;
			if(num_ones >= 7)	//bitstuff error
			{
				cap->m_offsets.push_back(pstart);
				cap->m_durations.push_back(din->m_durations[nin] - pdelta);
				cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_ERROR, 0));

				count = 0;
			}
			else
			{
				data = 0;

				//Add the ones, LSB to MSB
				for(size_t j=0; j<num_ones; j++)
					data = (data >> 1) | 0x80;
				count = num_ones;
			}
		}

		state = STATE_DATA;
	}
}

void USB2PCSDecoder::RefreshIterationData(
	size_t nin,
	size_t nlast,
	DecodeState& state,
	size_t& ui_width,
	USB2PCSWaveform* cap,
	USB2PMAWaveform* din,
	size_t& count,
	int64_t& offset,
	uint8_t& data)
{
	size_t sample_fs = din->m_durations[nin] * din->m_timescale;
	size_t last_sample_fs = din->m_durations[nlast] * din->m_timescale;
	float sample_width_ui = sample_fs * 1.0f / ui_width;
	float last_sample_width_ui = last_sample_fs * 1.0f / ui_width;

	//If this is a SE0, we're done
	auto sin = din->m_samples[nin];
	if(sin.m_type == USB2PMASymbol::TYPE_SE0)
	{
		//If we're not two UIs long, we have a problem
		//TODO: handle reset
		if(sample_width_ui < 1.2)
		{
			cap->m_offsets.push_back(din->m_offsets[nin]);
			cap->m_durations.push_back(din->m_durations[nin]);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_ERROR, 0));
		}

		//All good
		else
		{
			//Add the end symbol
			cap->m_offsets.push_back(din->m_offsets[nin]);
			cap->m_durations.push_back(din->m_durations[nin] + ui_width/din->m_timescale);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_EOP, 0));
		}
		state = STATE_IDLE;
		count = 0;
		return;
	}

	//SE1 means error
	else if(sin.m_type == USB2PMASymbol::TYPE_SE1)
	{
		//Add the error symbol
		cap->m_offsets.push_back(din->m_offsets[nin]);
		cap->m_durations.push_back(din->m_durations[nin] + ui_width/din->m_timescale);
		cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_ERROR, 0));

		state = STATE_IDLE;
		count = 0;
		return;
	}

	//Process the actual data
	size_t num_bits = round(sample_width_ui);
	size_t last_num_bits = round(last_sample_width_ui);
	for(size_t i=0; i<num_bits; i++)
	{
		//First bit is either a bitstuff or 0 bit
		if(i == 0)
		{
			//If not a bitstuff, add the data bit
			if(last_num_bits < 7)
				data = (data >> 1);

			//else no action needed, it was a bit-stuff.
			else
				continue;
		}

		//All other bits are 1 bits
		else
			data = (data >> 1) | 0x80;

		count ++;

		//If we just finished a byte, save the sample
		if(count == 8)
		{
			//Align our end so it looks nice
			size_t duration = din->m_offsets[nin] - offset;
			if(i+1 == num_bits)
				duration += din->m_durations[nin];

			//No, just move a few UIs over
			else
				duration += (i+1)*ui_width / din->m_timescale;

			cap->m_offsets.push_back(offset);
			cap->m_durations.push_back(duration);
			cap->m_samples.push_back(USB2PCSSymbol(USB2PCSSymbol::TYPE_DATA, data));

			//Start the new sample
			count = 0;
			data = 0;
			offset += duration;
		}
	}
}

Gdk::Color USB2PCSDecoder::GetColor(int i)
{
	auto data = dynamic_cast<USB2PCSWaveform*>(GetData(0));
	if(data == NULL)
		return m_standardColors[COLOR_ERROR];
	if(i >= (int)data->m_samples.size())
		return m_standardColors[COLOR_ERROR];

	//TODO: have a set of standard colors we use everywhere?

	auto sample = data->m_samples[i];
	switch(sample.m_type)
	{
		case USB2PCSSymbol::TYPE_SYNC:
			return m_standardColors[COLOR_PREAMBLE];
		case USB2PCSSymbol::TYPE_EOP:
			return m_standardColors[COLOR_PREAMBLE];
		case USB2PCSSymbol::TYPE_RESET:
			return m_standardColors[COLOR_CONTROL];
		case USB2PCSSymbol::TYPE_DATA:
			return m_standardColors[COLOR_DATA];

		//invalid state, should never happen
		case USB2PCSSymbol::TYPE_ERROR:
		default:
			return m_standardColors[COLOR_ERROR];
	}
}

string USB2PCSDecoder::GetText(int i)
{
	auto data = dynamic_cast<USB2PCSWaveform*>(GetData(0));
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto sample = data->m_samples[i];
	switch(sample.m_type)
	{
		case USB2PCSSymbol::TYPE_SYNC:
			return "SYNC";
		case USB2PCSSymbol::TYPE_EOP:
			return "EOP";
		case USB2PCSSymbol::TYPE_RESET:
			return "RESET";
		case USB2PCSSymbol::TYPE_DATA:
		{
			char tmp[16];
			snprintf(tmp, sizeof(tmp), "%02x", sample.m_data);
			return string(tmp);
		}
		case USB2PCSSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}

	return "";
}
