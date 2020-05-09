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
	@brief Implementation of DDR3Decoder
 */

#include "../scopehal/scopehal.h"
#include "DDR3Decoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DDR3Decoder::DDR3Decoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_MEMORY)
{
	//Set up channels
	m_signalNames.push_back("CLK");
	m_channels.push_back(NULL);

	m_signalNames.push_back("WE#");
	m_channels.push_back(NULL);

	m_signalNames.push_back("RAS#");
	m_channels.push_back(NULL);

	m_signalNames.push_back("CAS#");
	m_channels.push_back(NULL);

	m_signalNames.push_back("CS#");
	m_channels.push_back(NULL);

	m_signalNames.push_back("A12");
	m_channels.push_back(NULL);

	m_signalNames.push_back("A10");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DDR3Decoder::NeedsConfig()
{
	return true;
}

bool DDR3Decoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i < 7) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string DDR3Decoder::GetProtocolName()
{
	return "DDR3 Command Bus";
}

void DDR3Decoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DDR3Cmd(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DDR3Decoder::Refresh()
{
	//Get the input data
	DigitalCapture* caps[7] = {0};
	for(int i=0; i<7; i++)
	{
		if(m_channels[i] == NULL)
		{
			SetData(NULL);
			return;
		}
		DigitalCapture* cap = dynamic_cast<DigitalCapture*>(m_channels[i]->GetData());
		if(cap == NULL)
		{
			SetData(NULL);
			return;
		}
		caps[i] = cap;
	}

	//Sample all of the inputs
	DigitalCapture* cclk = caps[0];
	vector<DigitalSample> we;
	vector<DigitalSample> ras;
	vector<DigitalSample> cas;
	vector<DigitalSample> cs;
	vector<DigitalSample> a12;
	vector<DigitalSample> a10;
	SampleOnRisingEdges(caps[1], cclk, we);
	SampleOnRisingEdges(caps[2], cclk, ras);
	SampleOnRisingEdges(caps[3], cclk, cas);
	SampleOnRisingEdges(caps[4], cclk, cs);
	SampleOnRisingEdges(caps[5], cclk, a12);
	SampleOnRisingEdges(caps[6], cclk, a10);

	//Create the capture
	DDR3Capture* cap = new DDR3Capture;
	cap->m_timescale = 1;
	cap->m_startTimestamp = cclk->m_startTimestamp;
	cap->m_startPicoseconds = 0;

	//Loop over the data and look for events on clock edges
	for(size_t i=0; i<we.size(); i++)
	{
		//Abort if one of the other waveforms ends earlier
		if( (i >= ras.size()) ||
			(i >= cas.size()) ||
			(i >= cs.size()) ||
			(i >= a12.size()) ||
			(i >= a10.size()) )
		{
			break;
		}

		bool swe = we[i];
		bool sras = ras[i];
		bool scas = cas[i];
		bool scs = cs[i];
		bool sa12 = a12[i];
		bool sa10 = a10[i];

		if(!scs)
		{
			//NOP
			if(sras && scas && swe)
				continue;

			DDR3Symbol sym(DDR3Symbol::TYPE_ERROR);

			if(!sras && !scas && !swe)
				sym.m_stype = DDR3Symbol::TYPE_MRS;
			else if(!sras && !scas && swe)
				sym.m_stype = DDR3Symbol::TYPE_REF;
			else if(!sras && scas && !swe && !sa10)
				sym.m_stype = DDR3Symbol::TYPE_PRE;
			else if(!sras && scas && !swe && sa10)
				sym.m_stype = DDR3Symbol::TYPE_PREA;
			else if(!sras && scas && swe)
				sym.m_stype = DDR3Symbol::TYPE_ACT;
			else if(sras && !scas && !swe)
			{
				if(!sa10)
					sym.m_stype = DDR3Symbol::TYPE_WR;
				else
					sym.m_stype = DDR3Symbol::TYPE_WRA;
			}
			else if(sras && !scas && swe)
			{
				if(!sa10)
					sym.m_stype = DDR3Symbol::TYPE_RD;
				else
					sym.m_stype = DDR3Symbol::TYPE_RDA;
			}

			//Unknown
			//TODO: self refresh entry/exit (we don't have CKE in the current test data source so can't use it)
			else
				LogDebug("[%zu] Unknown command (RAS=%d, CAS=%d, WE=%d, A12=%d, A10=%d)\n", i, sras, scas, swe, sa12, sa10);

			//Create the symbol
			cap->m_samples.push_back(DDR3Sample(
				we[i].m_offset,
				we[i].m_duration,
				sym));
		}
	}

	SetData(cap);
}

Gdk::Color DDR3Decoder::GetColor(int i)
{
	DDR3Capture* capture = dynamic_cast<DDR3Capture*>(GetData());
	if(capture != NULL)
	{
		const DDR3Symbol& s = capture->m_samples[i].m_sample;

		switch(s.m_stype)
		{
			case DDR3Symbol::TYPE_MRS:
			case DDR3Symbol::TYPE_REF:
			case DDR3Symbol::TYPE_PRE:
			case DDR3Symbol::TYPE_PREA:
				return m_standardColors[COLOR_CONTROL];

			case DDR3Symbol::TYPE_ACT:
			case DDR3Symbol::TYPE_WR:
			case DDR3Symbol::TYPE_WRA:
			case DDR3Symbol::TYPE_RD:
			case DDR3Symbol::TYPE_RDA:
				return m_standardColors[COLOR_ADDRESS];

			case DDR3Symbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string DDR3Decoder::GetText(int i)
{
	DDR3Capture* capture = dynamic_cast<DDR3Capture*>(GetData());
	if(capture != NULL)
	{
		const DDR3Symbol& s = capture->m_samples[i].m_sample;

		switch(s.m_stype)
		{
			case DDR3Symbol::TYPE_MRS:
				return "MRS";

			case DDR3Symbol::TYPE_REF:
				return "REF";

			case DDR3Symbol::TYPE_PRE:
				return "PRE";

			case DDR3Symbol::TYPE_PREA:
				return "PREA";

			case DDR3Symbol::TYPE_ACT:
				return "ACT";

			case DDR3Symbol::TYPE_WR:
				return "WR";

			case DDR3Symbol::TYPE_WRA:
				return "WRA";

			case DDR3Symbol::TYPE_RD:
				return "RD";

			case DDR3Symbol::TYPE_RDA:
				return "RDA";

			case DDR3Symbol::TYPE_ERROR:
			default:
				return "ERR";
		}
	}
	return "";
}
