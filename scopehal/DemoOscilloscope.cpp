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
	@brief Implementation of DemoOscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "DemoOscilloscope.h"
#include <random>
#include <complex>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DemoOscilloscope::DemoOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport, false)
	, m_extTrigger(NULL)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
{
	m_model = "Oscilloscope Simulator";
	m_vendor = "Antikernel Labs";
	m_serial = "12345";

	//Create a bunch of channels

	static const char* colors[8] =
	{ "#ffff00", "#ff6abc", "#00ffff", "#00c100", "#d7ffd7", "#8482ff", "#ff0000", "#ff8000" };

	for(size_t i=0; i<4; i++)
	{
		m_channels.push_back(
			new OscilloscopeChannel(
			this,
			string("CH")+to_string(i+1),
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			colors[i],
			1,
			i,
			true));

		//initial configuration is 1V p-p for each
		m_channelsEnabled[i] = true;
		m_channelCoupling[i] = OscilloscopeChannel::COUPLE_DC_50;
		m_channelAttenuation[i] = 10;
		m_channelBandwidth[i] = 0;
		m_channelVoltageRange[i] = 1;
		m_channelOffset[i] = 0;
	}

	m_forwardPlan = NULL;
	m_reversePlan = NULL;

	m_cachedNumPoints = 0;
	m_cachedRawSize = 0;

	m_forwardInBuf = NULL;
	m_forwardOutBuf = NULL;
	m_reverseOutBuf = NULL;

	m_sweepFreq = 1e9;

	//Default sampling configuration
	m_depth = 100e3;
	m_rate = 50e9;

	m_channels[0]->m_displayname = "Tone";
	m_channels[1]->m_displayname = "Ramp";
	m_channels[2]->m_displayname = "PRBS31";
	m_channels[3]->m_displayname = "8B10B";
}

DemoOscilloscope::~DemoOscilloscope()
{
	if(m_forwardPlan)
		ffts_free(m_forwardPlan);
	if(m_reversePlan)
		ffts_free(m_reversePlan);

	m_allocator.deallocate(m_forwardInBuf);
	m_allocator.deallocate(m_forwardOutBuf);
	m_allocator.deallocate(m_reverseOutBuf);

	m_forwardPlan = NULL;
	m_reversePlan = NULL;
	m_forwardInBuf = NULL;
	m_forwardOutBuf = NULL;
	m_reverseOutBuf = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

string DemoOscilloscope::IDPing()
{
	return "";
}

string DemoOscilloscope::GetTransportName()
{
	return "null";
}

string DemoOscilloscope::GetTransportConnectionString()
{
	return "";
}

string DemoOscilloscope::GetDriverNameInternal()
{
	return "demo";
}

unsigned int DemoOscilloscope::GetInstrumentTypes()
{
	return INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerMode DemoOscilloscope::PollTrigger()
{
	if(m_triggerArmed)
		return TRIGGER_MODE_TRIGGERED;
	else
		return TRIGGER_MODE_STOP;
}

void DemoOscilloscope::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void DemoOscilloscope::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void DemoOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

bool DemoOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void DemoOscilloscope::LoadConfiguration(const YAML::Node& node, IDTable& table)
{
	//Load the channels
	auto& chans = node["channels"];
	for(auto it : chans)
	{
		auto& cnode = it.second;

		//Allocate channel space if we didn't have it yet
		size_t index = cnode["index"].as<int>();
		if(m_channels.size() < (index+1))
			m_channels.resize(index+1);

		//Configure the channel
		OscilloscopeChannel::ChannelType type = OscilloscopeChannel::CHANNEL_TYPE_COMPLEX;
		string stype = cnode["type"].as<string>();
		if(stype == "analog")
			type = OscilloscopeChannel::CHANNEL_TYPE_ANALOG;
		else if(stype == "digital")
			type = OscilloscopeChannel::CHANNEL_TYPE_DIGITAL;
		else if(stype == "trigger")
			type = OscilloscopeChannel::CHANNEL_TYPE_TRIGGER;
		auto chan = new OscilloscopeChannel(
			this,
			cnode["name"].as<string>(),
			type,
			cnode["color"].as<string>(),
			1,
			index,
			true);
		m_channels[index] = chan;

		//Create the channel ID
		table.emplace(cnode["id"].as<int>(), chan);
	}

	//Call the base class to configure everything
	Oscilloscope::LoadConfiguration(node, table);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration. Mostly trivial stubs.

bool DemoOscilloscope::IsChannelEnabled(size_t i)
{
	return m_channelsEnabled[i];
}

void DemoOscilloscope::EnableChannel(size_t i)
{
	m_channelsEnabled[i] = true;
}

void DemoOscilloscope::DisableChannel(size_t i)
{
	m_channelsEnabled[i] = false;
}

OscilloscopeChannel::CouplingType DemoOscilloscope::GetChannelCoupling(size_t i)
{
	return m_channelCoupling[i];
}

void DemoOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	m_channelCoupling[i] = type;
}

double DemoOscilloscope::GetChannelAttenuation(size_t i)
{
	return m_channelAttenuation[i];
}

void DemoOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	m_channelAttenuation[i] = atten;
}

int DemoOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	return m_channelBandwidth[i];
}

void DemoOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	m_channelBandwidth[i] = limit_mhz;
}

double DemoOscilloscope::GetChannelVoltageRange(size_t i)
{
	return m_channelVoltageRange[i];
}

void DemoOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	m_channelVoltageRange[i] = range;
}

OscilloscopeChannel* DemoOscilloscope::GetExternalTrigger()
{
	return m_extTrigger;
}

double DemoOscilloscope::GetChannelOffset(size_t i)
{
	return m_channelOffset[i];
}

void DemoOscilloscope::SetChannelOffset(size_t i, double offset)
{
	m_channelOffset[i] = offset;
}

vector<uint64_t> DemoOscilloscope::GetSampleRatesNonInterleaved()
{
	uint64_t k = 1000;
	uint64_t m = k * k;
	uint64_t g = k * m;

	vector<uint64_t> ret;
	ret.push_back(1 * g);
	ret.push_back(5 * g);
	ret.push_back(10 * g);
	ret.push_back(25 * g);
	ret.push_back(50 * g);
	ret.push_back(100 * g);
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleRatesInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> DemoOscilloscope::GetInterleaveConflicts()
{
	//no-op
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleDepthsNonInterleaved()
{
	uint64_t k = 1000;
	uint64_t m = k * k;

	vector<uint64_t> ret;
	ret.push_back(10 * k);
	ret.push_back(100 * k);
	ret.push_back(1 * m);
	ret.push_back(10 * m);
	return ret;
}

vector<uint64_t> DemoOscilloscope::GetSampleDepthsInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

uint64_t DemoOscilloscope::GetSampleRate()
{
	return m_rate;
}

uint64_t DemoOscilloscope::GetSampleDepth()
{
	return m_depth;
}

void DemoOscilloscope::SetSampleDepth(uint64_t depth)
{
	m_depth = depth;
}

void DemoOscilloscope::SetSampleRate(uint64_t rate)
{
	m_rate = rate;
}

void DemoOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t DemoOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}

bool DemoOscilloscope::IsInterleaving()
{
	return false;
}

bool DemoOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void DemoOscilloscope::PushTrigger()
{
	//no-op
}

void DemoOscilloscope::PullTrigger()
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waveform synthesis

bool DemoOscilloscope::AcquireData()
{
	//cap waveform rate at 50 wfm/s to avoid saturating cpu
	std::this_thread::sleep_for(std::chrono::microseconds(20 * 1000));

	//Sweeping frequency
	m_sweepFreq += 1e6;
	if(m_sweepFreq > 1.5e9)
		m_sweepFreq = 1.1e9;
	float sweepPeriod = 1e12 / m_sweepFreq;

	//Generate waveforms
	SequenceSet s;
	auto depth = GetSampleDepth();
	int64_t sampleperiod = 1e12 / m_rate;
	s[m_channels[0]] = GenerateNoisySinewave(0.9, 0.0, 1000, sampleperiod, depth);
	s[m_channels[1]] = GenerateNoisySinewaveMix(0.9, 0.0, M_PI_4, 1000, sweepPeriod, sampleperiod, depth);
	s[m_channels[2]] = GeneratePRBS31(0.9, 96.9696, sampleperiod, depth);
	s[m_channels[3]] = Generate8b10b(0.9, 800, sampleperiod, depth);

	//Timestamp the waveform(s)
	float now = GetTime();
	float tfrac = fmodf(now, 1);
	time_t start = round(now - tfrac);
	int64_t ps = round(tfrac * 1e12);
	for(auto it : s)
	{
		auto wfm = it.second;

		wfm->m_startTimestamp = start;
		wfm->m_startPicoseconds = ps;
		wfm->m_triggerPhase = 0;
	}

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

/**
	@brief Generates a sinewave with a bit of extra noise added
 */
WaveformBase* DemoOscilloscope::GenerateNoisySinewave(
	float amplitude,
	float startphase,
	int64_t period,
	int64_t sampleperiod,
	size_t depth)
{
	auto ret = new AnalogWaveform;
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	const float noise_amplitude = 0.010;	//gaussian noise w/ 10 mV stdev

	random_device rd;
	mt19937 rng(rd());
	normal_distribution<> noise(0, noise_amplitude);

	float samples_per_cycle = period * 1.0 / sampleperiod;
	float radians_per_sample = 2 * M_PI / samples_per_cycle;

	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor
	float scale = amplitude / 2;

	for(size_t i=0; i<depth; i++)
	{
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

		ret->m_samples[i] = scale * sinf(i*radians_per_sample + startphase) + noise(rng);
	}

	return ret;
}

/**
	@brief Generates a mix of two sinewaves plus some noise
 */
WaveformBase* DemoOscilloscope::GenerateNoisySinewaveMix(
	float amplitude,
	float startphase1,
	float startphase2,
	float period1,
	float period2,
	int64_t sampleperiod,
	size_t depth)
{
	auto ret = new AnalogWaveform;
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	const float noise_amplitude = 0.010;	//gaussian noise w/ 10 mV stdev

	random_device rd;
	mt19937 rng(rd());
	normal_distribution<> noise(0, noise_amplitude);

	float radians_per_sample1 = 2 * M_PI * sampleperiod / period1;
	float radians_per_sample2 = 2 * M_PI * sampleperiod / period2;

	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor.
	//Divide by 2 again to avoid clipping the sum of them
	float scale = amplitude / 4;

	for(size_t i=0; i<depth; i++)
	{
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

		ret->m_samples[i] = scale *
			(sinf(i*radians_per_sample1 + startphase1) + sinf(i*radians_per_sample2 + startphase2))
			+ noise(rng);
	}

	return ret;
}

WaveformBase* DemoOscilloscope::GeneratePRBS31(
	float amplitude,
	float period,
	int64_t sampleperiod,
	size_t depth)
{
	auto ret = new AnalogWaveform;
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	//Generate the PRBS as a square wave. Interpolate zero crossings as needed.
	uint32_t prbs = rand();
	float scale = amplitude / 2;
	float phase_to_next_edge = period;
	bool value = false;
	for(size_t i=0; i<depth; i++)
	{
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

		//Increment phase accumulator
		float last_phase = phase_to_next_edge;
		phase_to_next_edge -= sampleperiod;

		bool last = value;
		if(phase_to_next_edge < 0)
		{
			uint32_t next = ( (prbs >> 31) ^ (prbs >> 28) ) & 1;
			prbs = (prbs << 1) | next;
			value = next;

			phase_to_next_edge += period;
		}

		//Not an edge, just repeat the value
		if(last == value)
			ret->m_samples[i] = value ? scale : -scale;

		//Edge - interpolate
		else
		{
			float last_voltage = last ? scale : -scale;
			float cur_voltage = value ? scale : -scale;

			float frac = 1 - (last_phase / sampleperiod);
			float delta = cur_voltage - last_voltage;

			ret->m_samples[i] = last_voltage + delta*frac;
		}
	}

	DegradeSerialData(ret, sampleperiod, depth);

	return ret;
}

WaveformBase* DemoOscilloscope::Generate8b10b(
	float amplitude,
	float period,
	int64_t sampleperiod,
	size_t depth)
{
	auto ret = new AnalogWaveform;
	ret->m_timescale = sampleperiod;
	ret->Resize(depth);

	const int patternlen = 20;
	const bool pattern[patternlen] =
	{
		0, 0, 1, 1, 1, 1, 1, 0, 1, 0,		//K28.5
		1, 0, 0, 1, 0, 0, 0, 1, 0, 1		//D16.2
	};

	//Generate the data stream as a square wave. Interpolate zero crossings as needed.
	float scale = amplitude / 2;
	float phase_to_next_edge = period;
	bool value = false;
	int nbit = 0;
	for(size_t i=0; i<depth; i++)
	{
		ret->m_offsets[i] = i;
		ret->m_durations[i] = 1;

		//Increment phase accumulator
		float last_phase = phase_to_next_edge;
		phase_to_next_edge -= sampleperiod;

		bool last = value;
		if(phase_to_next_edge < 0)
		{
			value = pattern[nbit ++];
			if(nbit >= patternlen)
				nbit = 0;

			phase_to_next_edge += period;
		}

		//Not an edge, just repeat the value
		if(last == value)
			ret->m_samples[i] = value ? scale : -scale;

		//Edge - interpolate
		else
		{
			float last_voltage = last ? scale : -scale;
			float cur_voltage = value ? scale : -scale;

			float frac = 1 - (last_phase / sampleperiod);
			float delta = cur_voltage - last_voltage;

			ret->m_samples[i] = last_voltage + delta*frac;
		}
	}

	DegradeSerialData(ret, sampleperiod, depth);

	return ret;
}

/**
	@brief Takes an idealized serial data stream and turns it into something less pretty

	by adding noise and a band-limiting filter
 */
void DemoOscilloscope::DegradeSerialData(AnalogWaveform* cap, int64_t sampleperiod, size_t depth)
{
	//RNGs
	random_device rd;
	mt19937 rng(rd());
	normal_distribution<> noise(0, 0.01);

	//Prepare for second pass: reallocate FFT buffer if sample depth changed
	const size_t npoints = pow(2, ceil(log2(depth)));
	size_t nouts = npoints/2 + 1;
	if(m_cachedNumPoints != npoints)
	{
		if(m_forwardPlan)
			ffts_free(m_forwardPlan);
		m_forwardPlan = ffts_init_1d_real(npoints, FFTS_FORWARD);

		if(m_reversePlan)
			ffts_free(m_reversePlan);
		m_reversePlan = ffts_init_1d_real(npoints, FFTS_BACKWARD);

		m_forwardInBuf = m_allocator.allocate(npoints);
		m_forwardOutBuf = m_allocator.allocate(2*nouts);
		m_reverseOutBuf = m_allocator.allocate(npoints);

		m_cachedNumPoints = npoints;
	}

	//Copy the input, then fill any extra space with zeroes
	memcpy(m_forwardInBuf, &cap->m_samples[0], depth*sizeof(float));
	for(size_t i=depth; i<npoints; i++)
		m_forwardInBuf[i] = 0;

	//Do the forward FFT
	ffts_execute(m_forwardPlan, &m_forwardInBuf[0], &m_forwardOutBuf[0]);

	//Simple channel response model
	double sample_ghz = 1000.0 / sampleperiod;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);
	complex<float> pole(0, -FreqToPhase(5e9));
	float prescale = abs(pole);
	for(size_t i = 0; i<nouts; i++)
	{
		complex<float> s(0, FreqToPhase(bin_hz * i));
		complex<float> h = prescale * complex<float>(1, 0) / (s - pole);

		float binscale = abs(h);
		m_forwardOutBuf[i*2] *= binscale;		//real
		m_forwardOutBuf[i*2 + 1] *= binscale;	//imag
	}

	//Calculate the inverse FFT
	ffts_execute(m_reversePlan, &m_forwardOutBuf[0], &m_reverseOutBuf[0]);

	//Rescale the FFT output and copy to the output, then add noise
	float fftscale = 1.0f / npoints;
	for(size_t i=0; i<depth; i++)
		cap->m_samples[i] = m_reverseOutBuf[i] * fftscale + noise(rng);
}
