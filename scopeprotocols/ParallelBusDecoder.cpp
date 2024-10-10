/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "ParallelBusDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ParallelBusDecoder::ParallelBusDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	m_width = 0;
	m_inputCount = 0;
	m_widthname = "Width";
	m_parameters[m_widthname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_widthname].AddEnumValue(" 8 Bits", WIDTH_8BITS); // Keep leading space to have this line first in the list
	m_parameters[m_widthname].AddEnumValue("16 Bits", WIDTH_16BITS);
	m_parameters[m_widthname].AddEnumValue("32 Bits", WIDTH_32BITS);
	m_parameters[m_widthname].AddEnumValue("64 Bits", WIDTH_64BITS);
	m_parameters[m_widthname].SetIntVal(WIDTH_16BITS);
	updateWidth();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

vector<string> ParallelBusDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Length");
	ret.push_back("ASCII");
	return ret;
}

bool ParallelBusDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 16) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ParallelBusDecoder::GetProtocolName()
{
	return "Parallel Bus Decoder";
}

void ParallelBusDecoder::updateWidth()
{
	//Figure out how wide our input is
	int width;
	int widthEnum = m_parameters[m_widthname].GetIntVal();
	switch(widthEnum)
	{
		case WIDTH_8BITS :
			width = 8;
			break;
		case WIDTH_32BITS :
			width = 32;
			break;
		case WIDTH_64BITS :
			width = 64;
			break;
		case WIDTH_16BITS :
		default:
			width = 16;
			break;
	}
	if(width > m_inputCount)
	{	// We can only add inputs, never remove them
		char tmp[32];
		while(m_inputCount < width)
		{
			snprintf(tmp, sizeof(tmp), "din%d", m_inputCount);
			CreateInput(tmp);
			m_inputCount++;
		}
	}
	m_width = width;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ParallelBusDecoder::Refresh()
{
	updateWidth();
	if(m_width <= 0)
	{
		SetData(NULL, 0);
		return;
	}

	//Make sure we have an input for each channel in use
	vector<WaveformBase*> inputs;
	for(int i=0; i<m_width; i++)
	{
		auto din = GetInputWaveform(i);
		if(din != NULL)
		{
			din->PrepareForCpuAccess();
			inputs.push_back(din);
		}
		else if(i == 0)
		{ 	// We need at least one input on the first channel
			SetData(NULL, 0);
			return;
		}
		else
		{
			inputs.push_back(NULL);
		}
	}

	//Get first input
	WaveformBase* din = inputs[0];

	WaveformBase* curDin;
	auto sdin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto udin = dynamic_cast<UniformDigitalWaveform*>(din);

	//Figure out length of the output by finding the longest channel
	int64_t maxOffset = ::GetOffset(sdin,udin,din->size()-1);
	for(int j=1; j<m_width; j++)
	{
		curDin = inputs[j];
		if(curDin != NULL)
		{
			sdin = dynamic_cast<SparseDigitalWaveform*>(curDin);
			udin = dynamic_cast<UniformDigitalWaveform*>(curDin);
			maxOffset = max(maxOffset, ::GetOffset(sdin,udin,curDin->size()-1));
		}
	}

	ClearPackets();

	//Parallel bus processing
	int64_t timeScale = din->m_timescale; 
	int64_t triggerPhase = din->m_triggerPhase; 
	SparseWaveformBase* cap;
	ParallelBus8BitsWaveform* cap8 = NULL;
	ParallelBus16BitsWaveform* cap16 = NULL;
	ParallelBus32BitsWaveform* cap32 = NULL;
	ParallelBus64BitsWaveform* cap64 = NULL;
	// Use the appropriate waveform according to bus width
	if(m_width <= 8)
	{
		cap8 = new ParallelBus8BitsWaveform(m_displaycolor);
		cap = cap8; 
	}
	else if (m_width <= 16)
	{
		cap16 = new ParallelBus16BitsWaveform(m_displaycolor);
		cap = cap16; 
	}
	else if (m_width <= 32)
	{
		cap32 = new ParallelBus32BitsWaveform(m_displaycolor);
		cap = cap32; 
	}
	else
	{
		cap64 = new ParallelBus64BitsWaveform(m_displaycolor);
		cap = cap64; 
	}
	cap->PrepareForCpuAccess();
	cap->m_timescale = timeScale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = triggerPhase;

	// Assume all inputs have same samplerate / trigger phase / start time
	Packet* pack = NULL;
	uint64_t curData = 0;
	uint64_t lastData = 0;
	WaveformBase* currentInput = NULL;
	optional<bool> curSampleValue;
	int64_t currentTime;
	int64_t currentDuration = 1;
	int64_t currentSampleIndex = -1;
	for(int64_t currentOffset = 0 ; currentOffset < maxOffset ; currentOffset++)
	{	// Iterate on each offset and get the byte value out of each input + check if it changed since last offset
		curData = 0;
		currentTime = (currentOffset*timeScale)+triggerPhase;
		for(int ibit=(m_width-1); ibit >= 0; ibit--)
		{	// Iterate on each data line based on bus width
			currentInput = inputs[ibit];
			if(currentInput != NULL)
			{
				curSampleValue = GetDigitalValueAtTime(currentInput,currentTime);
				if(curSampleValue)
				{	// Sample found => get it's value
					curData |= curSampleValue.value();
				}
				else
				{	// No sample found => keep previous value
					curData |= ((lastData >> ibit) & 0x01);
				}
				if(ibit > 0)
					curData <<= 1;
			}
		}
		if(currentOffset == 0 || curData != lastData)
		{	// First sample or data has changed
			if(currentOffset != 0)
			{	// Update previous sample duration
				cap->m_durations[currentSampleIndex]=currentDuration;
			}
			if(pack != NULL)
			{	// Finish previous packet if needed
				pack->m_len = currentDuration * timeScale;
				FinishPacket(pack);
			}
			// Create a new sample
			currentSampleIndex++;
			currentDuration = 1;
			cap->m_offsets.push_back(currentOffset);
			cap->m_durations.push_back(currentDuration);
			// Push the value to the waveform according to the bus width
			if(cap8)
			{
				cap8->m_samples.push_back((uint8_t)curData);
			}
			else if(cap16)
			{
				cap16->m_samples.push_back((uint16_t)curData);
			}
			else if(cap32)
			{
				cap32->m_samples.push_back((uint32_t)curData);
			}
			else
			{
				cap64->m_samples.push_back(curData);
			}
			lastData = curData;
			// Create a new packet to push the data
			pack = new Packet;
			pack->m_offset = currentTime;
			for(int i = m_width-8 ; i >= 0 ; i-=8)
			{	// Push each byte of the data in the appropriate order
				pack->m_data.push_back((uint8_t)(0xFF & (curData>>i)));
			}
		}
		else
		{	// Data is unchanged => simply update current sample duration
			currentDuration++;
		}
	}
	if(currentSampleIndex>0)
	{	// Update last duration
		cap->m_durations[currentSampleIndex]=currentDuration;
	}
	if(pack != NULL)
	{	// Finish pending packet
		pack->m_len = currentDuration * timeScale;
		FinishPacket(pack);
	}
	SetData(cap, 0);
}

void ParallelBusDecoder::FinishPacket(Packet* pack)
{
	//length header
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
	pack->m_headers["Length"] = tmp;

	//ascii packet contents
	string s;
	for(auto b : pack->m_data)
	{
		if(isprint(b))
			s += (char)b;
		else
			s += ".";
	}
	pack->m_headers["ASCII"] = s;

	m_packets.push_back(pack);
}

std::string ParallelBus8BitsWaveform::GetColor(size_t /*i*/)
{
	return m_color;
}

string ParallelBus8BitsWaveform::GetText(size_t i)
{
	uint8_t c = m_samples[i];
	char sbuf[16] = {0};
	snprintf(sbuf, sizeof(sbuf), "0x%02X", c);
	return sbuf;
}

std::string ParallelBus16BitsWaveform::GetColor(size_t /*i*/)
{
	return m_color;
}

string ParallelBus16BitsWaveform::GetText(size_t i)
{
	uint16_t c = m_samples[i];
	char sbuf[16] = {0};
	snprintf(sbuf, sizeof(sbuf), "0x%04X", c);
	return sbuf;
}

std::string ParallelBus32BitsWaveform::GetColor(size_t /*i*/)
{
	return m_color;
}

string ParallelBus32BitsWaveform::GetText(size_t i)
{
	uint32_t c = m_samples[i];
	char sbuf[16] = {0};
	snprintf(sbuf, sizeof(sbuf), "0x%08X", c);
	return sbuf;
}

std::string ParallelBus64BitsWaveform::GetColor(size_t /*i*/)
{
	return m_color;
}

string ParallelBus64BitsWaveform::GetText(size_t i)
{
	uint64_t c = m_samples[i];
	char sbuf[32] = {0};
	snprintf(sbuf, sizeof(sbuf), "0x%016llX", c);
	return sbuf;
}
