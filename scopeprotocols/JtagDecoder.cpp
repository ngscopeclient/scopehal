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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of JtagDecoder
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "JtagRenderer.h"
#include "JtagDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JtagSymbol

const char* JtagSymbol::GetName(JtagSymbol::JtagState state)
{
	static const char* names[]=
	{
		"TLR",
		"RTI",
		"SLDR",
		"SLIR",
		"CDR",
		"CIR",
		"SDR",
		"SIR",
		"E1DR",
		"E1IR",
		"PDR",
		"PIR",
		"E2DR",
		"E2IR",
		"UDR",
		"UIR",
		"UNK0",
		"UNK1",
		"UNK2",
		"UNK3",
		"UNK4"
	};
	return names[state];
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

JtagDecoder::JtagDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("TDI");
	m_channels.push_back(NULL);

	m_signalNames.push_back("TDO");
	m_channels.push_back(NULL);

	m_signalNames.push_back("TMS");
	m_channels.push_back(NULL);

	m_signalNames.push_back("TCK");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool JtagDecoder::NeedsConfig()
{
	//need to set channel configuration
	return true;
}

ChannelRenderer* JtagDecoder::CreateRenderer()
{
	return new JtagRenderer(this);
}

bool JtagDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i < 4) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string JtagDecoder::GetProtocolName()
{
	return "JTAG";
}

void JtagDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "JTAG(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void JtagDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) || (m_channels[2] == NULL) || (m_channels[3] == NULL) )
	{
		SetData(NULL);
		return;
	}
	DigitalCapture* tdi = dynamic_cast<DigitalCapture*>(m_channels[0]->GetData());
	DigitalCapture* tdo = dynamic_cast<DigitalCapture*>(m_channels[1]->GetData());
	DigitalCapture* tms = dynamic_cast<DigitalCapture*>(m_channels[2]->GetData());
	DigitalCapture* tck = dynamic_cast<DigitalCapture*>(m_channels[3]->GetData());
	if( (tdi == NULL) || (tdo == NULL) || (tms == NULL) || (tck == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Sample the data stream at each clock edge
	vector<DigitalSample> dtdi;
	vector<DigitalSample> dtdo;
	vector<DigitalSample> dtms;
	SampleOnRisingEdges(tdi, tck, dtdi);
	SampleOnRisingEdges(tdo, tck, dtdo);
	SampleOnRisingEdges(tms, tck, dtms);

	//Create the capture
	JtagCapture* cap = new JtagCapture;
	cap->m_timescale = 1;
	cap->m_startTimestamp = tck->m_startTimestamp;
	cap->m_startPicoseconds = tck->m_startPicoseconds;

	//Table for state transitions
	JtagSymbol::JtagState state_if_tms_high[] =
	{
		JtagSymbol::TEST_LOGIC_RESET,	//from TEST_LOGIC_RESET
		JtagSymbol::SELECT_DR_SCAN,		//from RUN_TEST_IDLE
		JtagSymbol::SELECT_IR_SCAN,		//from SELECT_DR_SCAN
		JtagSymbol::TEST_LOGIC_RESET,	//from SELECT_IR_SCAN
		JtagSymbol::EXIT2_DR,			//from CAPTURE_DR
		JtagSymbol::EXIT2_IR,			//from CAPTURE_IR
		JtagSymbol::EXIT1_DR,			//from SHIFT_DR
		JtagSymbol::EXIT1_IR,			//from SHIFT_IR
		JtagSymbol::UPDATE_DR,			//from EXIT1_DR
		JtagSymbol::UPDATE_IR,			//from EXIT1_IR
		JtagSymbol::EXIT2_DR,			//from PAUSE_DR
		JtagSymbol::EXIT2_IR,			//from PAUSE_IR
		JtagSymbol::UPDATE_DR,			//from EXIT2_DR
		JtagSymbol::UPDATE_IR,			//from EXIT2_IR
		JtagSymbol::SELECT_DR_SCAN,		//from UPDATE_DR
		JtagSymbol::SELECT_DR_SCAN,		//from UPDATE_IR

		JtagSymbol::UNKNOWN_1,			//from UNKNOWN_0
		JtagSymbol::UNKNOWN_2,			//from UNKNOWN_1
		JtagSymbol::UNKNOWN_3,			//from UNKNOWN_2
		JtagSymbol::UNKNOWN_4,			//from UNKNOWN_3
		JtagSymbol::TEST_LOGIC_RESET	//from UNKNOWN_4
	};

	JtagSymbol::JtagState state_if_tms_low[] =
	{
		JtagSymbol::RUN_TEST_IDLE,		//from TEST_LOGIC_RESET
		JtagSymbol::RUN_TEST_IDLE,		//from RUN_TEST_IDLE
		JtagSymbol::CAPTURE_DR,			//from SELECT_DR_SCAN
		JtagSymbol::CAPTURE_IR,			//from SELECT_IR_SCAN
		JtagSymbol::SHIFT_DR,			//from CAPTURE_DR
		JtagSymbol::SHIFT_IR,			//from CAPTURE_IR
		JtagSymbol::SHIFT_DR,			//from SHIFT_DR
		JtagSymbol::SHIFT_IR,			//from SHIFT_IR
		JtagSymbol::PAUSE_DR,			//from EXIT1_DR
		JtagSymbol::PAUSE_IR,			//from EXIT1_IR
		JtagSymbol::PAUSE_DR,			//from PAUSE_DR
		JtagSymbol::PAUSE_IR,			//from PAUSE_IR
		JtagSymbol::CAPTURE_DR,			//from EXIT2_DR
		JtagSymbol::CAPTURE_IR,			//from EXIT2_IR
		JtagSymbol::RUN_TEST_IDLE,		//from UPDATE_DR
		JtagSymbol::RUN_TEST_IDLE,		//from UPDATE_IR

		JtagSymbol::UNKNOWN_0,			//from UNKNOWN_0
		JtagSymbol::UNKNOWN_0,			//from UNKNOWN_1
		JtagSymbol::UNKNOWN_0,			//from UNKNOWN_2
		JtagSymbol::UNKNOWN_0,			//from UNKNOWN_3
		JtagSymbol::UNKNOWN_0			//from UNKNOWN_4
	};

	//Main decode loop
	JtagSymbol::JtagState state = JtagSymbol::UNKNOWN_0;
	size_t num_cycles = 1;
	size_t istart = 0;
	for(size_t i=0; i<dtms.size(); i++)
	{
		//Update the state
		JtagSymbol::JtagState next_state;
		if(dtms[i])
			next_state = state_if_tms_high[state];
		else
			next_state = state_if_tms_low[state];

		if(next_state != state)
		{
			//Add a sample for the previous state
			cap->m_samples.push_back(JtagSample(
				dtms[istart].m_offset,
				dtms[i].m_offset - dtms[istart].m_offset,
				JtagSymbol(state)));

			LogDebug("[JtagDecoder] state = %s (num_cycles = %zu)\n", JtagSymbol::GetName(state), num_cycles);
			state = next_state;
			num_cycles = 1;
			istart = i;
		}
		else
			num_cycles ++;
	}

	SetData(cap);
}
