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
	CMD_GET_WIDTH
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Connects to a UART and reads the stuff off it
 */
AntikernelLogicAnalyzer::AntikernelLogicAnalyzer(SCPITransport* transport)
{
	m_transport = transport;

	//TODO: read IDN info some other way, since it's not really scpi

	LoadChannels();
	ResetTriggerConditions();
}

AntikernelLogicAnalyzer::~AntikernelLogicAnalyzer()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

void AntikernelLogicAnalyzer::SendCommand(uint8_t opcode)
{
	m_transport->SendRawData(1, &opcode);
}

void AntikernelLogicAnalyzer::SendCommand(uint8_t opcode, uint8_t chan)
{
	uint8_t buf[2] = {opcode, chan};
	m_transport->SendRawData(2, buf);
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
	LogDebug("We have %d channels, %d chars per name\n", nchans, namelen);

	char* namebuf = new char[namelen + 1];

	//Read each name
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

		//Invert the order
		LogDebug("Channel: %s (%d bits wide)\n", realname.c_str(), width);
	}

	delete[] namebuf;
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
	return TRIGGER_MODE_RUN;
}

bool AntikernelLogicAnalyzer::AcquireData(bool toQueue)
{
	LogDebug("Acquiring data...\n");
	LogIndenter li;

	return true;
}

void AntikernelLogicAnalyzer::StartSingleTrigger()
{

}

void AntikernelLogicAnalyzer::Start()
{
	//printf("continuous capture not implemented\n");
}

void AntikernelLogicAnalyzer::Stop()
{

}

bool AntikernelLogicAnalyzer::IsTriggerArmed()
{
	return true;
}

void AntikernelLogicAnalyzer::ResetTriggerConditions()
{
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
	//FIXME
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> AntikernelLogicAnalyzer::GetSampleDepthsInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	return ret;
}
