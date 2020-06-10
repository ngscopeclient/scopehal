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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of AntikernelLogicAnalyzer
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "AntikernelLogicAnalyzer.h"
#include "ProtocolDecoder.h"

using namespace std;

enum opcodes_t
{
	CMD_NOP,
	CMD_SET_MATCH_ALL,
	CMD_SET_TRIG_OFFSET,
	CMD_SET_TRIG_MODE,
	CMD_SET_COMPARE_TARGET,
	CMD_ARM,
	CMD_STOP,
	CMD_FORCE,
	CMD_GET_STATUS,
	CMD_GET_NAME_LEN,
	CMD_GET_CHANNEL_COUNT,
	CMD_GET_NAME,
	CMD_GET_WIDTH,
	CMD_GET_DATA,
	CMD_GET_DEPTH,
	CMD_GET_TOTAL_WIDTH,
	CMD_GET_SAMPLE_PERIOD,
	CMD_GET_MAX_WIDTH
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Connects to a UART and reads the stuff off it
 */
AntikernelLogicAnalyzer::AntikernelLogicAnalyzer(SCPITransport* transport)
{
	m_transport = transport;

	m_triggerArmed = false;
	m_triggerOneShot = false;

	//Populate IDN info
	m_vendor = "Antikernel Labs";
	m_model = "ILA";
	m_fwVersion = "1.0";
	m_serial = "NoSerial";

	LoadChannels();
	ResetTriggerConditions();
}

AntikernelLogicAnalyzer::~AntikernelLogicAnalyzer()
{

}


string AntikernelLogicAnalyzer::IDPing()
{
	return "";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

string AntikernelLogicAnalyzer::GetTransportName()
{
	return m_transport->GetName();
}

string AntikernelLogicAnalyzer::GetTransportConnectionString()
{
	return m_transport->GetConnectionString();
}

string AntikernelLogicAnalyzer::GetDriverNameInternal()
{
	return "akila";
}

void AntikernelLogicAnalyzer::SendCommand(uint8_t opcode)
{
	m_transport->SendRawData(1, &opcode);
}

void AntikernelLogicAnalyzer::SendCommand(uint8_t opcode, uint8_t chan)
{
	uint8_t buf[2] = {opcode, chan};
	m_transport->SendRawData(2, buf);
}

void AntikernelLogicAnalyzer::SendCommand(uint8_t opcode, uint8_t chan, uint8_t arg)
{
	uint8_t buf[3] = {opcode, chan, arg};
	m_transport->SendRawData(3, buf);
}

uint8_t AntikernelLogicAnalyzer::Read1ByteReply()
{
	uint8_t ret;
	m_transport->ReadRawData(1, &ret);
	return ret;
}

unsigned int AntikernelLogicAnalyzer::GetInstrumentTypes()
{
	return INST_OSCILLOSCOPE;
}

string AntikernelLogicAnalyzer::GetName()
{
	return "NoName";
}

string AntikernelLogicAnalyzer::GetVendor()
{
	return "Antikernel ILA";
}

string AntikernelLogicAnalyzer::GetSerial()
{
	return "NoSerialNumber";
}

void AntikernelLogicAnalyzer::LoadChannels()
{
	LogDebug("Logic analyzer: loading channel metadata\n");
	LogIndenter li;

	//Get the number of channels
	SendCommand(CMD_GET_CHANNEL_COUNT);
	uint8_t nchans = Read1ByteReply();

	//Get the length of the names
	SendCommand(CMD_GET_NAME_LEN);
	uint8_t namelen = Read1ByteReply();
	//LogDebug("We have %d channels, %d chars per name\n", nchans, namelen);

	char* namebuf = new char[namelen + 1];

	//Create a new channel 0 for the capture clock
	//(since some protocol decoders need rising edges to trigger on, etc)
	auto chan = new OscilloscopeChannel(
		this,
		"clk",
		OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
		GetDefaultChannelColor(m_channels.size()),
		1,
		m_channels.size(),
		false);	//not a physical channel
	m_channels.push_back(chan);
	m_highIndexes.push_back(0);	//not used, just pad stuff to fit
	m_lowIndexes.push_back(0);

	//Read each name
	size_t index = 0;
	for(size_t i=0; i<nchans; i++)
	{
		//Get the width of this channel
		SendCommand(CMD_GET_WIDTH, i);
		uint8_t width = Read1ByteReply();

		SendCommand(CMD_GET_NAME, i);
		m_transport->ReadRawData(namelen, (unsigned char*)namebuf);

		//Names currently come off in reversed order with null padding after.
		//Correct this.
		namebuf[namelen] = '\0';
		string realname;
		for(ssize_t j=namelen; j>=0; j--)
		{
			if(namebuf[j] == 0)
				continue;
			realname += namebuf[j];
		}

		//Add the channel
		chan = new OscilloscopeChannel(
			this,
			realname,
			OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
			GetDefaultChannelColor(m_channels.size()),
			width,
			m_channels.size(),
			true);
		m_channels.push_back(chan);

		m_lowIndexes.push_back(index);
		m_highIndexes.push_back(index + width - 1);
		index += width;

		/*
		LogDebug("Channel: %s (%d bits wide, from %zu to %zu)\n",
			realname.c_str(),
			width,
			m_lowIndexes[i],
			m_highIndexes[i]);
		*/
	}

	delete[] namebuf;

	uint8_t rawperiod[3];
	SendCommand(CMD_GET_SAMPLE_PERIOD);
	m_transport->ReadRawData(3, (unsigned char*)rawperiod);
	m_samplePeriod = (rawperiod[0] << 16) | (rawperiod[1] << 8) | rawperiod[2];
	//LogDebug("Sample period is %u ps\n", m_samplePeriod);

	//Get memory aspect ratio info
	uint8_t rawlen[3];
	uint8_t rawwidth[3];
	SendCommand(CMD_GET_DEPTH);
	m_transport->ReadRawData(3, (unsigned char*)rawlen);
	SendCommand(CMD_GET_TOTAL_WIDTH);
	m_transport->ReadRawData(3, (unsigned char*)rawwidth);
	m_memoryDepth = (rawlen[0] << 16) | (rawlen[1] << 8) | rawlen[2];
	m_memoryWidth = (rawwidth[0] << 16) | (rawwidth[1] << 8) | rawwidth[2];
	//LogDebug("Capture memory is %u samples long\n", depth);
	SendCommand(CMD_GET_MAX_WIDTH);
	m_maxWidth = Read1ByteReply();

	//Round sampling period down to be an even number of picoseconds
	//(this is needed so the clock can be double-rate and not lose sync)
	if(m_samplePeriod & 1)
		m_samplePeriod --;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerType AntikernelLogicAnalyzer::GetTriggerType()
{
	return TRIGGER_TYPE_COMPLEX;
}

void AntikernelLogicAnalyzer::SetTriggerType(Oscilloscope::TriggerType /*type*/)
{
	//no op, always complex pattern trigger
}

Oscilloscope::TriggerMode AntikernelLogicAnalyzer::PollTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	SendCommand(CMD_GET_STATUS);
	uint8_t status = Read1ByteReply();

	switch(status)
	{
		case 1:	//armed
		case 2:	//triggered but data not ready to read yet
			return TRIGGER_MODE_RUN;
		case 3:
			return TRIGGER_MODE_TRIGGERED;

		case 0:
		default:
			return TRIGGER_MODE_STOP;
	}
}

bool AntikernelLogicAnalyzer::AcquireData(bool toQueue)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//LogDebug("Acquiring data...\n");
	LogIndenter li;

	//Calculate some memory dimensions
	uint32_t bytewidth = m_memoryWidth/8;
	if(m_memoryWidth & 7)
		bytewidth ++;
	//LogDebug("Capture memory is %u bits (%u bytes) wide\n", m_memoryWidth, bytewidth);
	uint32_t memsize = bytewidth*m_memoryDepth;
	//LogDebug("Total capture is %u bytes\n", memsize);

	//Read the actual data
	vector<uint8_t> data;
	data.resize(memsize);
	SendCommand(CMD_GET_DATA);
	m_transport->ReadRawData(memsize, &data[0]);

	SequenceSet pending_waveforms;

	//Synthesize the clock
	double time = GetTime();
	double ps = (time - floor(time)) * 1e12f;
	{
		DigitalWaveform* cap = new DigitalWaveform;
		cap->m_timescale = m_samplePeriod / 2;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time;
		cap->m_startPicoseconds = ps;
		cap->m_samples.resize(m_memoryDepth * 2);

		auto chan = m_channels[0];

		for(size_t i=0; i<m_memoryDepth; i++)
		{
			cap->m_offsets[i*2]			= i*2;
			cap->m_durations[i*2] 		= 1;
			cap->m_samples[i*2] 		= false;

			cap->m_offsets[i*2 + 1]		= i*2 + 1;
			cap->m_durations[i*2 + 1] 	= 1;
			cap->m_samples[i*2 + 1] 	= true;
		}

		//Done, update the data
		if(!toQueue)
			chan->SetData(cap);
		else
			pending_waveforms[chan] = cap;
	}

	//Crunch the waveform data (note, LS byte first in each row)
	for(size_t i=1; i<m_channels.size(); i++)
	{
		auto chan = m_channels[i];

		size_t nlow = m_lowIndexes[i];
		size_t cwidth = chan->GetWidth();

		if(cwidth == 1)
		{
			size_t nbyte = nlow / 8;
			size_t nbit = nlow % 8;

			//Create the channel
			DigitalWaveform* cap = new DigitalWaveform;
			cap->m_timescale = m_samplePeriod;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time;
			cap->m_startPicoseconds = ps;
			cap->m_samples.resize(m_memoryDepth);

			//Pull the data
			for(size_t j=0; j<m_memoryDepth; j++)
			{
				cap->m_offsets[j] 	= j;
				cap->m_durations[j]	= 1;

				uint8_t s = data[j*bytewidth + nbyte];
				cap->m_samples[j]	= (s >> nbit) & 1 ? true : false;
			}

			//Done, update the data
			if(!toQueue)
				chan->SetData(cap);
			else
				pending_waveforms[chan] = cap;
		}

		else
		{
			//Create the channel
			DigitalBusWaveform* cap = new DigitalBusWaveform;
			cap->m_timescale = m_samplePeriod;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time;
			cap->m_startPicoseconds = ps;
			cap->m_samples.resize(m_memoryDepth);

			for(size_t j=0; j<m_memoryDepth; j++)
			{
				cap->m_offsets[j] = j;
				cap->m_durations[j] = 1;

				vector<bool> bits;
				for(size_t k=0; k<cwidth; k++)
				{
					size_t off = nlow + k;
					size_t nbyte = off / 8;
					size_t nbit = off % 8;
					uint8_t s = data[j*bytewidth + nbyte];
					bits.push_back((s >> nbit) & 1 ? true : false);
				}

				cap->m_samples[j] = bits;
			}

			//Done, update the data
			if(!toQueue)
				chan->SetData(cap);
			else
				pending_waveforms[chan] = cap;
		}
	}
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(pending_waveforms);
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
		ArmTrigger();
	else
		m_triggerArmed = false;

	return true;
}

void AntikernelLogicAnalyzer::ArmTrigger()
{
	SendCommand(CMD_ARM);
	m_triggerArmed = true;

	//SendCommand(CMD_FORCE);
}

void AntikernelLogicAnalyzer::StartSingleTrigger()
{
	m_triggerOneShot = true;
	ArmTrigger();
}

void AntikernelLogicAnalyzer::Start()
{
	m_triggerOneShot = false;
	ArmTrigger();
}

void AntikernelLogicAnalyzer::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	SendCommand(CMD_STOP);
	m_triggerArmed = false;
}

bool AntikernelLogicAnalyzer::IsTriggerArmed()
{
	return m_triggerArmed;
}

void AntikernelLogicAnalyzer::ResetTriggerConditions()
{
	//Set the trigger to be ch1 equal
	SendCommand(CMD_SET_TRIG_MODE, 0, 2);

	//Set target to 1
	unsigned char tmp[6] =
	{ CMD_SET_COMPARE_TARGET, 0, 0,0,0,1 };
	m_transport->SendRawData(sizeof(tmp), tmp);

	//Set offset to 32 samples
	SendCommand(CMD_SET_TRIG_OFFSET, 0, 32);

	//SendCommand(CMD_SET_COMPARE_TARGET, 0, 1);

	/*
	for(size_t i=0; i<m_triggers.size(); i++)
		m_triggers[i] = TRIGGER_TYPE_DONTCARE;
	*/
}

void AntikernelLogicAnalyzer::SetTriggerForChannel(OscilloscopeChannel* /*channel*/, vector<TriggerType> /*triggerbits*/)
{
}

size_t AntikernelLogicAnalyzer::GetTriggerChannelIndex()
{
	return 0;
}

void AntikernelLogicAnalyzer::SetTriggerChannelIndex(size_t /*i*/)
{

}

float AntikernelLogicAnalyzer::GetTriggerVoltage()
{
	return 0;
}

void AntikernelLogicAnalyzer::SetTriggerVoltage(float /*v*/)
{
	//no-op, all channels are digital
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration. Mostly empty stubs.

bool AntikernelLogicAnalyzer::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void AntikernelLogicAnalyzer::EnableChannel(size_t /*i*/)
{
	//no-op, all channels are always on
}

void AntikernelLogicAnalyzer::DisableChannel(size_t /*i*/)
{
	//no-op, all channels are always on
}

OscilloscopeChannel::CouplingType AntikernelLogicAnalyzer::GetChannelCoupling(size_t /*i*/)
{
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void AntikernelLogicAnalyzer::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, all channels are digital
}

double AntikernelLogicAnalyzer::GetChannelAttenuation(size_t /*i*/)
{
	return 1;
}

void AntikernelLogicAnalyzer::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op, all channels are digital
}

int AntikernelLogicAnalyzer::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void AntikernelLogicAnalyzer::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op, all channels are digital
}

double AntikernelLogicAnalyzer::GetChannelVoltageRange(size_t /*i*/)
{
	return 1;
}

void AntikernelLogicAnalyzer::SetChannelVoltageRange(size_t /*i*/, double /*range*/)
{
	//no-op, all channels are digital
}

OscilloscopeChannel* AntikernelLogicAnalyzer::GetExternalTrigger()
{
	return NULL;
}

double AntikernelLogicAnalyzer::GetChannelOffset(size_t /*i*/)
{
	return 0;
}

void AntikernelLogicAnalyzer::SetChannelOffset(size_t /*i*/, double /*offset*/)
{
	//no-op, all channels are digital
}

vector<uint64_t> AntikernelLogicAnalyzer::GetSampleRatesNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> AntikernelLogicAnalyzer::GetSampleRatesInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> AntikernelLogicAnalyzer::GetInterleaveConflicts()
{
	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> AntikernelLogicAnalyzer::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(m_memoryDepth);
	return ret;
}

vector<uint64_t> AntikernelLogicAnalyzer::GetSampleDepthsInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(m_memoryDepth);
	return ret;
}

uint64_t AntikernelLogicAnalyzer::GetSampleRate()
{
	//FIXME
	return 1;
}

uint64_t AntikernelLogicAnalyzer::GetSampleDepth()
{
	return m_memoryDepth;
}

void AntikernelLogicAnalyzer::SetSampleDepth(uint64_t /*depth*/)
{
	//not changeable, no-op
}

void AntikernelLogicAnalyzer::SetSampleRate(uint64_t /*rate*/)
{
	//not changeable, no-op
}

void AntikernelLogicAnalyzer::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t AntikernelLogicAnalyzer::GetTriggerOffset()
{
	//FIXME
	return 0;
}
