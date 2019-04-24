
/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
#include "USB2PacketDecoder.h"
#include "USB2PacketRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

USB2PacketDecoder::USB2PacketDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("State");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* USB2PacketDecoder::CreateRenderer()
{
	return new USB2PacketRenderer(this);
}

bool USB2PacketDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (dynamic_cast<USBLineStateDecoder*>(channel) != NULL) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void USB2PacketDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "USB2Packet(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string USB2PacketDecoder::GetProtocolName()
{
	return "USB 1.x/2.0 Packet";
}

bool USB2PacketDecoder::IsOverlay()
{
	return true;
}

bool USB2PacketDecoder::NeedsConfig()
{
	return true;
}

double USB2PacketDecoder::GetVoltageRange()
{
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void USB2PacketDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	USBLineStateCapture* din = dynamic_cast<USBLineStateCapture*>(m_channels[0]->GetData());
	if( (din == NULL) || (din->GetDepth() == 0) )
	{
		SetData(NULL);
		return;
	}

	//Make the capture and copy our time scales from the input
	USB2PacketCapture* cap = new USB2PacketCapture;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	//Initialize the current sample to idle at the start of the capture
	USB2PacketSample current_sample;
	current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_IDLE;
	current_sample.m_offset = 0;
	current_sample.m_duration = 0;

	DecodeState state = STATE_IDLE;
	BusSpeed speed = SPEED_1M;
	size_t ui_width = 0;

	//Decode stuff
	size_t count = 0;
	for(size_t i=0; i<din->m_samples.size(); i++)
	{
		switch(state)
		{
			case STATE_IDLE:
				RefreshIterationIdle(din->m_samples[i], state, speed, ui_width, cap, din, count, current_sample);
				break;

			case STATE_SYNC:
				RefreshIterationSync(din->m_samples[i], state, ui_width, cap, din, count, current_sample);
				break;

			case STATE_DATA:
				RefreshIterationData(
					din->m_samples[i],
					din->m_samples[i-1],
					state,
					ui_width,
					cap,
					din,
					count,
					current_sample);
				break;
		}
	}

	//Done
	SetData(cap);
}

void USB2PacketDecoder::RefreshIterationIdle(
	const USBLineSample& sin,
	DecodeState& state,
	BusSpeed& speed,
	size_t& ui_width,
	USB2PacketCapture* cap,
	USBLineStateCapture* din,
	size_t& count,
	USB2PacketSample& current_sample)
{
	const size_t ui_width_480 = 2083;
	const size_t ui_width_12 = 83333;
	const size_t ui_width_1 = 666666;

	size_t sample_ps = sin.m_duration * din->m_timescale;

	switch(sin.m_sample.m_type)
	{
		//If the line state is J again, we're still idle.
		//Extend the current sample's length.
		case USBLineSymbol::TYPE_J:
			current_sample.m_duration =	sin.m_offset + sin.m_duration - current_sample.m_offset;
			break;

		//If we go to K, it's the start of a sync symbol.
		case USBLineSymbol::TYPE_K:

			//Save the current sample
			cap->m_samples.push_back(current_sample);

			//Begin the sync
			count = 0;
			current_sample.m_offset = sin.m_offset;
			current_sample.m_duration = sin.m_duration;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_SYNC;

			//The length of the K indicates our clock speed
			if(sample_ps < (2 * ui_width_480) )
			{
				speed = SPEED_480M;
				ui_width = ui_width_480;
			}
			else if(sample_ps < (2 * ui_width_12) )
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
			break;

		case USBLineSymbol::TYPE_SE0:
			//TODO: This is either a detach, a reset, or a keepalive (EOP)
			break;

		//SE1 should never occur (error)
		case USBLineSymbol::TYPE_SE1:

			//Save the previous idle symbol
			cap->m_samples.push_back(current_sample);

			//Add the error symbol
			current_sample.m_offset = sin.m_offset;
			current_sample.m_duration = sin.m_duration;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_ERROR;
			cap->m_samples.push_back(current_sample);

			//Start a new idle symbol
			current_sample.m_offset = sin.m_offset + sin.m_duration;
			current_sample.m_duration = 0;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_IDLE;
			break;
	}
}

void USB2PacketDecoder::RefreshIterationSync(
	const USBLineSample& sin,
	DecodeState& state,
	size_t& ui_width,
	USB2PacketCapture* cap,
	USBLineStateCapture* din,
	size_t& count,
	USB2PacketSample& current_sample)
{
	size_t sample_ps = sin.m_duration * din->m_timescale;
	float sample_width_ui = sample_ps * 1.0f / ui_width;

	//Keep track of our position in the sync sequence
	count ++;

	//Odd numbered position
	if( (count == 1) || (count == 3) || (count == 5) )
	{
		//Should be one UI long, and a J. Complain if not.
		if( (sample_width_ui > 1.5) || (sample_width_ui < 0.5) ||
			(sin.m_sample.m_type != USBLineSymbol::TYPE_J))
		{
			//Save the previous (partial) sync symbol
			cap->m_samples.push_back(current_sample);

			//Add the error symbol
			current_sample.m_offset = sin.m_offset;
			current_sample.m_duration = sin.m_duration;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_ERROR;
			cap->m_samples.push_back(current_sample);

			//Go back
			state = STATE_IDLE;
			return;
		}

		//All good, keep going
		else
			current_sample.m_duration =	sin.m_offset + sin.m_duration - current_sample.m_offset;
	}

	//Even numbered position, but not the last
	else if( (count == 2) || (count == 4) )
	{
		//Should be one UI long, and a K. Complain if not.
		if( (sample_width_ui > 1.5) || (sample_width_ui < 0.5) ||
			(sin.m_sample.m_type != USBLineSymbol::TYPE_K) )
		{
			//Save the previous (partial) sync symbol
			cap->m_samples.push_back(current_sample);

			//Add the error symbol
			current_sample.m_offset = sin.m_offset;
			current_sample.m_duration = sin.m_duration;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_ERROR;
			cap->m_samples.push_back(current_sample);

			state = STATE_IDLE;
			return;
		}

		//All good, keep going
		else
			current_sample.m_duration =	sin.m_offset + sin.m_duration - current_sample.m_offset;
	}

	//Last position
	else
	{
		//Should be a K and at least two UIs long
		if( (sample_width_ui < 1.5) || (sin.m_sample.m_type != USBLineSymbol::TYPE_K) )
		{
			//Save the previous (partial) sync symbol
			cap->m_samples.push_back(current_sample);

			//Add the error symbol
			current_sample.m_offset = sin.m_offset;
			current_sample.m_duration = sin.m_duration;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_ERROR;
			cap->m_samples.push_back(current_sample);

			state = STATE_IDLE;
			return;
		}

		//If it's two UIs long, the start of the packet is a "0" data bit.
		//We end right at the boundary.
		if(round(sample_width_ui) == 2)
		{
			//Save the sync symbol
			current_sample.m_duration =	sin.m_offset + sin.m_duration - current_sample.m_offset;
			cap->m_samples.push_back(current_sample);

			//New sample starts right after us and contains no data so far
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_DATA;
			current_sample.m_offset = sin.m_offset + sin.m_duration;
			current_sample.m_duration = 0;
			current_sample.m_sample.m_data = 0;
			count = 0;

			LogDebug("Start\n");
		}

		//Packet begins with a "1" bit.
		//Start ends two UIs into the packet.
		else
		{
			//Save the sync symbol
			current_sample.m_duration += 2 * ui_width / din->m_timescale;
			cap->m_samples.push_back(current_sample);

			//Start the new sample and add the 1 bit(s)
			size_t num_ones = round(sample_width_ui - 2);
			size_t old_width = 2 * ui_width / din->m_timescale;
			current_sample.m_offset = sin.m_offset + old_width;
			current_sample.m_duration = sin.m_duration - old_width;
			if(num_ones >= 7)
			{
				current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_ERROR;
				cap->m_samples.push_back(current_sample);
				count = 0;
			}
			else
			{
				current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_DATA;
				current_sample.m_sample.m_data = 0;

				//Add the ones, LSB to MSB
				for(size_t j=0; j<num_ones; j++)
					current_sample.m_sample.m_data = (current_sample.m_sample.m_data >> 1) | 0x80;
				count = num_ones;
			}
		}

		state = STATE_DATA;
	}
}

void USB2PacketDecoder::RefreshIterationData(
	const USBLineSample& sin,
	const USBLineSample& slast,
	DecodeState& state,
	size_t& ui_width,
	USB2PacketCapture* cap,
	USBLineStateCapture* din,
	size_t& count,
	USB2PacketSample& current_sample)
{
	size_t sample_ps = sin.m_duration * din->m_timescale;
	size_t last_sample_ps = slast.m_duration * din->m_timescale;
	float sample_width_ui = sample_ps * 1.0f / ui_width;
	float last_sample_width_ui = last_sample_ps * 1.0f / ui_width;

	//If this is a SE0, we're done
	if(sin.m_sample.m_type == USBLineSymbol::TYPE_SE0)
	{
		//If we're not two UIs long, we have a problem
		//TODO: handle reset
		if(round(sample_width_ui) != 2)
		{
			current_sample.m_offset = sin.m_offset;
			current_sample.m_duration = sin.m_duration;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_ERROR;
			cap->m_samples.push_back(current_sample);
		}

		//All good
		else
		{
			//Add the end symbol
			current_sample.m_offset = sin.m_offset;
			current_sample.m_duration = sin.m_duration + ui_width/din->m_timescale;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_EOP;
			cap->m_samples.push_back(current_sample);

			//Start the idle symbol
			current_sample.m_offset = sin.m_offset + sin.m_duration + ui_width/din->m_timescale;
			current_sample.m_duration = 0;
			current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_IDLE;
		}
		state = STATE_IDLE;
		count = 0;
		return;
	}

	//SE1 means error
	else if(sin.m_sample.m_type == USBLineSymbol::TYPE_SE1)
	{
		//Add the error symbol
		current_sample.m_offset = sin.m_offset;
		current_sample.m_duration = sin.m_duration;
		current_sample.m_sample.m_type = USB2PacketSymbol::TYPE_ERROR;
		cap->m_samples.push_back(current_sample);

		state = STATE_IDLE;
		count = 0;
		return;
	}

	//Process the actual data
	size_t num_bits = round(sample_width_ui);
	for(size_t i=0; i<num_bits; i++)
	{
		//First bit is either a bitstuff or 0 bit
		if(i == 0)
		{
			//If not a bitstuff, add the data bit
			if(round(last_sample_width_ui) < 6)
				current_sample.m_sample.m_data = (current_sample.m_sample.m_data >> 1);

			//else no action needed, it was a bit-stuff.
		}

		//All other bits are 1 bits
		else
			current_sample.m_sample.m_data = (current_sample.m_sample.m_data >> 1) | 0x80;

		//If this is the last bit, align our end so it looks nice
		if(i+1 == num_bits)
			current_sample.m_duration = sin.m_offset + sin.m_duration - current_sample.m_offset;

		//No, just move one UI after the current start
		else
			current_sample.m_duration += ui_width / din->m_timescale;

		count ++;

		//If we just finished a byte, save the sample
		if(count == 8)
		{
			cap->m_samples.push_back(current_sample);

			//Start the new sample
			count = 0;
			if(i+1 == num_bits)
				current_sample.m_offset = sin.m_offset + sin.m_duration;
			else
				current_sample.m_offset = sin.m_offset + (i+1)*ui_width/din->m_timescale;
			current_sample.m_sample.m_data = 0;
		}
	}
}
