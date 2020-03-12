/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg, Galen Schretlen                                                         *
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

#include "scopehal.h"
#include "SiglentSCPIOscilloscope.h"
#include "ProtocolDecoder.h"
#include "base64.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentSCPIOscilloscope::SiglentSCPIOscilloscope(SCPITransport* transport)
	: LeCroyOscilloscope(transport)
{
}

SiglentSCPIOscilloscope::~SiglentSCPIOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SCPI protocol logic

string SiglentSCPIOscilloscope::GetDriverNameInternal()
{
	return "siglent";
}

// parses out length, does no other validation. requires 17 bytes at header.
uint32_t SiglentSCPIOscilloscope::ReadWaveHeader(char *header)
{

	int r = 0;

	m_transport->ReadRawData(16, (unsigned char*)header);

	if (strlen(header) != 16)
	{
		LogError("Unexpected descriptor header %s\n", header);
		return 0;
	}
	LogDebug("got header: %s\n", header);
	return atoi(&header[8]);
}

void SiglentSCPIOscilloscope::ReadWaveDescriptorBlock(SiglentWaveformDesc_t *descriptor, unsigned int channel)
{
	char header[17] = {0};
	ssize_t r = 0;
	uint32_t headerLength = 0;

	headerLength = ReadWaveHeader(header);

	if(headerLength != sizeof(struct SiglentWaveformDesc_t))
	{
		LogError("Unexpected header length: %u\n", headerLength);
	}

	m_transport->ReadRawData(sizeof(struct SiglentWaveformDesc_t), (unsigned char*)descriptor);

	// grab the \n
	m_transport->ReadReply();
}

bool SiglentSCPIOscilloscope::AcquireData(bool toQueue)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	LogDebug("Acquire data\n");

	double start = GetTime();


	//Read the wavedesc for every enabled channel in batch mode first
	vector<struct SiglentWaveformDesc_t*> wavedescs;
	char tmp[128];
	string cmd;
	bool enabled[4] = {false};
	BulkCheckChannelEnableState();
	for(unsigned int i=0; i<m_analogChannelCount; i++)
		enabled[i] = IsChannelEnabled(i);

	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		wavedescs.push_back(new struct SiglentWaveformDesc_t);
		if(enabled[i])
		{
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? DESC");
			ReadWaveDescriptorBlock(wavedescs[i], i);
			LogDebug("name %s, number: %u\n",wavedescs[i]->InstrumentName,
				wavedescs[i]->InstrumentNumber);

		}
	}

	// grab the actual waveforms

	//TODO: WFSU in outer loop and WF in inner loop
	unsigned int num_sequences = 1;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		//If the channel is invisible, don't waste time capturing data
		struct SiglentWaveformDesc_t *wavedesc = wavedescs[i];
		if(string(wavedesc->DescName).empty())
		{
			m_channels[i]->SetData(NULL);
			continue;
		}

		//Set up the capture we're going to store our data into
		AnalogCapture* cap = new AnalogCapture;

		//TODO: get sequence count from wavedesc
		//TODO: sequence mode should be multiple captures, one per sequence, with some kind of fifo or something?

		//Parse the wavedesc headers
		LogDebug("    Wavedesc len: %d\n", wavedesc->WaveDescLen);
		LogDebug("    Usertext len: %d\n", wavedesc->UserTextLen);
		LogDebug("    Trigtime len: %d\n", wavedesc->TriggerTimeArrayLen);

		if(wavedesc->TriggerTimeArrayLen != 0)
			num_sequences = wavedesc->TriggerTimeArrayLen;
		float v_gain = wavedesc->VerticalGain;
		float v_off = wavedesc->VerticalOffset;
		float interval = wavedesc->HorizontalInterval * 1e12f;
		double h_off = wavedesc->HorizontalOffset * 1e12f;	//ps from start of waveform to trigger
		double h_off_frac = fmodf(h_off, interval);						//fractional sample position, in ps
		if(h_off_frac < 0)
			h_off_frac = interval + h_off_frac;
		cap->m_triggerPhase = h_off_frac;	//TODO: handle this properly in segmented mode?
											//We might have multiple offsets
		//double h_unit = *reinterpret_cast<double*>(pdesc + 244);

		//Timestamp is a somewhat complex format that needs some shuffling around.
		double fseconds = wavedesc->Timestamp.Seconds;
		uint8_t seconds = floor(wavedesc->Timestamp.Seconds);
		cap->m_startPicoseconds = static_cast<int64_t>( (fseconds - seconds) * 1e12f );
		time_t tnow = time(NULL);
		struct tm* now = localtime(&tnow);
		struct tm tstruc;
		tstruc.tm_sec = seconds;
		tstruc.tm_min = wavedesc->Timestamp.Minutes;
		tstruc.tm_hour = wavedesc->Timestamp.Hours;
		tstruc.tm_mday = wavedesc->Timestamp.Days;
		tstruc.tm_mon = wavedesc->Timestamp.Months;
		tstruc.tm_year = wavedesc->Timestamp.Years;
		tstruc.tm_wday = now->tm_wday;
		tstruc.tm_yday = now->tm_yday;
		tstruc.tm_isdst = now->tm_isdst;
		cap->m_startTimestamp = mktime(&tstruc);
		cap->m_timescale = round(interval);
		for(unsigned int j=0; j<num_sequences; j++)
		{
			LogDebug("Channel %u block %u\n", i, j);

			//Ask for the segment of interest
			//(segment number is ignored for non-segmented waveforms)
			cmd = "WAVEFORM_SETUP SP,0,NP,0,FP,0,SN,";
			if(num_sequences > 1)
			{
				snprintf(tmp, sizeof(tmp), "%u", j + 1);	//segment 0 = "all", 1 = first part of capture
				cmd += tmp;
				m_transport->SendCommand(cmd);
			}

			//Read the actual waveform data
			cmd = "C1:WF? DAT2";
			cmd[1] += i;
			m_transport->SendCommand(cmd);
			char header[17] = {0};
			size_t wavesize = ReadWaveHeader(header);
			uint8_t *data = new uint8_t[wavesize];
			m_transport->ReadRawData(wavesize, data);
			// two \n...
			m_transport->ReadReply();
			m_transport->ReadReply();

			double trigtime = 0;
			if( (num_sequences > 1) && (j > 0) )
			{
				//If a multi-segment capture, ask for the trigger time data
				cmd = "C1:WF? TIME";
				cmd[1] += i;
				m_transport->SendCommand(cmd);

				trigtime = ReadWaveHeader(header);
				// \n
				m_transport->ReadReply();
				//double trigoff = ptrigtime[1];	//offset to point 0 from trigger time
			}

			int64_t trigtime_samples = trigtime * 1e12f / interval;
			//int64_t trigoff_samples = trigoff * 1e12f / interval;
			//LogDebug("    Trigger time: %.3f sec (%lu samples)\n", trigtime, trigtime_samples);
			//LogDebug("    Trigger offset: %.3f sec (%lu samples)\n", trigoff, trigoff_samples);

			//If we have samples already in the capture, stretch the final one to our trigger offset
			if(cap->m_samples.size())
			{
				auto& last_sample = cap->m_samples[cap->m_samples.size()-1];
				last_sample.m_duration = trigtime_samples - last_sample.m_offset;
			}

			//Decode the samples
			unsigned int num_samples = wavesize;
			LogDebug("Got %u samples\n", num_samples);
			for(unsigned int i=0; i<num_samples; i++)
				cap->m_samples.push_back(AnalogSample(i + trigtime_samples, 1, data[i] * v_gain - v_off));
		}

		//Done, update the data
		m_channels[i]->SetData(cap);
	}

	double dt = GetTime() - start;
	LogTrace("Waveform download took %.3f ms\n", dt * 1000);
	//Refresh protocol decoders
	for(size_t i=0; i<m_channels.size(); i++)
	{
		ProtocolDecoder* decoder = dynamic_cast<ProtocolDecoder*>(m_channels[i]);
		if(decoder != NULL)
			decoder->Refresh();
	}
	return true;
}
