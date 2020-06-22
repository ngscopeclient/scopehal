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
	, m_acquiredDataIsSigned(false)
	, m_hasVdivAttnBug(true)
{
	if (m_modelid == MODEL_SIGLENT_SDS2000X)
	{
		m_acquiredDataIsSigned = true;
	}
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

void SiglentSCPIOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	// FIXME: this assumes there are 8 vertical DIVs on the screen. Should this become a per-SKU parameter?
	double wantedVdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	// A Siglent SDS2304X has the 2 firmware bugs (FW 1.2.2.2 R19)
	//
	// When you program a VOLT_DIV of x, it actually sets a VOLT_DIV of x * probe_attenuation.
	// That's the value that will show up to the scope UI and also the value that gets read back
	// for VOLT_DIV?.
	// So the bug only happens when sending VOLT_DIV, but not when reading it.
	//
	// The other bug is that, sometimes, programming VOLT_DIV just doesn't work: the value
	// gets ignored. However, when you do a VOLT_DIV? immediately after a VOLT_DIV, then
	// it always seems to work.
	//
	// It's unclear which SKUs and FW version have this bug.
	//
	// The following work around should be work for all scopes, whether they have the bug or not:
	// 1. Program the desired value
	// 2. Read it back the actual value
	// 3. Program the value again, but this time adjusted by the ratio between desired and actual value.
	// 4. Read back the value again to make sure it held
	//
	// The only disadvantage to this is that UI on the scope will update twice.
	//
	// A potential improvement would be to check at the start if the scope exhibits this bug...

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV %.4f", m_channels[i]->GetHwname().c_str(), wantedVdiv);
	m_transport->SendCommand(cmd);

	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV?", m_channels[i]->GetHwname().c_str());
	m_transport->SendCommand(cmd);

	string resultStr = m_transport->ReadReply();
	double actVdiv;
	sscanf(resultStr.c_str(), "%lf", &actVdiv);

	if (!m_hasVdivAttnBug)
		return;

	double adjustVdiv = wantedVdiv / actVdiv;

	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV %.4f", m_channels[i]->GetHwname().c_str(), wantedVdiv * adjustVdiv);
	m_transport->SendCommand(cmd);

	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV?", m_channels[i]->GetHwname().c_str());
	m_transport->SendCommand(cmd);

	resultStr = m_transport->ReadReply();
	sscanf(resultStr.c_str(), "%lf", &actVdiv);

	adjustVdiv = wantedVdiv / actVdiv;

	LogDebug("Wanted VOLT_DIV: %lf, Actual VOLT_DIV: %lf, ratio: %lf \n", wantedVdiv, actVdiv, adjustVdiv);
}

//  Somewhat arbitrary. No header has been seen that's larger than 17...
static const int maxWaveHeaderSize = 40;

// "WF?" commands return data that starts with a header. 
// On a Siglent SDS2304X, the header of "C0: WF? DESC looks like this: "ALL,#9000000346"
// On other Siglent scopes, a header may look like this: "C1:WF ALL,#9000000070"
// So the size of the header is unknown due to the variable lenghth prefix.

// Returns -1 if no valid header was seen.
// Otherwise, it returns the size of the data chunk that follows the header.
int SiglentSCPIOscilloscope::ReadWaveHeader(char *header)
{
	int i = 0;

	// Scan the prefix until ',' is seen.
	// We don't want to overfetch, so just get stuff one byte at time...
	bool comma_seen = false;
	while(!comma_seen && i<maxWaveHeaderSize-12)			// -12: we need space for the size part of the header
	{
		m_transport->ReadRawData(1, (unsigned char *)(header+i));
		comma_seen = (header[i] == ',');
		++i;
	}
	header[i] = '\0';

	if (!comma_seen)
	{
		LogError("WaveHeader: no end of prefix seen in header (%s)\n", header);
		return -1;
	}

	// We now expect "#9nnnnnnnnn" (11 characters), where 'n' is a digit.
	int start_of_size = i;

	m_transport->ReadRawData(11, (unsigned char *)(header+start_of_size));
	header[start_of_size+11] = '\0';

	bool header_conformant = true;
	header_conformant &= (header[start_of_size]   == '#');
	header_conformant &= (header[start_of_size+1] == '9');
	for(i=2; i<11;++i)
		header_conformant &= isdigit(header[start_of_size+i]);

	header[start_of_size+11] = '\0';

	if (!header_conformant)
	{
		LogError("WaveHeader: header non-conformant (%s)\n", header);
		return -1;
	}

	int data_chunk_size = atoi(&header[start_of_size+2]);

	LogDebug("WaveHeader: size = %d (%s)\n", data_chunk_size, header);
	return data_chunk_size;

	m_transport->ReadRawData(15, (unsigned char*)header);
	header[15] = 0;
}

void SiglentSCPIOscilloscope::ReadWaveDescriptorBlock(SiglentWaveformDesc_t *descriptor, unsigned int /*channel*/)
{
	char header[maxWaveHeaderSize] = {0};
	int headerLength = 0;

	headerLength = ReadWaveHeader(header);
	LogDebug("header length: %d\n", headerLength);

	if(headerLength != sizeof(struct SiglentWaveformDesc_t))
	{
		LogError("Unexpected header length: %d\n", headerLength);
	}

	m_transport->ReadRawData(sizeof(struct SiglentWaveformDesc_t), (unsigned char*)descriptor);

	// grab the \n
	m_transport->ReadReply();
}

bool SiglentSCPIOscilloscope::AcquireData(bool toQueue)
{

	LogDebug("Acquire data\n");

	double start = GetTime();

	vector<struct SiglentWaveformDesc_t*> wavedescs;
	bool enabled[4] = {false};
	map<int, vector<WaveformBase*> > pending_waveforms;

	{
		lock_guard<recursive_mutex> lock(m_mutex);

		BulkCheckChannelEnableState();
		for(unsigned int i=0; i<m_analogChannelCount; i++)
			enabled[i] = IsChannelEnabled(i);

		// Read the wavedesc for every enabled channel 
		for(unsigned int i=0; i<m_analogChannelCount; i++)
		{
			wavedescs.push_back(new struct SiglentWaveformDesc_t);
			if(enabled[i])
			{
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? DESC");
				// TODO: a bunch of error checking...
				ReadWaveDescriptorBlock(wavedescs[i], i);
				LogDebug("name %s, number: %u\n",wavedescs[i]->InstrumentName,
					wavedescs[i]->InstrumentNumber);
			}
		}

		// Grab the actual waveforms

		//TODO: WFSU in outer loop and WF in inner loop
		unsigned int num_sequences = 1;
		for(unsigned int chanNr=0; chanNr<m_analogChannelCount; chanNr++)
		{
			//If the channel is invisible, don't waste time capturing data
			struct SiglentWaveformDesc_t *wavedesc = wavedescs[chanNr];
			if(!enabled[chanNr] || string(wavedesc->DescName).empty())
			{
				if (!toQueue)
					m_channels[chanNr]->SetData(NULL);
				continue;
			}

			//Set up the capture we're going to store our data into
			AnalogWaveform* cap = new AnalogWaveform;

			//TODO: get sequence count from wavedesc
			//TODO: sequence mode should be multiple captures, one per sequence, with some kind of fifo or something?

			//Parse the wavedesc headers
			LogDebug("   Wavedesc len: %d\n", wavedesc->WaveDescLen);
			LogDebug("   Usertext len: %d\n", wavedesc->UserTextLen);
			LogDebug("   Trigtime len: %d\n", wavedesc->TriggerTimeArrayLen);

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
			for(unsigned int seqNr=0; seqNr<num_sequences; seqNr++)
			{
				LogDebug("Channel %s block %u\n", m_channels[chanNr]->GetHwname().c_str(), seqNr);

				//Ask for the segment of interest
				//(segment number is ignored for non-segmented waveforms)
				if(num_sequences > 1)
				{
					//segment 0 = "all", 1 = first part of capture
					m_transport->SendCommand("WAVEFORM_SETUP SP,0,NP,0,FP,0,SN," + (seqNr+1));
				}

				//Read the actual waveform data
				m_transport->SendCommand(m_channels[chanNr]->GetHwname() + ":WF? DAT2");
				char header[maxWaveHeaderSize] = {0};
				size_t wavesize = ReadWaveHeader(header);
				uint8_t *data = new uint8_t[wavesize];
				m_transport->ReadRawData(wavesize, data);
				// two \n...
				m_transport->ReadReply();
				m_transport->ReadReply();

				double trigtime = 0;
				if( (num_sequences > 1) && (seqNr > 0) )
				{
					//If a multi-segment capture, ask for the trigger time data
					m_transport->SendCommand(m_channels[chanNr]->GetHwname() + ":WF? TIME");

					trigtime = ReadWaveHeader(header);
					// \n
					m_transport->ReadReply();
					//double trigoff = ptrigtime[1];	//offset to point 0 from trigger time
				}

				int64_t trigtime_samples = trigtime * 1e12f / interval;
				//int64_t trigoff_samples = trigoff * 1e12f / interval;
				//LogDebug("	Trigger time: %.3f sec (%lu samples)\n", trigtime, trigtime_samples);
				//LogDebug("	Trigger offset: %.3f sec (%lu samples)\n", trigoff, trigoff_samples);

				//If we have samples already in the capture, stretch the final one to our trigger offset
				/*
				if(cap->m_samples.size())
				{
					auto& last_sample = cap->m_samples[cap->m_samples.size()-1];
					last_sample.m_duration = trigtime_samples - last_sample.m_offset;
				}
				*/

				//Decode the samples
				unsigned int num_samples = wavesize;
				LogDebug("Got %u samples\n", num_samples);
				cap->Resize(num_samples);
				for(unsigned int i=0; i<num_samples; i++)
				{
					cap->m_offsets[i]	= i+trigtime_samples;
					cap->m_durations[i]	= 1;
					if (m_acquiredDataIsSigned)
					{
						// See programming guide, page 267: https://siglentna.com/wp-content/uploads/2020/04/ProgrammingGuide_PG01-E02C.pdf
						// voltage value (V) = code value * (vdiv /25) - voffset
						cap->m_samples[i]	= (int8_t)(data[i]) * (v_gain / 25.0) - v_off;
					}
					else
						cap->m_samples[i]	= data[i] * v_gain - v_off;
				}
			}

			//Done, update the data
			if (!toQueue)
				m_channels[chanNr]->SetData(cap);
			else
				pending_waveforms[chanNr].push_back(cap);
		}
	}

	//At this point all data has been read so the scope is free to go do its thing while we crunch the results.
	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		lock_guard<recursive_mutex> lock(m_mutex);

		m_transport->SendCommand("TRIG_MODE SINGLE");
		m_triggerArmed = true;
	}

	m_pendingWaveformsMutex.lock();
	size_t num_pending = 0;

	if (toQueue)
		num_pending++;

	for(size_t i = 0; i < num_pending; ++i)
	{
		SequenceSet s;
		for(size_t j = 0; j < m_analogChannelCount; j++)
		{
			if(enabled[j])
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	double dt = GetTime() - start;
	LogTrace("Waveform download took %.3f ms\n", dt * 1000);

	return true;
}
