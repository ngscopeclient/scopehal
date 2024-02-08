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
	@brief Implementation of I2CDecoder
 */

#include "../scopehal/scopehal.h"
#include "I2CDecoder.h"
#include <algorithm>
#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

I2CDecoder::I2CDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	CreateInput("sda");
	CreateInput("scl");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool I2CDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string I2CDecoder::GetProtocolName()
{
	return "I2C";
}

vector<string> I2CDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Op");
	ret.push_back("Address");
	ret.push_back("Len");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

template<class T, class U>
void I2CDecoder::InnerLoop(T* sda, U* scl, I2CWaveform* cap)
{
	Packet* pack = nullptr;

	//Loop over the data and look for transactions
	bool				last_scl = true;
	bool 				last_sda = true;
	int64_t				tstart	= 0;
	I2CSymbol::stype	current_type = I2CSymbol::TYPE_ERROR;
	uint8_t				current_byte = 0;
	uint8_t				bitcount = 0;
	bool				last_was_start	= 0;
	size_t				sdalen = sda->size();
	size_t 				scllen = scl->size();
	size_t 				isda = 0;
	size_t 				iscl = 0;
	int64_t 			timestamp	= 0;

	while(true)
	{
		bool cur_sda = sda->m_samples[isda];
		bool cur_scl = scl->m_samples[iscl];

		//SDA falling with SCL high is beginning of a start condition
		if(!cur_sda && last_sda && cur_scl)
		{
			LogTrace("found i2c start at time %" PRId64 "\n", timestamp);

			//If we're following an ACK, this is a restart
			if(current_type == I2CSymbol::TYPE_DATA)
			{
				current_type = I2CSymbol::TYPE_RESTART;

				//Finish existing packet, if we have one
				if(pack)
				{
					pack->m_len = timestamp - pack->m_offset;
					pack->m_headers["Len"] = to_string(pack->m_data.size());
					m_packets.push_back(pack);
					pack = nullptr;
				}
			}

			//Otherwise, regular start
			else
			{
				tstart = timestamp;
				current_type = I2CSymbol::TYPE_START;
			}

			//Create a new packet. If we already have an incomplete one that got aborted, reset it
			if(pack)
			{
				pack->m_data.clear();
				pack->m_headers.clear();
			}
			else
				pack = new Packet;
			pack->m_offset = timestamp;
			pack->m_len = 0;
		}

		//End a start bit when SDA goes high if the first data bit is a 1
		//Otherwise end on a falling clock edge
		else if( ((current_type == I2CSymbol::TYPE_START) || (current_type == I2CSymbol::TYPE_RESTART)) &&
				(cur_sda || !cur_scl) )
		{
			cap->m_offsets.push_back(tstart);
			cap->m_durations.push_back(timestamp - tstart);
			cap->m_samples.push_back(I2CSymbol(current_type, 0));

			last_was_start	= true;
			current_type = I2CSymbol::TYPE_DATA;
			tstart = timestamp;
			bitcount = 0;
			current_byte = 0;
		}

		//SDA rising with SCL high is a stop condition
		else if(cur_sda && !last_sda && cur_scl)
		{
			LogTrace("found i2c stop at time %" PRIx64 "\n", timestamp);

			cap->m_offsets.push_back(tstart);
			cap->m_durations.push_back(timestamp - tstart);
			cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_STOP, 0));

			last_was_start	= false;

			tstart = timestamp;

			//Finish existing packet, if we have one
			if(pack)
			{
				pack->m_len = timestamp - pack->m_offset;
				pack->m_headers["Len"] = to_string(pack->m_data.size());
				m_packets.push_back(pack);
				pack = nullptr;
			}
		}

		//On a rising SCL edge, end the current bit
		else if(cur_scl && !last_scl)
		{
			if(current_type == I2CSymbol::TYPE_DATA)
			{
				//Save the current data bit
				bitcount ++;
				current_byte = (current_byte << 1);
				if(cur_sda)
					current_byte |= 1;

				//Add a sample if the byte is over
				if(bitcount == 8)
				{
					int64_t this_len = timestamp - tstart;

					if(last_was_start)
					{
						//If the start bit was insanely long, shorten it
						size_t nlast = cap->m_offsets.size() - 1;
						if(cap->m_durations[nlast] > 3*this_len)
						{
							int64_t tend = cap->m_offsets[nlast] + cap->m_durations[nlast];
							cap->m_durations[nlast] = this_len;
							cap->m_offsets[nlast] = tend - this_len;
						}

						cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_ADDRESS, current_byte));

						if(pack)
						{
							pack->m_headers["Address"] = to_string_hex(current_byte & 0xfe);
							if(current_byte & 1)
							{
								pack->m_headers["Op"] = "Read";
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							else
							{
								pack->m_headers["Op"] = "Write";
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							}
						}
					}
					else
					{
						cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_DATA, current_byte));

						if(pack)
							pack->m_data.push_back(current_byte);
					}

					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(this_len);

					last_was_start	= false;

					bitcount = 0;
					current_byte = 0;
					tstart = timestamp;

					current_type = I2CSymbol::TYPE_ACK;
				}
			}

			//ACK/NAK
			else if(current_type == I2CSymbol::TYPE_ACK)
			{
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(timestamp - tstart);
				cap->m_samples.push_back(I2CSymbol(I2CSymbol::TYPE_ACK, cur_sda));

				last_was_start	= false;

				tstart = timestamp;
				current_type = I2CSymbol::TYPE_DATA;
			}
		}

		//Save old state of both pins
		last_sda = cur_sda;
		last_scl = cur_scl;

		//Move on
		int64_t next_sda = Filter::GetNextEventTimestampScaled(sda, isda, sdalen, timestamp);
		int64_t next_scl = Filter::GetNextEventTimestampScaled(scl, iscl, scllen, timestamp);
		int64_t next_timestamp = min(next_sda, next_scl);
		if(next_timestamp == timestamp)
			break;
		timestamp = next_timestamp;
		Filter::AdvanceToTimestampScaled(sda, isda, sdalen, timestamp);
		Filter::AdvanceToTimestampScaled(scl, iscl, scllen, timestamp);
	}

	if(pack)
		delete pack;
}

void I2CDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto sda = GetInputWaveform(0);
	auto scl = GetInputWaveform(1);
	sda->PrepareForCpuAccess();
	scl->PrepareForCpuAccess();

	auto usda = dynamic_cast<UniformDigitalWaveform*>(sda);
	auto uscl = dynamic_cast<UniformDigitalWaveform*>(scl);

	auto ssda = dynamic_cast<SparseDigitalWaveform*>(sda);
	auto sscl = dynamic_cast<SparseDigitalWaveform*>(scl);

	//Create the capture
	auto cap = new I2CWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = sda->m_startTimestamp;
	cap->m_startFemtoseconds = sda->m_startFemtoseconds;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	if(usda && uscl)
		InnerLoop(usda, uscl, cap);
	else if(usda && sscl)
		InnerLoop(usda, sscl, cap);
	else if(ssda && sscl)
		InnerLoop(ssda, sscl, cap);
	else /*if(ssda && uscl)*/
		InnerLoop(ssda, uscl, cap);

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

std::string I2CWaveform::GetColor(size_t i)
{
	const I2CSymbol& s = m_samples[i];

	switch(s.m_stype)
	{
		case I2CSymbol::TYPE_ERROR:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
		case I2CSymbol::TYPE_ADDRESS:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];
		case I2CSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case I2CSymbol::TYPE_ACK:
			if(s.m_data)
				return StandardColors::colors[StandardColors::COLOR_IDLE];
			else
				return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];

		default:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];
	}
}

string I2CWaveform::GetText(size_t i)
{
	const I2CSymbol& s = m_samples[i];

	char tmp[32];
	switch(s.m_stype)
	{
		case I2CSymbol::TYPE_NONE:
		case I2CSymbol::TYPE_ERROR:
			snprintf(tmp, sizeof(tmp), "ERR");
			break;
		case I2CSymbol::TYPE_START:
			snprintf(tmp, sizeof(tmp), "START");
			break;
		case I2CSymbol::TYPE_RESTART:
			snprintf(tmp, sizeof(tmp), "RESTART");
			break;
		case I2CSymbol::TYPE_STOP:
			snprintf(tmp, sizeof(tmp), "STOP");
			break;
		case I2CSymbol::TYPE_ACK:
			if(s.m_data)
				snprintf(tmp, sizeof(tmp), "NAK");
			else
				snprintf(tmp, sizeof(tmp), "ACK");
			break;
		case I2CSymbol::TYPE_ADDRESS:
			if(s.m_data & 1)
				snprintf(tmp, sizeof(tmp), "R:%02x", s.m_data & 0xfe);
			else
				snprintf(tmp, sizeof(tmp), "W:%02x", s.m_data & 0xfe);
			break;
		case I2CSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			break;
	}
	return string(tmp);
}
