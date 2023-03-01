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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of Oscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"

#include <stdio.h>

#ifdef __x86_64__
#include <immintrin.h>
#endif
#include <omp.h>

#include "EdgeTrigger.h"

using namespace std;

Oscilloscope::CreateMapType Oscilloscope::m_createprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Oscilloscope::Oscilloscope()
{
	m_trigger = NULL;
}

Oscilloscope::~Oscilloscope()
{
	if(m_trigger)
	{
		m_trigger->DetachInputs();
		delete m_trigger;
		m_trigger = NULL;
	}

	for(auto set : m_pendingWaveforms)
	{
		for(auto it : set)
			delete it.second;
	}
	m_pendingWaveforms.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void Oscilloscope::DoAddDriverClass(string name, CreateProcType proc)
{
	m_createprocs[name] = proc;
}

void Oscilloscope::EnumDrivers(vector<string>& names)
{
	for(CreateMapType::iterator it=m_createprocs.begin(); it != m_createprocs.end(); ++it)
		names.push_back(it->first);
}

Oscilloscope* Oscilloscope::CreateOscilloscope(string driver, SCPITransport* transport)
{
	if(m_createprocs.find(driver) != m_createprocs.end())
		return m_createprocs[driver](transport);

	LogError("Invalid driver name");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device properties

void Oscilloscope::FlushConfigCache()
{
	//nothing to do, base class has no caching
}

bool Oscilloscope::IsOffline()
{
	return false;
}

bool Oscilloscope::CanEnableChannel(size_t /*i*/)
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering helpers

bool Oscilloscope::WaitForTrigger(int timeout)
{
	for(int i=0; i<timeout*100; i++)
	{
		if(HasPendingWaveforms())
			return true;
		std::this_thread::sleep_for(std::chrono::microseconds(10 * 1000));
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sequenced capture

size_t Oscilloscope::GetPendingWaveformCount()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	return m_pendingWaveforms.size();
}

bool Oscilloscope::HasPendingWaveforms()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	return (m_pendingWaveforms.size() != 0);
}

/**
	@brief Discard any pending waveforms that haven't yet been processed
 */
void Oscilloscope::ClearPendingWaveforms()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	while(!m_pendingWaveforms.empty())
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			delete it.second;
		m_pendingWaveforms.pop_front();
	}
}

/**
	@brief Pops the queue of pending waveforms and updates each channel with a new waveform
 */
bool Oscilloscope::PopPendingWaveform()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	if(m_pendingWaveforms.size())
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			it.first.m_channel->SetData(it.second, it.first.m_stream);
		m_pendingWaveforms.pop_front();
		return true;
	}
	return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string Oscilloscope::SerializeConfiguration(IDTable& table)
{
	//Save basic scope info
	char tmp[1024];
	int iscope = table.emplace(this);
	snprintf(tmp, sizeof(tmp), "    scope%d:\n", iscope);
	string config = tmp;
	snprintf(tmp, sizeof(tmp), "        id:             %d\n", iscope);
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        nick:           \"%s\"\n", m_nickname.c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        name:           \"%s\"\n", GetName().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        vendor:         \"%s\"\n", GetVendor().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        serial:         \"%s\"\n", GetSerial().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        transport:      \"%s\"\n", GetTransportName().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        args:           \"%s\"\n", GetTransportConnectionString().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        driver:         \"%s\"\n", GetDriverName().c_str());
	config += tmp;

	//Save timebase info
	snprintf(tmp, sizeof(tmp), "        rate:           %ld\n", GetSampleRate());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        depth:          %ld\n", GetSampleDepth());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        interleave:     %d\n", IsInterleaving());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        triggerpos:     %ld\n", GetTriggerOffset());
	config += tmp;

	//Save channels
	config += "        channels:\n";
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		auto chan = GetOscilloscopeChannel(i);

		//Skip any channel that's not an oscilloscope input
		//TODO: new unified object model might require some retooling here since we can have multiple types of channel
		//and not all are oscilloscope channels
		if(!chan)
			continue;

		//skip any kind of math functions etc
		if(!chan->IsPhysicalChannel())
			continue;

		//Basic channel info
		snprintf(tmp, sizeof(tmp), "            ch%zu:\n", i);
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                id:          %d\n", table.emplace(chan));
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                index:       %zu\n", i);
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                color:       \"%s\"\n", chan->m_displaycolor.c_str());
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                nick:        \"%s\"\n", chan->GetDisplayName().c_str());
		config += tmp;
		snprintf(tmp, sizeof(tmp), "                name:        \"%s\"\n", chan->GetHwname().c_str());
		config += tmp;

		if(chan->HasInputMux())
		{
			snprintf(tmp, sizeof(tmp), "                inmux:       %zu\n", chan->GetInputMuxSetting());
			config += tmp;
		}

		//All *hardware* channels have the same type for all streams for now
		switch(chan->GetType(0))
		{
			case Stream::STREAM_TYPE_ANALOG:
				config += "                type:        analog\n";
				if(IsADCModeConfigurable())
				{
					snprintf(tmp, sizeof(tmp), "                adcmode:     %ld\n", GetADCMode(i));
					config += tmp;
				}
				if(chan->CanInvert())
				{
					snprintf(tmp, sizeof(tmp), "                invert:      %d\n", IsInverted(i));
					config += tmp;
				}
				break;
			case Stream::STREAM_TYPE_DIGITAL:
				config += "                type:        digital\n";
				snprintf(tmp, sizeof(tmp), "                thresh:      %f\n", GetDigitalThreshold(i));
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                hys:         %f\n", GetDigitalHysteresis(i));
				config += tmp;
				break;
			case Stream::STREAM_TYPE_TRIGGER:
				config += "                type:        trigger\n";
				break;

			//should never get complex channels on a scope
			//TODO: how to handle digital bus channels? are they possible?
			//TODO: how to handle eye patterns from sampling scope?
			default:
				break;
		}

		//Current channel configuration
		if(chan->IsEnabled())
			config += "                enabled:     1\n";
		else
			config += "                enabled:     0\n";

		snprintf(tmp, sizeof(tmp), "                xunit:       \"%s\"\n", chan->GetXAxisUnits().ToString().c_str());
		config += tmp;

		size_t nstreams = chan->GetStreamCount();
		if(chan->GetType(0) == Stream::STREAM_TYPE_ANALOG)
		{
			snprintf(tmp, sizeof(tmp), "                attenuation: %f\n", chan->GetAttenuation());
			config += tmp;
			snprintf(tmp, sizeof(tmp), "                bwlimit:     %d\n", chan->GetBandwidthLimit());
			config += tmp;

			//single stream unit goes here
			//multi stream unit goes under streams heading
			if(nstreams == 1)
			{
				snprintf(tmp, sizeof(tmp), "                yunit:       \"%s\"\n", chan->GetYAxisUnits(0).ToString().c_str());
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                vrange:      %f\n", chan->GetVoltageRange(0));
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                offset:      %f\n", chan->GetOffset(0));
				config += tmp;
			}

			switch(chan->GetCoupling())
			{
				case OscilloscopeChannel::COUPLE_DC_1M:
					config += "                coupling:    dc_1M\n";
					break;
				case OscilloscopeChannel::COUPLE_AC_1M:
					config += "                coupling:    ac_1M\n";
					break;
				case OscilloscopeChannel::COUPLE_DC_50:
					config += "                coupling:    dc_50\n";
					break;
				case OscilloscopeChannel::COUPLE_GND:
					config += "                coupling:    gnd\n";
					break;

				//should never get synthetic coupling on a scope channel
				default:
					LogWarning("unsupported coupling value when saving\n");
					break;
			}
		}

		//Save streams if there's more than one
		if(nstreams > 1)
		{
			snprintf(tmp, sizeof(tmp), "                nstreams:     %zu\n", nstreams);
			config += tmp;
			snprintf(tmp, sizeof(tmp), "                streams:\n");
			config += tmp;
			for(size_t j=0; j<nstreams; j++)
			{
				snprintf(tmp, sizeof(tmp), "                    stream%zu:\n", j);
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                        index: %zu\n", j);
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                        name: \"%s\"\n", chan->GetStreamName(j).c_str());
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                        yunit:       \"%s\"\n", chan->GetYAxisUnits(j).ToString().c_str());
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                        vrange:      %f\n", chan->GetVoltageRange(j));
				config += tmp;
				snprintf(tmp, sizeof(tmp), "                        offset:      %f\n", chan->GetOffset(j));
				config += tmp;
			}
		}
	}

	//Save trigger
	auto trig = GetTrigger();
	if(trig)
		config += trig->SerializeConfiguration(table);

	return config;
}

void Oscilloscope::LoadConfiguration(const YAML::Node& node, IDTable& table)
{
	m_nickname = node["nick"].as<string>();

	//Load the channels
	auto& chans = node["channels"];
	for(auto it : chans)
	{
		auto& cnode = it.second;
		auto chan = GetOscilloscopeChannel(cnode["index"].as<int>());
		table.emplace(cnode["id"].as<int>(), chan);

		//Ignore name/type.
		//These are only needed for offline scopes to create a representation of the original instrument.

		chan->m_displaycolor = cnode["color"].as<string>();
		chan->SetDisplayName(cnode["nick"].as<string>());

		if(cnode["enabled"].as<int>())
			chan->Enable();
		else
			chan->Disable();

		//Input mux and attenuation control a bunch of the other parameters, so must be changed first
		if(cnode["inmux"])
			chan->SetInputMux(cnode["inmux"].as<int>());
		if(cnode["attenuation"])
			chan->SetAttenuation(cnode["attenuation"].as<float>());

		Unit yunit = chan->GetYAxisUnits(0);
		if(cnode["yunit"])
			yunit = Unit(cnode["yunit"].as<string>());
		if(cnode["vrange"])
			chan->SetVoltageRange(cnode["vrange"].as<float>(), 0);
		if(cnode["offset"])
			chan->SetOffset(cnode["offset"].as<float>(), 0);
		if(cnode["invert"])
			chan->Invert(cnode["invert"].as<int>());

		//Add multiple streams if present
		auto snode = cnode["nstreams"];
		if(snode)
		{
			size_t nstreams = snode.as<size_t>();
			if(nstreams > 1)
			{
				auto stype = chan->GetType(0);

				chan->ClearStreams();

				//We have to keep track of indexes because streams might show up out of order
				//but right now OscilloscopeChannel only lets us add them in order
				map<int, string> names;
				map<int, string> yunits;

				auto streams = cnode["streams"];
				for(auto st : streams)
				{
					auto index = st.second["index"].as<size_t>();
					names[index] = st.second["name"].as<string>();

					if(st.second["yunit"])
						yunits[index] = st.second["yunit"].as<string>();
					else
						yunits[index] = "V";

					if(st.second["vrange"])
						chan->SetVoltageRange(st.second["vrange"].as<float>(), index);
					if(st.second["offset"])
						chan->SetOffset(st.second["offset"].as<float>(), index);
				}

				for(size_t j=0; j<nstreams; j++)
					chan->AddStream(yunit, names[j], stype);
			}
		}

		switch(chan->GetType(0))
		{
			case Stream::STREAM_TYPE_ANALOG:
				chan->SetBandwidthLimit(cnode["bwlimit"].as<int>());

				if(cnode["xunit"])
					chan->SetXAxisUnits(cnode["xunit"].as<string>());

				if(cnode["coupling"])
				{
					string coupling = cnode["coupling"].as<string>();
					if(coupling == "dc_50")
						chan->SetCoupling(OscilloscopeChannel::COUPLE_DC_50);
					else if(coupling == "dc_1M")
						chan->SetCoupling(OscilloscopeChannel::COUPLE_DC_1M);
					else if(coupling == "ac_1M")
						chan->SetCoupling(OscilloscopeChannel::COUPLE_AC_1M);
					else if(coupling == "gnd")
						chan->SetCoupling(OscilloscopeChannel::COUPLE_GND);
				}
				if(cnode["adcmode"])
					SetADCMode(chan->GetIndex(), cnode["adcmode"].as<int>());
				break;

			case Stream::STREAM_TYPE_DIGITAL:
				if(cnode["thresh"])
					chan->SetDigitalThreshold(cnode["thresh"].as<float>());
				if(cnode["hys"])
					chan->SetDigitalHysteresis(cnode["hys"].as<float>());
				break;

			default:
				break;
		}
	}

	//Set sample rate/depth only after channels are in their final state.
	//Interleaving has to be done first, since some rates/depths are only available when interleaved.
	if(CanInterleave())
	{
		if(node["interleave"])
			SetInterleaving(node["interleave"].as<int>());
	}
	if(node["rate"])
		SetSampleRate(node["rate"].as<unsigned long>());
	if(node["depth"])
		SetSampleDepth(node["depth"].as<unsigned long>());
	if(node["triggerpos"])
		SetTriggerOffset(node["triggerpos"].as<int64_t>());

	auto tnode = node["trigger"];
	if(tnode)
	{
		auto trig = Trigger::CreateTrigger(tnode["type"].as<string>(), this);
		trig->LoadParameters(tnode, table);
		trig->LoadInputs(tnode, table);
		SetTrigger(trig);
	}
}

void Oscilloscope::EnableTriggerOutput()
{
	//do nothing, assuming the scope needs no config to enable trigger out
}

bool Oscilloscope::IsSamplingModeAvailable(SamplingMode mode)
{
	return (mode == REAL_TIME);
}

Oscilloscope::SamplingMode Oscilloscope::GetSamplingMode()
{
	return REAL_TIME;
}

void Oscilloscope::SetSamplingMode(SamplingMode /*mode*/)
{
	//default implementation is a no-op
}

void Oscilloscope::SetUseExternalRefclk(bool external)
{
	//override this function in the driver class if an external reference input is present
	if(external)
		LogWarning("Oscilloscope::SetUseExternalRefclk: no external reference supported\n");
}

void Oscilloscope::SetDeskewForChannel(size_t /*channel*/, int64_t /*skew*/)
{
	//override this function in the driver class if deskew is supported
}

int64_t Oscilloscope::GetDeskewForChannel(size_t /*channel*/)
{
	//override this function in the driver class if deskew is supported
	return 0;
}

bool Oscilloscope::CanInterleave()
{
	//Check each conflict in the list
	auto conflicts = GetInterleaveConflicts();
	for(auto c : conflicts)
	{
		if(c.first->IsEnabled() && c.second->IsEnabled())
			return false;
	}

	return true;
}

vector<unsigned int> Oscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;
	ret.push_back(0);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration (default no-op for scopes without MSO feature)

vector<Oscilloscope::DigitalBank> Oscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> ret;
	return ret;
}

Oscilloscope::DigitalBank Oscilloscope::GetDigitalBank(size_t /*channel*/)
{
	return DigitalBank();
}

bool Oscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool Oscilloscope::IsDigitalThresholdConfigurable()
{
	return false;
}

float Oscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0.1;
}

float Oscilloscope::GetDigitalThreshold(size_t /*channel*/)
{
	return 0.5;
}

void Oscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
}

void Oscilloscope::SetDigitalThreshold(size_t /*channel*/, float /*level*/)
{
}

bool Oscilloscope::CanAutoZero(size_t /*i*/)
{
	return false;
}

void Oscilloscope::AutoZero(size_t /*i*/)
{
}

string Oscilloscope::GetProbeName(size_t /*i*/)
{
	return "";
}

bool Oscilloscope::HasInputMux(size_t /*i*/)
{
	return false;
}

size_t Oscilloscope::GetInputMuxSetting(size_t /*i*/)
{
	return 0;
}

vector<string> Oscilloscope::GetInputMuxNames(size_t /*i*/)
{
	vector<string> ret;
	return ret;
}

void Oscilloscope::SetInputMux(size_t /*i*/, size_t /*select*/)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Analog channel configuration

vector<Oscilloscope::AnalogBank> Oscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> ret;
	ret.push_back(GetAnalogBank(0));
	return ret;
}

Oscilloscope::AnalogBank Oscilloscope::GetAnalogBank(size_t /*channel*/)
{
	AnalogBank ret;
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto chan = GetOscilloscopeChannel(i);
		if(chan == nullptr)
			continue;
		if(chan->GetType(0) == Stream::STREAM_TYPE_ANALOG)
			ret.push_back(chan);
	}
	return ret;
}

bool Oscilloscope::IsADCModeConfigurable()
{
	return false;
}

vector<string> Oscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> ret;
	ret.push_back("Default");
	return ret;
}

size_t Oscilloscope::GetADCMode(size_t /*channel*/)
{
	return 0;
}

void Oscilloscope::SetADCMode(size_t /*channel*/, size_t /*mode*/)
{
	//no-op
}

bool Oscilloscope::CanInvert(size_t /*i*/)
{
	return false;
}

void Oscilloscope::Invert(size_t /*i*/, bool /*invert*/)
{
}

bool Oscilloscope::IsInverted(size_t /*i*/)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trigger configuration

vector<string> Oscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(EdgeTrigger::GetTriggerName());
	return ret;
}

bool Oscilloscope::PeekTriggerArmed()
{
	return (PollTrigger() == TRIGGER_MODE_RUN);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spectrum analyzer configuration (default no-op for scopes without SA feature)

void Oscilloscope::SetSpan(int64_t /*span*/)
{
}

int64_t Oscilloscope::GetSpan()
{
	return 1;
}

void Oscilloscope::SetCenterFrequency(size_t /*channel*/, int64_t /*freq*/)
{
}

int64_t Oscilloscope::GetCenterFrequency(size_t /*channel*/)
{
	return 0;
}

void Oscilloscope::SetResolutionBandwidth(int64_t /*freq*/)
{
}

int64_t Oscilloscope::GetResolutionBandwidth()
{
	return 1;
}

bool Oscilloscope::HasFrequencyControls()
{
	return false;
}

bool Oscilloscope::HasTimebaseControls()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers for converting raw 8-bit ADC samples to fp32 waveforms

/**
	@brief Converts 8-bit ADC samples to floating point
 */
void Oscilloscope::Convert8BitSamples(float* pout, int8_t* pin, float gain, float offset, size_t count)
{
	//Divide large waveforms (>1M points) into blocks and multithread them
	//TODO: tune split
	if(count > 1000000)
	{
		//Round blocks to multiples of 32 samples for clean vectorization
		size_t numblocks = omp_get_max_threads();
		size_t lastblock = numblocks - 1;
		size_t blocksize = count / numblocks;
		blocksize = blocksize - (blocksize % 32);

		#pragma omp parallel for
		for(size_t i=0; i<numblocks; i++)
		{
			//Last block gets any extra that didn't divide evenly
			size_t nsamp = blocksize;
			if(i == lastblock)
				nsamp = count - i*blocksize;

			size_t off = i*blocksize;
			#ifdef __x86_64__
			if(g_hasAvx2)
			{
				Convert8BitSamplesAVX2(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
			}
			else
			#endif /* __x86_64__ */
			{
				Convert8BitSamplesGeneric(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
			}
		}
	}

	//Small waveforms get done single threaded to avoid overhead
	else
	{
		#ifdef __x86_64__
		if(g_hasAvx2)
			Convert8BitSamplesAVX2(pout, pin, gain, offset, count);
		else
		#endif
			Convert8BitSamplesGeneric(pout, pin, gain, offset, count);
	}
}

/**
	@brief Generic backend for Convert8BitSamples()
 */
void Oscilloscope::Convert8BitSamplesGeneric(float* pout, int8_t* pin, float gain, float offset, size_t count)
{
	for(unsigned int k=0; k<count; k++)
		pout[k] = pin[k] * gain - offset;
}

#ifdef __x86_64__
/**
	@brief Optimized version of Convert8BitSamples()
 */
__attribute__((target("avx2")))
void Oscilloscope::Convert8BitSamplesAVX2(float* pout, int8_t* pin, float gain, float offset, size_t count)
{
	unsigned int end = count - (count % 32);

	__m256 gains = { gain, gain, gain, gain, gain, gain, gain, gain };
	__m256 offsets = { offset, offset, offset, offset, offset, offset, offset, offset };

	for(unsigned int k=0; k<end; k += 32)
	{
		//Load all 32 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m256i raw_samples = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k));

		//Extract the low and high 16 samples from the block
		__m128i block01_x8 = _mm256_extracti128_si256(raw_samples, 0);
		__m128i block23_x8 = _mm256_extracti128_si256(raw_samples, 1);

		//Swap the low and high halves of these vectors
		//Ugly casting needed because all permute instrinsics expect float/double datatypes
		__m128i block10_x8 = _mm_castpd_si128(_mm_permute_pd(_mm_castsi128_pd(block01_x8), 1));
		__m128i block32_x8 = _mm_castpd_si128(_mm_permute_pd(_mm_castsi128_pd(block23_x8), 1));

		//Divide into blocks of 8 samples and sign extend to 32 bit
		__m256i block0_int = _mm256_cvtepi8_epi32(block01_x8);
		__m256i block1_int = _mm256_cvtepi8_epi32(block10_x8);
		__m256i block2_int = _mm256_cvtepi8_epi32(block23_x8);
		__m256i block3_int = _mm256_cvtepi8_epi32(block32_x8);

		//Convert the 32-bit int blocks to float.
		//Apparently there's no direct epi8 to ps conversion instruction.
		__m256 block0_float = _mm256_cvtepi32_ps(block0_int);
		__m256 block1_float = _mm256_cvtepi32_ps(block1_int);
		__m256 block2_float = _mm256_cvtepi32_ps(block2_int);
		__m256 block3_float = _mm256_cvtepi32_ps(block3_int);

		//Woo! We've finally got floating point data. Now we can do the fun part.
		block0_float = _mm256_mul_ps(block0_float, gains);
		block1_float = _mm256_mul_ps(block1_float, gains);
		block2_float = _mm256_mul_ps(block2_float, gains);
		block3_float = _mm256_mul_ps(block3_float, gains);

		block0_float = _mm256_sub_ps(block0_float, offsets);
		block1_float = _mm256_sub_ps(block1_float, offsets);
		block2_float = _mm256_sub_ps(block2_float, offsets);
		block3_float = _mm256_sub_ps(block3_float, offsets);

		//All done, store back to the output buffer
		_mm256_store_ps(pout + k, 		block0_float);
		_mm256_store_ps(pout + k + 8,	block1_float);
		_mm256_store_ps(pout + k + 16,	block2_float);
		_mm256_store_ps(pout + k + 24,	block3_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(unsigned int k=end; k<count; k++)
		pout[k] = pin[k] * gain - offset;
}
#endif /* __x86_64__ */

/**
	@brief Converts Unsigned 8-bit ADC samples to floating point
 */
void Oscilloscope::ConvertUnsigned8BitSamples(float* pout, uint8_t* pin, float gain, float offset, size_t count)
{
	//Divide large waveforms (>1M points) into blocks and multithread them
	//TODO: tune split
	if(count > 1000000)
	{
		//Round blocks to multiples of 32 samples for clean vectorization
		size_t numblocks = omp_get_max_threads();
		size_t lastblock = numblocks - 1;
		size_t blocksize = count / numblocks;
		blocksize = blocksize - (blocksize % 32);

		#pragma omp parallel for
		for(size_t i=0; i<numblocks; i++)
		{
			//Last block gets any extra that didn't divide evenly
			size_t nsamp = blocksize;
			if(i == lastblock)
				nsamp = count - i*blocksize;

			size_t off = i*blocksize;
			#ifdef __x86_64__
			if(g_hasAvx2)
			{
				ConvertUnsigned8BitSamplesAVX2(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
			}
			else
			#endif
			{
				ConvertUnsigned8BitSamplesGeneric(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
			}
		}
	}

	//Small waveforms get done single threaded to avoid overhead
	else
	{
		#ifdef __x86_64__
		if(g_hasAvx2)
			ConvertUnsigned8BitSamplesAVX2(pout, pin, gain, offset, count);
		else
		#endif
			ConvertUnsigned8BitSamplesGeneric(pout, pin, gain, offset, count);
	}
}

/**
	@brief Generic backend for ConvertUnsigned8BitSamples()
 */
void Oscilloscope::ConvertUnsigned8BitSamplesGeneric(float* pout, uint8_t* pin, float gain, float offset, size_t count)
{
	for(unsigned int k=0; k<count; k++)
		pout[k] = pin[k] * gain - offset;
}

#ifdef __x86_64__
/**
	@brief Optimized version of ConvertUnsigned8BitSamples()
 */
__attribute__((target("avx2")))
void Oscilloscope::ConvertUnsigned8BitSamplesAVX2(float* pout, uint8_t* pin, float gain, float offset, size_t count)
{
	unsigned int end = count - (count % 32);

	__m256 gains = { gain, gain, gain, gain, gain, gain, gain, gain };
	__m256 offsets = { offset, offset, offset, offset, offset, offset, offset, offset };

	for(unsigned int k=0; k<end; k += 32)
	{
		//Load all 32 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m256i raw_samples = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k));

		//Extract the low and high 16 samples from the block
		__m128i block01_x8 = _mm256_extracti128_si256(raw_samples, 0);
		__m128i block23_x8 = _mm256_extracti128_si256(raw_samples, 1);

		//Swap the low and high halves of these vectors
		//Ugly casting needed because all permute instrinsics expect float/double datatypes
		__m128i block10_x8 = _mm_castpd_si128(_mm_permute_pd(_mm_castsi128_pd(block01_x8), 1));
		__m128i block32_x8 = _mm_castpd_si128(_mm_permute_pd(_mm_castsi128_pd(block23_x8), 1));

		//Divide into blocks of 8 samples and sign extend to 32 bit
		__m256i block0_int = _mm256_cvtepu8_epi32(block01_x8);
		__m256i block1_int = _mm256_cvtepu8_epi32(block10_x8);
		__m256i block2_int = _mm256_cvtepu8_epi32(block23_x8);
		__m256i block3_int = _mm256_cvtepu8_epi32(block32_x8);

		//Convert the 32-bit int blocks to float.
		//Apparently there's no direct epi8 to ps conversion instruction.
		__m256 block0_float = _mm256_cvtepi32_ps(block0_int);
		__m256 block1_float = _mm256_cvtepi32_ps(block1_int);
		__m256 block2_float = _mm256_cvtepi32_ps(block2_int);
		__m256 block3_float = _mm256_cvtepi32_ps(block3_int);

		//Woo! We've finally got floating point data. Now we can do the fun part.
		block0_float = _mm256_mul_ps(block0_float, gains);
		block1_float = _mm256_mul_ps(block1_float, gains);
		block2_float = _mm256_mul_ps(block2_float, gains);
		block3_float = _mm256_mul_ps(block3_float, gains);

		block0_float = _mm256_sub_ps(block0_float, offsets);
		block1_float = _mm256_sub_ps(block1_float, offsets);
		block2_float = _mm256_sub_ps(block2_float, offsets);
		block3_float = _mm256_sub_ps(block3_float, offsets);

		//All done, store back to the output buffer
		_mm256_store_ps(pout + k, 		block0_float);
		_mm256_store_ps(pout + k + 8,	block1_float);
		_mm256_store_ps(pout + k + 16,	block2_float);
		_mm256_store_ps(pout + k + 24,	block3_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(unsigned int k=end; k<count; k++)
		pout[k] = pin[k] * gain - offset;
}
#endif /* __x86_64__ */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers for converting raw 16-bit ADC samples to fp32 waveforms

/**
	@brief Converts 16-bit ADC samples to floating point
 */
void Oscilloscope::Convert16BitSamples(float* pout, int16_t* pin, float gain, float offset, size_t count)
{
	//Divide large waveforms (>1M points) into blocks and multithread them
	//TODO: tune split
	if(count > 1000000)
	{
		//Round blocks to multiples of 64 samples for clean vectorization
		size_t numblocks = omp_get_max_threads();
		size_t lastblock = numblocks - 1;
		size_t blocksize = count / numblocks;
		blocksize = blocksize - (blocksize % 64);

		#pragma omp parallel for
		for(size_t i=0; i<numblocks; i++)
		{
			//Last block gets any extra that didn't divide evenly
			size_t nsamp = blocksize;
			if(i == lastblock)
				nsamp = count - i*blocksize;

			size_t off = i*blocksize;
			#ifdef __x86_64__
			if(g_hasAvx512F)
			{
				Convert16BitSamplesAVX512F(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
			}
			else if(g_hasAvx2)
			{
				if(g_hasFMA)
				{
					Convert16BitSamplesFMA(
						pout + off,
						pin + off,
						gain,
						offset,
						nsamp);
				}
				else
				{
					Convert16BitSamplesAVX2(
						pout + off,
						pin + off,
						gain,
						offset,
						nsamp);
				}
			}
			else
			#endif /* __x86_64__ */
			{
				Convert16BitSamplesGeneric(
					pout + off,
					pin + off,
					gain,
					offset,
					nsamp);
			}
		}
	}

	//Small waveforms get done single threaded to avoid overhead
	else
	{
		#ifdef __x86_64__
		if(g_hasAvx2)
		{
			if(g_hasFMA)
				Convert16BitSamplesFMA(pout, pin, gain, offset, count);
			else
				Convert16BitSamplesAVX2(pout, pin, gain, offset, count);
		}
		else
		#endif /* __x86_64__ */
			Convert16BitSamplesGeneric(pout, pin, gain, offset, count);
	}
}

/**
	@brief Converts raw ADC samples to floating point
 */
void Oscilloscope::Convert16BitSamplesGeneric(float* pout, int16_t* pin, float gain, float offset, size_t count)
{
	for(size_t j=0; j<count; j++)
		pout[j] = gain*pin[j] - offset;
}

#ifdef __x86_64__
__attribute__((target("avx2")))
void Oscilloscope::Convert16BitSamplesAVX2(float* pout, int16_t* pin, float gain, float offset, size_t count)
{
	size_t end = count - (count % 32);

	__m256 gains = { gain, gain, gain, gain, gain, gain, gain, gain };
	__m256 offsets = { offset, offset, offset, offset, offset, offset, offset, offset };

	for(size_t k=0; k<end; k += 32)
	{
		//Load all 32 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m256i raw_samples1 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k));
		__m256i raw_samples2 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k + 16));

		//Extract the low and high halves (8 samples each) from the input blocks
		__m128i block0_i16 = _mm256_extracti128_si256(raw_samples1, 0);
		__m128i block1_i16 = _mm256_extracti128_si256(raw_samples1, 1);
		__m128i block2_i16 = _mm256_extracti128_si256(raw_samples2, 0);
		__m128i block3_i16 = _mm256_extracti128_si256(raw_samples2, 1);

		//Convert both blocks from 16 to 32 bit, giving us a pair of 8x int32 vectors
		__m256i block0_i32 = _mm256_cvtepi16_epi32(block0_i16);
		__m256i block1_i32 = _mm256_cvtepi16_epi32(block1_i16);
		__m256i block2_i32 = _mm256_cvtepi16_epi32(block2_i16);
		__m256i block3_i32 = _mm256_cvtepi16_epi32(block3_i16);

		//Convert the 32-bit int blocks to fp32
		//Sadly there's no direct epi16 to ps conversion instruction.
		__m256 block0_float = _mm256_cvtepi32_ps(block0_i32);
		__m256 block1_float = _mm256_cvtepi32_ps(block1_i32);
		__m256 block2_float = _mm256_cvtepi32_ps(block2_i32);
		__m256 block3_float = _mm256_cvtepi32_ps(block3_i32);

		//Woo! We've finally got floating point data. Now we can do the fun part.
		block0_float = _mm256_mul_ps(block0_float, gains);
		block1_float = _mm256_mul_ps(block1_float, gains);
		block2_float = _mm256_mul_ps(block2_float, gains);
		block3_float = _mm256_mul_ps(block3_float, gains);

		block0_float = _mm256_sub_ps(block0_float, offsets);
		block1_float = _mm256_sub_ps(block1_float, offsets);
		block2_float = _mm256_sub_ps(block2_float, offsets);
		block3_float = _mm256_sub_ps(block3_float, offsets);

		//All done, store back to the output buffer
		_mm256_store_ps(pout + k, 		block0_float);
		_mm256_store_ps(pout + k + 8,	block1_float);
		_mm256_store_ps(pout + k + 16,	block2_float);
		_mm256_store_ps(pout + k + 24,	block3_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(size_t k=end; k<count; k++)
		pout[k] = pin[k] * gain - offset;
}

__attribute__((target("avx2,fma")))
void Oscilloscope::Convert16BitSamplesFMA(float* pout, int16_t* pin, float gain, float offset, size_t count)
{
	size_t end = count - (count % 64);

	__m256 gains = { gain, gain, gain, gain, gain, gain, gain, gain };
	__m256 offsets = { offset, offset, offset, offset, offset, offset, offset, offset };

	for(size_t k=0; k<end; k += 64)
	{
		//Load all 64 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m256i raw_samples1 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k));
		__m256i raw_samples2 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k + 16));
		__m256i raw_samples3 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k + 32));
		__m256i raw_samples4 = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k + 48));

		//Extract the low and high halves (8 samples each) from the input blocks
		__m128i block0_i16 = _mm256_extracti128_si256(raw_samples1, 0);
		__m128i block1_i16 = _mm256_extracti128_si256(raw_samples1, 1);
		__m128i block2_i16 = _mm256_extracti128_si256(raw_samples2, 0);
		__m128i block3_i16 = _mm256_extracti128_si256(raw_samples2, 1);
		__m128i block4_i16 = _mm256_extracti128_si256(raw_samples3, 0);
		__m128i block5_i16 = _mm256_extracti128_si256(raw_samples3, 1);
		__m128i block6_i16 = _mm256_extracti128_si256(raw_samples4, 0);
		__m128i block7_i16 = _mm256_extracti128_si256(raw_samples4, 1);

		//Convert the blocks from 16 to 32 bit, giving us a pair of 8x int32 vectors
		__m256i block0_i32 = _mm256_cvtepi16_epi32(block0_i16);
		__m256i block1_i32 = _mm256_cvtepi16_epi32(block1_i16);
		__m256i block2_i32 = _mm256_cvtepi16_epi32(block2_i16);
		__m256i block3_i32 = _mm256_cvtepi16_epi32(block3_i16);
		__m256i block4_i32 = _mm256_cvtepi16_epi32(block4_i16);
		__m256i block5_i32 = _mm256_cvtepi16_epi32(block5_i16);
		__m256i block6_i32 = _mm256_cvtepi16_epi32(block6_i16);
		__m256i block7_i32 = _mm256_cvtepi16_epi32(block7_i16);

		//Convert the 32-bit int blocks to fp32
		//Sadly there's no direct epi16 to ps conversion instruction.
		__m256 block0_float = _mm256_cvtepi32_ps(block0_i32);
		__m256 block1_float = _mm256_cvtepi32_ps(block1_i32);
		__m256 block2_float = _mm256_cvtepi32_ps(block2_i32);
		__m256 block3_float = _mm256_cvtepi32_ps(block3_i32);
		__m256 block4_float = _mm256_cvtepi32_ps(block4_i32);
		__m256 block5_float = _mm256_cvtepi32_ps(block5_i32);
		__m256 block6_float = _mm256_cvtepi32_ps(block6_i32);
		__m256 block7_float = _mm256_cvtepi32_ps(block7_i32);

		//Woo! We've finally got floating point data. Now we can do the fun part.
		block0_float = _mm256_fmsub_ps(block0_float, gains, offsets);
		block1_float = _mm256_fmsub_ps(block1_float, gains, offsets);
		block2_float = _mm256_fmsub_ps(block2_float, gains, offsets);
		block3_float = _mm256_fmsub_ps(block3_float, gains, offsets);
		block4_float = _mm256_fmsub_ps(block4_float, gains, offsets);
		block5_float = _mm256_fmsub_ps(block5_float, gains, offsets);
		block6_float = _mm256_fmsub_ps(block6_float, gains, offsets);
		block7_float = _mm256_fmsub_ps(block7_float, gains, offsets);

		//All done, store back to the output buffer
		_mm256_store_ps(pout + k, 		block0_float);
		_mm256_store_ps(pout + k + 8,	block1_float);
		_mm256_store_ps(pout + k + 16,	block2_float);
		_mm256_store_ps(pout + k + 24,	block3_float);

		_mm256_store_ps(pout + k + 32,	block4_float);
		_mm256_store_ps(pout + k + 40,	block5_float);
		_mm256_store_ps(pout + k + 48,	block6_float);
		_mm256_store_ps(pout + k + 56,	block7_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(size_t k=end; k<count; k++)
		pout[k] = pin[k] * gain - offset;
}

__attribute__((target("avx512f")))
void Oscilloscope::Convert16BitSamplesAVX512F(float* pout, int16_t* pin, float gain, float offset, size_t count)
{
	size_t end = count - (count % 64);

	__m512 gains = _mm512_set1_ps(gain);
	__m512 offsets = _mm512_set1_ps(offset);

	for(size_t k=0; k<end; k += 64)
	{
		//Load all 64 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m512i raw_samples1 = _mm512_loadu_si512(reinterpret_cast<__m512i*>(pin + k));
		__m512i raw_samples2 = _mm512_loadu_si512(reinterpret_cast<__m512i*>(pin + k + 32));

		//Extract the high and low halves (16 samples each) from the input blocks
		__m256i block0_i16 = _mm512_extracti64x4_epi64(raw_samples1, 0);
		__m256i block1_i16 = _mm512_extracti64x4_epi64(raw_samples1, 1);
		__m256i block2_i16 = _mm512_extracti64x4_epi64(raw_samples2, 0);
		__m256i block3_i16 = _mm512_extracti64x4_epi64(raw_samples2, 1);

		//Convert the blocks from 16 to 32 bit, giving us a pair of 16x int32 vectors
		__m512i block0_i32 = _mm512_cvtepi16_epi32(block0_i16);
		__m512i block1_i32 = _mm512_cvtepi16_epi32(block1_i16);
		__m512i block2_i32 = _mm512_cvtepi16_epi32(block2_i16);
		__m512i block3_i32 = _mm512_cvtepi16_epi32(block3_i16);

		//Convert the 32-bit int blocks to fp32
		//Sadly there's no direct epi16 to ps conversion instruction.
		__m512 block0_float = _mm512_cvtepi32_ps(block0_i32);
		__m512 block1_float = _mm512_cvtepi32_ps(block1_i32);
		__m512 block2_float = _mm512_cvtepi32_ps(block2_i32);
		__m512 block3_float = _mm512_cvtepi32_ps(block3_i32);

		//Woo! We've finally got floating point data. Now we can do the fun part.
		block0_float = _mm512_fmsub_ps(block0_float, gains, offsets);
		block1_float = _mm512_fmsub_ps(block1_float, gains, offsets);
		block2_float = _mm512_fmsub_ps(block2_float, gains, offsets);
		block3_float = _mm512_fmsub_ps(block3_float, gains, offsets);

		//All done, store back to the output buffer
		_mm512_store_ps(pout + k, 		block0_float);
		_mm512_store_ps(pout + k + 16,	block1_float);
		_mm512_store_ps(pout + k + 32,	block2_float);
		_mm512_store_ps(pout + k + 48,	block3_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(size_t k=end; k<count; k++)
		pout[k] = pin[k] * gain - offset;
}
#endif /* __x86_64__ */

