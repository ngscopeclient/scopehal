/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "AseqSpectrometer.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

AseqSpectrometer::AseqSpectrometer(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	//Create the channel
	auto chan = new OscilloscopeChannel(
		this,
		"Spectrum",
		"#4040ff",
		Unit(Unit::UNIT_PM),
		Unit(Unit::UNIT_COUNTS),
		Stream::STREAM_TYPE_ANALOG,
		0);
	m_channels.push_back(chan);

	//default to reasonable full scale range
	chan->SetVoltageRange(30000, 0);
	chan->SetOffset(-15000, 0);

	/*
	//Add the external trigger input
	m_extTrigChannel =
		new OscilloscopeChannel(
		this,
		"EX",
		"#808080",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_COUNTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_channels.push_back(m_extTrigChannel);
	m_extTrigChannel->SetDefaultDisplayName();

	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(GetOscilloscopeChannel(0)));
	SetTrigger(trig);
	PushTrigger();
	SetTriggerOffset(10 * 1000L * 1000L);
	*/
}

AseqSpectrometer::~AseqSpectrometer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int AseqSpectrometer::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t AseqSpectrometer::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

vector<uint64_t> AseqSpectrometer::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(GetSampleDepth());
	return ret;
}

OscilloscopeChannel* AseqSpectrometer::GetExternalTrigger()
{
	return nullptr;
}

uint64_t AseqSpectrometer::GetSampleRate()
{
	return 1;
}

uint64_t AseqSpectrometer::GetSampleDepth()
{
	return 3653;
}

void AseqSpectrometer::SetSampleDepth(uint64_t /*depth*/)
{
}

void AseqSpectrometer::SetSampleRate(uint64_t /*rate*/)
{
}

void AseqSpectrometer::Start()
{
}

void AseqSpectrometer::StartSingleTrigger()
{
}

void AseqSpectrometer::Stop()
{
}

void AseqSpectrometer::ForceTrigger()
{
}

string AseqSpectrometer::GetDriverNameInternal()
{
	return "aseq";
}

void AseqSpectrometer::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

void AseqSpectrometer::PushTrigger()
{
}

void AseqSpectrometer::PullTrigger()
{
}

bool AseqSpectrometer::IsTriggerArmed()
{
	//temp: always on
	return true;
}

Oscilloscope::TriggerMode AseqSpectrometer::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool AseqSpectrometer::AcquireData()
{
	/*
	#pragma pack(push, 1)
	struct
	{
		//Number of channels in the current waveform
		uint16_t numChannels;

		//Sample interval.
		//May be different from m_srate if we changed the rate after the trigger was armed
		int64_t fs_per_sample;
	} wfmhdrs;
	#pragma pack(pop)

	//Read global waveform settings (independent of each channel)
	if(!m_transport->ReadRawData(sizeof(wfmhdrs), (uint8_t*)&wfmhdrs))
		return false;
	uint16_t numChannels = wfmhdrs.numChannels;
	int64_t fs_per_sample = wfmhdrs.fs_per_sample;

	//Acquire data for each channel
	size_t chnum;
	size_t memdepth;
	float config[3];
	SequenceSet s;
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Analog channels get processed separately
	vector<UniformAnalogWaveform*> awfms;
	vector<size_t> achans;
	vector<float> scales;
	vector<float> offsets;

	for(size_t i=0; i<numChannels; i++)
	{
		size_t tmp[2];

		//Get channel ID and memory depth (samples, not bytes)
		if(!m_transport->ReadRawData(sizeof(tmp), (uint8_t*)&tmp))
			return false;
		chnum = tmp[0];
		memdepth = tmp[1];

		//Analog channels
		if(chnum < m_analogChannelCount)
		{
			auto& abuf = m_analogRawWaveformBuffers[chnum];
			abuf->resize(memdepth);
			abuf->PrepareForCpuAccess();
			achans.push_back(chnum);

			//Scale and offset are sent in the header since they might have changed since the capture began
			if(!m_transport->ReadRawData(sizeof(config), (uint8_t*)&config))
				return false;
			float scale = config[0];
			float offset = config[1];
			float trigphase = -config[2] * fs_per_sample;
			scale *= GetChannelAttenuation(chnum);
			offset *= GetChannelAttenuation(chnum);

			//TODO: stream timestamp from the server
			if(!m_transport->ReadRawData(memdepth * sizeof(int16_t), reinterpret_cast<uint8_t*>(abuf->GetCpuPointer())))
				return false;

			abuf->MarkModifiedFromCpu();

			//Create our waveform
			auto cap = AllocateAnalogWaveform(m_nickname + "." + GetOscilloscopeChannel(i)->GetHwname());
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = trigphase;
			cap->m_startTimestamp = time(NULL);
			cap->m_startFemtoseconds = fs;
			cap->Resize(memdepth);
			awfms.push_back(cap);
			scales.push_back(scale);
			offsets.push_back(offset);

			s[GetOscilloscopeChannel(chnum)] = cap;
		}

		//Digital pod
		else
		{
			int16_t* buf = new int16_t[memdepth];

			float trigphase;
			if(!m_transport->ReadRawData(sizeof(trigphase), (uint8_t*)&trigphase))
				return false;
			trigphase = -trigphase * fs_per_sample;
			if(!m_transport->ReadRawData(memdepth * sizeof(int16_t), (uint8_t*)buf))
				return false;

			size_t podnum = chnum - m_analogChannelCount;
			if(podnum > 2)
			{
				LogError("Digital pod number was >2 (chnum = %zu). Possible protocol desync or data corruption?\n",
						 chnum);
				return false;
			}

			//Create buffers for output waveforms
			SparseDigitalWaveform* caps[8];
			for(size_t j=0; j<8; j++)
			{
				auto nchan = m_digitalChannelBase + 8*podnum + j;
				caps[j] = AllocateDigitalWaveform(m_nickname + "." + GetOscilloscopeChannel(nchan)->GetHwname());
				s[GetOscilloscopeChannel(nchan) ] = caps[j];
			}

			//Now that we have the waveform data, unpack it into individual channels
			#pragma omp parallel for
			for(size_t j=0; j<8; j++)
			{
				//Bitmask for this digital channel
				int16_t mask = (1 << j);

				//Create the waveform
				auto cap = caps[j];
				cap->m_timescale = fs_per_sample;
				cap->m_triggerPhase = trigphase;
				cap->m_startTimestamp = time(NULL);
				cap->m_startFemtoseconds = fs;

				//Preallocate memory assuming no deduplication possible
				cap->Resize(memdepth);
				cap->PrepareForCpuAccess();

				//First sample never gets deduplicated
				bool last = (buf[0] & mask) ? true : false;
				size_t k = 0;
				cap->m_offsets[0] = 0;
				cap->m_durations[0] = 1;
				cap->m_samples[0] = last;

				//Read and de-duplicate the other samples
				//TODO: can we vectorize this somehow?
				for(size_t m=1; m<memdepth; m++)
				{
					bool sample = (buf[m] & mask) ? true : false;

					//Deduplicate consecutive samples with same value
					//FIXME: temporary workaround for rendering bugs
					//if(last == sample)
					if( (last == sample) && ((m+3) < memdepth) )
						cap->m_durations[k] ++;

					//Nope, it toggled - store the new value
					else
					{
						k++;
						cap->m_offsets[k] = m;
						cap->m_durations[k] = 1;
						cap->m_samples[k] = sample;
						last = sample;
					}
				}

				//Free space reclaimed by deduplication
				cap->Resize(k);
				cap->m_offsets.shrink_to_fit();
				cap->m_durations.shrink_to_fit();
				cap->m_samples.shrink_to_fit();
				cap->MarkSamplesModifiedFromCpu();
				cap->MarkTimestampsModifiedFromCpu();
			}

			delete[] buf;
		}
	}

	//If we have GPU support for int16, we can do the conversion on the card
	//But only do this if we also have push-descriptor support, because doing N separate dispatches is likely
	//to be slower than a parallel CPU-side conversion
	//Note also that a strict benchmarking here may be slower than the CPU version due to transfer latency,
	//but having the waveform on the GPU now means we don't have to do *that* later.
	if(g_hasShaderInt16 && g_hasPushDescriptor)
	{
		m_cmdBuf->begin({});

		m_conversionPipeline->Bind(*m_cmdBuf);

		for(size_t i=0; i<awfms.size(); i++)
		{
			auto cap = awfms[i];

			m_conversionPipeline->BindBufferNonblocking(0, cap->m_samples, *m_cmdBuf, true);
			m_conversionPipeline->BindBufferNonblocking(1, *m_analogRawWaveformBuffers[achans[i]], *m_cmdBuf);

			ConvertRawSamplesShaderArgs args;
			args.size = cap->size();
			args.gain = scales[i];
			args.offset = -offsets[i];

			m_conversionPipeline->DispatchNoRebind(*m_cmdBuf, args, GetComputeBlockCount(cap->size(), 64));

			cap->MarkModifiedFromGpu();
		}

		m_cmdBuf->end();
		m_queue->SubmitAndBlock(*m_cmdBuf);
	}
	else
	{
		//Fallback path
		//Process analog captures in parallel
		#pragma omp parallel for
		for(size_t i=0; i<awfms.size(); i++)
		{
			auto cap = awfms[i];
			cap->PrepareForCpuAccess();
			Convert16BitSamples(
				cap->m_samples.GetCpuPointer(),
				m_analogRawWaveformBuffers[achans[i]]->GetCpuPointer(),
				scales[i],
				-offsets[i],
				cap->size());

			cap->MarkSamplesModifiedFromCpu();
		}
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;
	*/
	return true;
}
