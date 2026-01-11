/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "PicoOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;

#define RATE_5GSPS		(INT64_C(5000) * INT64_C(1000) * INT64_C(1000))
#define RATE_2P5GSPS	(INT64_C(2500) * INT64_C(1000) * INT64_C(1000))
#define RATE_1P25GSPS	(INT64_C(1250) * INT64_C(1000) * INT64_C(1000))
#define RATE_1GSPS		(INT64_C(1000) * INT64_C(1000) * INT64_C(1000))
#define RATE_625MSPS	(INT64_C(625)  * INT64_C(1000) * INT64_C(1000))
#define RATE_500MSPS	(INT64_C(500)  * INT64_C(1000) * INT64_C(1000))
#define RATE_400MSPS	(INT64_C(400)  * INT64_C(1000) * INT64_C(1000))
#define RATE_250MSPS	(INT64_C(250)  * INT64_C(1000) * INT64_C(1000))
#define RATE_200MSPS	(INT64_C(200)  * INT64_C(1000) * INT64_C(1000))
#define RATE_125MSPS	(INT64_C(125)  * INT64_C(1000) * INT64_C(1000))
#define RATE_100MSPS	(INT64_C(100)  * INT64_C(1000) * INT64_C(1000))
#define RATE_80MSPS		(INT64_C(80)   * INT64_C(1000) * INT64_C(1000))
#define RATE_62P5MSPS	(INT64_C(625)  * INT64_C(1000) * INT64_C(100))
#define RATE_50MSPS		(INT64_C(50)   * INT64_C(1000) * INT64_C(1000))
#define RATE_40MSPS		(INT64_C(40)   * INT64_C(1000) * INT64_C(1000))

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

PicoOscilloscope::PicoOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, RemoteBridgeOscilloscope(transport)
	, m_lastSeq(0)
	, m_dropUntilSeq(0)
	, m_nextWaveformWriteBuffer(0)
{
	//Set up initial cache configuration as "not valid" and let it populate as we go

	IdentifyHardware();

	//Set resolution
	SetADCMode(0, 0);

	//Add analog channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = "A";
		chname[0] += i;

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			GetChannelColor(i),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_ANALOG,
			i);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 1;
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_DC_1M);
		SetChannelOffset(i, 0,  0);
		SetChannelVoltageRange(i, 0, 5);
	}

	//Add digital channels (named 1D0...7 and 2D0...7 for Pods, D0...15 for MSO models)
	m_digitalChannelBase = m_analogChannelCount;
	switch(m_picoSeries)
	{
		case 2:
		case 3:
		case 5:
		{
			for(size_t i=0; i<m_digitalChannelCount; i++)
			{
				//Hardware name of the channel
				size_t ibank = i / 8;
				size_t ichan = i % 8;
				string chname = "1D0";
				chname[0] += ibank;
				chname[2] += ichan;

				//Create the channel
				size_t chnum = i + m_digitalChannelBase;
				auto chan = new OscilloscopeChannel(
					this,
					chname,
					GetChannelColor(ichan),
					Unit(Unit::UNIT_FS),
					Unit(Unit::UNIT_COUNTS),
					Stream::STREAM_TYPE_DIGITAL,
					chnum);
				m_channels.push_back(chan);
				chan->SetDefaultDisplayName();
				//Change the display name to D0...D15
				chname = "D" + to_string(i);
				chan->SetDisplayName(chname);
				//Hysteresis is fixed to 250mV for most MSO models
				SetDigitalHysteresis(chnum, 0.25);
				if( m_model=="2206" || m_model=="2207" || m_model=="2208" )
					SetDigitalHysteresis(chnum, 0.2);
				if( m_model=="2205MSO" || m_model=="3204MSO" || m_model=="3205MSO" || m_model=="3206MSO" )
					SetDigitalHysteresis(chnum, 0.1);
				SetDigitalThreshold(chnum, 0);
			}
		}
		break;

		case 6:
		{
			for(size_t i=0; i<m_digitalChannelCount; i++)
			{
				//Hardware name of the channel
				size_t ibank = i / 8;
				size_t ichan = i % 8;
				string chname = "1D0";
				chname[0] += ibank;
				chname[2] += ichan;

				//Create the channel
				size_t chnum = i + m_digitalChannelBase;
				auto chan = new OscilloscopeChannel(
					this,
					chname,
					GetChannelColor(ichan),
					Unit(Unit::UNIT_FS),
					Unit(Unit::UNIT_COUNTS),
					Stream::STREAM_TYPE_DIGITAL,
					chnum);
				m_channels.push_back(chan);
				chan->SetDefaultDisplayName();

				SetDigitalHysteresis(chnum, 0.1);
				SetDigitalThreshold(chnum, 0);
			}
		}
	}

	//Set initial memory configuration.
	switch(m_picoSeries)
	{
		case 2:
		{
			if(m_model == "2205MSO")
			{
				//50 Msps is the highest rate the 2205MSO supports with all channels, including MSO, active.
				SetSampleRate(50000000L);
				SetSampleDepth(10000);
			}
			else
			{
				//125 Msps is the highest rate the 2000 series supports with all channels, including MSO, active.
				SetSampleRate(125000000L);
				SetSampleDepth(10000);
			}
			break;
		}
		case 3:
		{
			if(m_model[4] == 'E')
			{
				//625 Msps is the highest rate the 3000E series supports with all channels, including MSO, active.
				SetSampleRate(625000000L);
				SetSampleDepth(1000000);
			}
			else
			{
				//125 Msps is the highest rate the 3000 series supports with all channels, including MSO, active.
				SetSampleRate(125000000L);
				SetSampleDepth(100000);
			}
			break;
		}
		case 5:
		{
			//125 Msps is the highest rate the 5000 series supports with all channels, including MSO, active.
			SetSampleRate(125000000L);
			SetSampleDepth(100000);
			break;
		}

		case 4:
		{
			//40 Msps is the highest rate the 4000 series supports with all channels active.
			SetSampleRate(40000000L);
			SetSampleDepth(100000);
			break;
		}

		case 6:
		{
			//625 Msps is the highest rate the 6000 series supports with all channels, including MSO, active.
			SetSampleRate(625000000L);
			SetSampleDepth(1000000);
			break;
		}

		default:
			LogWarning("Unknown/unsupported Pico model\n");
			break;
	}

	//Set initial AWG configuration
	if(m_picoHasAwg)
	{
		//has function generator
		SetFunctionChannelAmplitude(0, 0.1);
		SetFunctionChannelShape(0, SHAPE_SQUARE);
		SetFunctionChannelDutyCycle(0, 0.5);
		SetFunctionChannelFrequency(0, 1e6);
		SetFunctionChannelOffset(0, 0);
		SetFunctionChannelOutputImpedance(0, IMPEDANCE_HIGH_Z);
		SetFunctionChannelActive(0, false);
		m_awgChannel = new FunctionGeneratorChannel(
			this,
			"AWG",
			"#808080",
			m_channels.size());
		m_channels.push_back(m_awgChannel);

		//Default to not showing in the filter graph to avoid clutter
		m_awgChannel->m_visibilityMode = InstrumentChannel::VIS_HIDE;
	}
	else
		m_awgChannel = nullptr;

	//Add the external trigger input
	if(m_picoHasExttrig)
	{
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
	}

	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(GetOscilloscopeChannel(0)));
	SetTrigger(trig);
	PushTrigger();
	SetTriggerOffset(10 * 1000L * 1000L);

	//Initialize waveform buffers
	//(allocate an extra so we can have conversion running in the background as we download data)
	for(size_t i=0; i<m_analogChannelCount + 1; i++)
	{
		m_analogRawWaveformBuffers.push_back(std::make_unique<AcceleratorBuffer<int16_t> >());
		m_analogRawWaveformBuffers[i]->SetCpuAccessHint(AcceleratorBuffer<int16_t>::HINT_LIKELY);
		m_analogRawWaveformBuffers[i]->SetGpuAccessHint(AcceleratorBuffer<int16_t>::HINT_LIKELY);
	}

	//Create Vulkan objects for the waveform conversion
	m_queue = g_vkQueueManager->GetComputeQueue("PicoOscilloscope.queue");
	vk::CommandPoolCreateInfo poolInfo(
		vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		m_queue->m_family );

	m_pool = make_unique<vk::raii::CommandPool>(*g_vkComputeDevice, poolInfo);

	vk::CommandBufferAllocateInfo bufinfo(**m_pool, vk::CommandBufferLevel::ePrimary, 1);
	m_cmdBuf = make_unique<vk::raii::CommandBuffer>(
		std::move(vk::raii::CommandBuffers(*g_vkComputeDevice, bufinfo).front()));

	if(g_hasDebugUtils)
	{
		string poolname = "PicoOscilloscope.pool";
		string bufname = "PicoOscilloscope.cmdbuf";

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eCommandPool,
				reinterpret_cast<uint64_t>(static_cast<VkCommandPool>(**m_pool)),
				poolname.c_str()));

		g_vkComputeDevice->setDebugUtilsObjectNameEXT(
			vk::DebugUtilsObjectNameInfoEXT(
				vk::ObjectType::eCommandBuffer,
				reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(**m_cmdBuf)),
				bufname.c_str()));
	}

	m_conversionPipeline = make_unique<ComputePipeline>(
			"shaders/Convert16BitSamples.spv", 2, sizeof(ConvertRawSamplesShaderArgs) );
}

/**
	@brief Color the channels based on Pico's standard color sequence (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string PicoOscilloscope::GetChannelColor(size_t i)
{
	switch(i % 8)
	{
		case 0:
			return "#4040ff";

		case 1:
			return "#ff4040";

		case 2:
			return "#208020";

		case 3:
			return "#ffff00";

		case 4:
			return "#600080";

		case 5:
			return "#808080";

		case 6:
			return "#40a0a0";

		case 7:
		default:
			return "#e040e0";
	}
}

void PicoOscilloscope::IdentifyHardware()
{
	//Assume no MSO channels to start
	m_digitalChannelCount = 0;

	//Figure out device family
	m_picoSeries = m_model[0] - '0';
	m_picoHasAwg = false;
	m_picoHasExttrig = false;
	m_picoHasBwlimiter = false;
	m_picoHas50ohm = false;
	m_BandwidthLimits = {0};

	switch(m_picoSeries)
	{
		case 2:
		{
			m_picoHasAwg = true;
			m_picoHasBwlimiter = false;
			m_awgBufferSize = 8192;
			if(m_model[4] == 'B')
				m_awgBufferSize = 32768;

			if(m_model.find("MSO") != string::npos)
				m_digitalChannelCount = 16;
			if( m_model=="2206" || m_model=="2207" || m_model=="2208" )
				m_picoHasExttrig = true;

			m_adcModes = {8};
			break;
		}

		case 3:
		{
			if(m_model[4] != 'A')
				m_picoHasAwg = true;

			if( (m_model[4] == 'D') || (m_model.find("34") != string::npos) )
			{
				m_picoHasBwlimiter = true;
				m_BandwidthLimits.push_back(20);
				m_awgBufferSize = 32768;
			}

			if( (m_model[4] == 'A') || (m_model[4] == 'B') )
			{
				switch(m_model[3])
				{
					case '4':
					case '5':
						m_awgBufferSize = 8192;
						break;
					case '6':
						m_awgBufferSize = 16384;
						break;
					case '7':
						m_awgBufferSize = 32768;
						break;
				}
			}

			if(m_model.find("MSO") != string::npos)
			{
				m_digitalChannelCount = 16;
				m_picoHasExttrig = false;
			}
			else
				m_picoHasExttrig = true;

			m_adcModes = {8};
			if(m_model[4] == 'E')
			{
				m_picoHas50ohm = true;
				m_picoHasExttrig = true;
				m_adcModes.push_back(10);
				m_BandwidthLimits.push_back(50);
				m_BandwidthLimits.push_back(100);
				if( (m_model[3] - '0') >= 6)
					m_BandwidthLimits.push_back(200);
				if( (m_model[3] - '0') >= 7)
					m_BandwidthLimits.push_back(350);
				if( (m_model[3] - '0') == 8)
					m_BandwidthLimits.push_back(500);
			}

			break;
		}

		case 4:
		{
			m_picoHasAwg = true;
			m_awgBufferSize = 16384;
			m_picoHasBwlimiter = false;

			if(m_model.find("4444") != string::npos)
			{
				m_picoHasAwg = false;
				m_picoHasExttrig = false;
				m_picoHasBwlimiter = true;
				m_BandwidthLimits.push_back(1);
				m_BandwidthLimits.push_back(100);	//workaround: use 100MHz for 100kHz filter (applicable to 4444 (20MHz bandwidth))
				m_awgBufferSize = 0;
				m_adcModes = {12, 14};
			}
			else
				m_adcModes = {12};
			break;
		}

		case 5:
		{
			m_picoHasBwlimiter = true;
			m_BandwidthLimits.push_back(20);
			if(m_model[4] == 'A')
			{
				m_awgBufferSize = 0;
			}
			else if(m_model[4] == 'B')
			{
				m_picoHasAwg = true;
				switch(m_model[3])
				{
					case '2':
						m_awgBufferSize = 16384;
						break;
					case '3':
						m_awgBufferSize = 32768;
						break;
					case '4':
						m_awgBufferSize = 49152;
						break;
				}
			}
			else if(m_model[4] == 'D')
			{
				m_picoHasAwg = true;
				m_awgBufferSize = 32768;
			}

			if(m_model.find("MSO") != string::npos)
			{
				m_digitalChannelCount = 16;
				m_picoHasExttrig = false;
			}
			else
			{
				m_picoHasExttrig = true;
			}
			m_adcModes = {8, 12, 14, 15, 16};
			break;
		}

		case 6:
		{
			m_digitalChannelCount = 16;
			m_picoHas50ohm = true;
			m_picoHasExttrig = true;
			m_picoHasBwlimiter = true;
			m_picoHasAwg = true;
			m_awgBufferSize = 40960;
			if(m_model.find("6428") != string::npos)
				m_picoHasBwlimiter = false;
			if(m_model[3] == '5' or m_model[3] == '6')
				m_BandwidthLimits.push_back(20);
			else
			{
				m_BandwidthLimits.push_back(20);
				m_BandwidthLimits.push_back(200);
			}
			if(m_model[2] == '2')
				m_adcModes = {8, 10, 12};
			else
				m_adcModes = {8};
			break;
		}

		default:
		{
			LogWarning("Unknown PicoScope model \"%s\"\n", m_model.c_str());
			m_picoSeries = 0;
			break;
		}
	}
	//Ask the scope how many channels it has available or enabled
	m_analogChannelCount = stoi(m_transport->SendCommandQueuedWithReply("CHANS?"));

	//LogDebug("PicoScope model \"%s\", series: %i, digital channels: %zu, BW-Limiter: %s, AWG: %s/%u, Ext.Trig: %s, 50 ohm: %s\n",
	//	m_model.c_str(), m_picoSeries, m_digitalChannelCount, m_picoHasBwlimiter ? "Y" : "N", m_picoHasAwg ? "Y" : "N", m_awgBufferSize, m_picoHasExttrig ? "Y" : "N", m_picoHas50ohm ? "Y" : "N");
}

PicoOscilloscope::~PicoOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int PicoOscilloscope::GetInstrumentTypes() const
{

	if(m_picoHasAwg)
		return Instrument::INST_OSCILLOSCOPE | Instrument::INST_FUNCTION;
	else
		return Instrument::INST_OSCILLOSCOPE;
}

uint32_t PicoOscilloscope::GetInstrumentTypesForChannel(size_t i) const
{
	if(m_awgChannel && (m_awgChannel->GetIndex() == i))
		return Instrument::INST_FUNCTION;
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string PicoOscilloscope::GetDriverNameInternal()
{
	return "pico";
}

void PicoOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//clear probe presence flags as those can change without our knowledge
	m_digitalBankPresent.clear();
}

bool PicoOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(m_picoHasExttrig)
	{
		//this will crash if m_extTrigChannel was not created, hence the prior check for m_picoHasExttrig
		if(i == m_extTrigChannel->GetIndex())
			return false;
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelsEnabled[i];
}

void PicoOscilloscope::EnableChannel(size_t i)
{
	//If the pod is already active we don't have to touch anything scope side.
	//Update the cache and we're done.
	if(IsChannelIndexDigital(i))
	{
		size_t npod = GetDigitalPodIndex(i);
		if(IsDigitalPodActive(npod))
		{
			lock_guard<recursive_mutex> lock(m_cacheMutex);
			m_channelsEnabled[i] = true;
			return;
		}
	}

	RemoteBridgeOscilloscope::EnableChannel(i);

	//Memory configuration might have changed. Update availabe sample rates and memory depths.
	GetSampleRatesNonInterleaved();
	GetSampleDepthsNonInterleaved();
}

void PicoOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
	}

	//If the pod still has active channels after turning this one off, we don't have to touch anything scope side.
	if(IsChannelIndexDigital(i))
	{
		size_t npod = GetDigitalPodIndex(i);
		if(IsDigitalPodActive(npod))
			return;
	}

	RemoteBridgeOscilloscope::DisableChannel(i);

	//Memory configuration might have changed. Update availabe sample rates and memory depths.
	GetSampleRatesNonInterleaved();
	GetSampleDepthsNonInterleaved();
}

vector<OscilloscopeChannel::CouplingType> PicoOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	//All models with an 'E' have 50 ohm
	if(m_model[4] == 'E')
	{
		if(m_model.find("6428") == string::npos) //6428 has ONLY 50 ohm and NO 1Meg
		{
			ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
			ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
		}
		ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
		//ret.push_back(OscilloscopeChannel::COUPLE_GND);
	}
	else
	{
		ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
		ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
		//ret.push_back(OscilloscopeChannel::COUPLE_GND);
	}
	return ret;
}

double PicoOscilloscope::GetChannelAttenuation(size_t i)
{
	if(m_picoHasExttrig)
	{
		//this will crash if m_extTrigChannel was not created, hence the prior check for m_picoHasExttrig
		if(GetOscilloscopeChannel(i) == m_extTrigChannel)
			return 1;
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void PicoOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	double oldAtten = m_channelAttenuations[i];
	m_channelAttenuations[i] = atten;

	//Rescale channel voltage range and offset
	double delta = atten / oldAtten;
	m_channelVoltageRanges[i] *= delta;
	m_channelOffsets[i] *= delta;
}

vector<unsigned int> PicoOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	return m_BandwidthLimits;
}

unsigned int PicoOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	int ret = stoi(m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":BWLIM?"));
	return ret;
}

void PicoOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	m_transport->SendCommand(GetOscilloscopeChannel(i)->GetHwname() + ":BWLIM " + to_string(limit_mhz));
}

OscilloscopeChannel* PicoOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

void PicoOscilloscope::Stop()
{
	RemoteBridgeOscilloscope::Stop();

	//Wait for any previous in-progress waveforms to finish processing
	{
		lock_guard<recursive_mutex> wipLock(m_wipWaveformMutex);
		while(!m_wipWaveforms.empty())
			PushPendingWaveformsIfReady();
	}

	//Ask the server what the last waveform it sent was
	m_dropUntilSeq = stoul(Trim(m_transport->SendCommandQueuedWithReply("SEQNUM?")));
	LogTrace("Trigger stopped after processing waveform %u. Last sequence number sent by scope was %u. Need to drop %u stale waveforms already in flight\n",
		(unsigned int)m_lastSeq, (unsigned int)m_dropUntilSeq, (unsigned int)(m_dropUntilSeq - m_lastSeq));
}

void PicoOscilloscope::BackgroundProcessing()
{
	//Call the base class to flush the transport etc
	RemoteBridgeOscilloscope::BackgroundProcessing();

	//Push any previously acquired waveforms to the RX buffer if we have them
	lock_guard<recursive_mutex> wipLock(m_wipWaveformMutex);
	PushPendingWaveformsIfReady();
}

/**
	@brief Wait for waveform conversion to finish, then push it to the pending waveforms buffer
 */
void PicoOscilloscope::PushPendingWaveformsIfReady()
{
	if(m_wipWaveforms.empty())
		return;

	//Wait up to 1ms for GPU side conversion to finish and return if it's not done
	if(!m_queue->WaitIdleWithTimeout(1000 * 1000))
		return;

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(m_wipWaveforms);

	//If we got backed up, drop the extra waveforms
	while (m_pendingWaveforms.size() > 2)
	{
		LogTrace("Dropping waveform due to excessive pend queue depth\n");

		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			AddWaveformToAnalogPool(it.second);
		m_pendingWaveforms.pop_front();
	}

	m_pendingWaveformsMutex.unlock();
	m_wipWaveforms.clear();
}

Oscilloscope::TriggerMode PicoOscilloscope::PollTrigger()
{
	//Is the trigger armed? If not, report stopped
	if(!IsTriggerArmed())
		return TRIGGER_MODE_STOP;

	//See if we have data ready
	if(dynamic_cast<SCPITwinLanTransport*>(m_transport)->GetSecondarySocket().GetRxBytesAvailable() > 0)
	{
		#ifdef HAVE_NVTX
			nvtxMark("PollTrigger");
		#endif

		//Do we have old stale waveforms to drop still in the socket buffer? Throw it out
		if(m_dropUntilSeq > m_lastSeq)
		{
			LogTrace("Dropping until sequence %u, last received sequence was %u. Need to drop this waveform\n",
				(unsigned int)m_dropUntilSeq, (unsigned int)m_lastSeq);
			DoAcquireData(false);
			return TRIGGER_MODE_RUN;
		}

		//No, this is a fresh waveform - prepare to download it
		return TRIGGER_MODE_TRIGGERED;
	}

	else
		return TRIGGER_MODE_RUN;
}

bool PicoOscilloscope::AcquireData()
{
	return DoAcquireData(true);
}

bool PicoOscilloscope::DoAcquireData(bool keep)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range range("PicoOscilloscope::DoAcquireData");
	#endif

	#pragma pack(push, 1)
	struct
	{
		//Sequence nu,ber
		uint32_t sequence;

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

	//Acknowledge receipt of this waveform
	m_lastSeq = wfmhdrs.sequence;
	m_transport->SendRawData(4, (uint8_t*)&m_lastSeq);

	//Acquire data for each channel
	size_t chnum;
	size_t memdepth;
	float config[3];
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Analog channels get processed separately
	vector<UniformAnalogWaveform*> awfms;
	lock_guard<recursive_mutex> wipLock(m_wipWaveformMutex);

	bool processedWaveformsOnGPU = false;
	for(size_t i=0; i<numChannels; i++)
	{
		size_t tmp[2];

		//Get channel ID and memory depth (samples, not bytes)
		if(!m_transport->ReadRawData(sizeof(tmp), (uint8_t*)&tmp))
			return false;
		chnum = tmp[0];
		memdepth = tmp[1];

		#ifdef HAVE_NVTX
			nvtx3::scoped_range range2(string("Channel ") + to_string(chnum));
		#endif

		//Analog channels
		if(chnum < m_analogChannelCount)
		{
			auto& abuf = m_analogRawWaveformBuffers[m_nextWaveformWriteBuffer];
			m_nextWaveformWriteBuffer = (m_nextWaveformWriteBuffer + 1) % m_analogRawWaveformBuffers.size();
			abuf->resize(memdepth);
			abuf->PrepareForCpuAccess();

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

			if(!keep)
				continue;

			//Create our waveform
			auto cap = AllocateAnalogWaveform(m_nickname + "." + GetOscilloscopeChannel(i)->GetHwname());
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = trigphase;
			cap->m_startTimestamp = time(NULL);
			cap->m_startFemtoseconds = fs;
			cap->Resize(memdepth);

			//Clear out any previously pending waveforms before we queue up this one
			if(i == 0)
				PushPendingWaveformsIfReady();

			m_wipWaveforms[GetOscilloscopeChannel(chnum)] = cap;

			if(g_hasShaderInt16)
			{
				m_queue->WaitIdle();
				m_cmdBuf->begin({});

				m_conversionPipeline->BindBufferNonblocking(0, cap->m_samples, *m_cmdBuf, true);
				m_conversionPipeline->BindBufferNonblocking(1, *abuf, *m_cmdBuf);

				ConvertRawSamplesShaderArgs args;
				args.size = cap->size();
				args.gain = scale;
				args.offset = -offset;

				const uint32_t compute_block_count = GetComputeBlockCount(cap->size(), 64);
				m_conversionPipeline->Dispatch(
					*m_cmdBuf, args,
					min(compute_block_count, 32768u),
					compute_block_count / 32768 + 1);

				cap->MarkModifiedFromGpu();

				m_cmdBuf->end();
				m_queue->Submit(*m_cmdBuf);

				processedWaveformsOnGPU = true;
			}
			else
			{
				cap->PrepareForCpuAccess();
				Convert16BitSamples(
					cap->m_samples.GetCpuPointer(),
					abuf->GetCpuPointer(),
					scale,
					-offset,
					cap->size());

				cap->MarkSamplesModifiedFromCpu();
			}
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

			if(!keep)
				continue;

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
				m_wipWaveforms[GetOscilloscopeChannel(nchan) ] = caps[j];
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

	if(!keep)
		return true;

	//If we did CPU side conversion, push the waveforms to our queue now
	if(!processedWaveformsOnGPU)
		PushPendingWaveformsIfReady();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

bool PicoOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

bool PicoOscilloscope::CanInterleave()
{
	return false;
}

vector<uint64_t> PicoOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	string rates = m_transport->SendCommandQueuedWithReply("RATES?");

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = rates.find(',', i+1);
		if(i == string::npos)
			break;

		auto block = rates.substr(istart, i-istart);
		uint64_t fs = stoull(block);
		auto hz = FS_PER_SECOND / fs;
		ret.push_back(hz);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> PicoOscilloscope::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> PicoOscilloscope::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> PicoOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

	string depths = m_transport->SendCommandQueuedWithReply("DEPTHS?");

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = depths.find(',', i+1);
		if(i == string::npos)
			break;

		uint64_t sampleDepth = stoull(depths.substr(istart, i-istart));
		ret.push_back(sampleDepth);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> PicoOscilloscope::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

uint64_t PicoOscilloscope::GetSampleRate()
{
	return m_srate;
}

uint64_t PicoOscilloscope::GetSampleDepth()
{
	return m_mdepth;
}

void PicoOscilloscope::SetSampleDepth(uint64_t depth)
{
	m_transport->SendCommand(string("DEPTH ") + to_string(depth));
	m_mdepth = depth;
}

void PicoOscilloscope::SetSampleRate(uint64_t rate)
{
	m_srate = rate;
	m_transport->SendCommand( string("RATE ") + to_string(rate));
}

void PicoOscilloscope::SetTriggerOffset(int64_t offset)
{
	//Don't allow setting trigger offset beyond the end of the capture
	int64_t captureDuration = GetSampleDepth() * FS_PER_SECOND / GetSampleRate();
	m_triggerOffset = min(offset, captureDuration);

	PushTrigger();
}

int64_t PicoOscilloscope::GetTriggerOffset()
{
	return m_triggerOffset;
}

bool PicoOscilloscope::IsInterleaving()
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

bool PicoOscilloscope::SetInterleaving(bool /*combine*/)
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

void PicoOscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");

	ClearPendingWaveforms();
}

vector<Oscilloscope::AnalogBank> PicoOscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> banks;
	banks.push_back(GetAnalogBank(0));
	return banks;
}

Oscilloscope::AnalogBank PicoOscilloscope::GetAnalogBank(size_t /*channel*/)
{
	AnalogBank bank;
	return bank;
}

bool PicoOscilloscope::IsADCModeConfigurable()
{
	switch(m_picoSeries)
	{
		case 2:
			return false;
			break;

		case 3:
			if(m_model[2] == '1')
				return true;
			else
				return false;
			break;

		case 4:
			if(m_model.find("4444") == string::npos)
				return true;
			else
				return false;
			break;

		case 5:
			return true;
			break;

		case 6:
			if(m_model[2] == '2')
				return true;
			else
				return false;
			break;

		default:
			LogWarning("PicoOscilloscope::IsADCModeConfigurable: unknown series\n");
			return false;
	}
}

vector<string> PicoOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> ret;

	switch(m_picoSeries)
	{
		case 2:
			ret.push_back("8 Bit");
			break;

		case 3:
			ret.push_back("8 Bit");
			if(Is10BitModeAvailable())
				ret.push_back("10 Bit");
			break;

		case 4:
			ret.push_back("12 Bit");
			if(m_model.find("4444") != string::npos)
				ret.push_back("14 Bit");
			break;

		case 5:
			ret.push_back("8 Bit");
			if(Is12BitModeAvailable())
			{
				ret.push_back("12 Bit");
				if(Is14BitModeAvailable())
				{
					ret.push_back("14 Bit");
					if(Is15BitModeAvailable())
					{
						ret.push_back("15 Bit");
						if(Is16BitModeAvailable())
							ret.push_back("16 Bit");
					}
				}
			}
			break;

		case 6:
			ret.push_back("8 Bit");
			if(Is10BitModeAvailable())
			{
				ret.push_back("10 Bit");
				if(Is12BitModeAvailable())
					ret.push_back("12 Bit");
			}
			break;

		default:
			break;
	}

	return ret;
}

size_t PicoOscilloscope::GetADCMode(size_t /*channel*/)
{
	size_t n = 0;
	for (int i : m_adcModes)
	{
 		if(m_adcBits == i)
			break;
		n++;
	}
	return n;
}

void PicoOscilloscope::SetADCMode(size_t /*channel*/, size_t mode)
{
	m_adcBits = m_adcModes[mode];
	m_transport->SendCommand("BITS " + to_string(m_adcBits));


	//Memory configuration might have changed. Update availabe sample rates and memory depths.
	GetSampleRatesNonInterleaved();
	GetSampleDepthsNonInterleaved();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> PicoOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;
	for(size_t i=0; i<m_digitalChannelCount; i++)
	{
		DigitalBank bank;
		bank.push_back(GetOscilloscopeChannel(m_digitalChannelBase + i));
		banks.push_back(bank);
	}
	return banks;
}

Oscilloscope::DigitalBank PicoOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank ret;
	ret.push_back(GetOscilloscopeChannel(channel));
	return ret;
}

bool PicoOscilloscope::IsDigitalHysteresisConfigurable()
{
	if(m_picoSeries == 6)
		return true;
	else
		return false;
}

bool PicoOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float PicoOscilloscope::GetDigitalHysteresis(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_digitalHysteresis[channel];
}

float PicoOscilloscope::GetDigitalThreshold(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_digitalThresholds[channel];
}

void PicoOscilloscope::SetDigitalHysteresis(size_t channel, float level)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_digitalHysteresis[channel] = level;
	}

	m_transport->SendCommand(GetOscilloscopeChannel(channel)->GetHwname() + ":HYS " + to_string(level * 1000));
}

void PicoOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	switch(m_picoSeries)
	{
		case 2:
		case 3:
		case 5:
		{
			//MSO scopes: sync threshold for the whole channel w/8 lanes
			size_t chnum = channel - m_digitalChannelBase;
			int n = 8;
			if(chnum<8)
				n = 0;
			for(size_t i=0; i<8; i++)
			{
				//Set the threshold for every lane of the channel
				chnum = i + n + m_digitalChannelBase;
				{
					lock_guard<recursive_mutex> lock(m_cacheMutex);
					m_digitalThresholds[chnum] = level;
				}
				//Only actually set the threshold on the first hardware channel though
				if(i==0)
				{
					m_transport->SendCommand(GetOscilloscopeChannel(chnum)->GetHwname() + ":THRESH " + to_string(level));
				}
			}
			break;
		}

		default:
		{
			{
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				m_digitalThresholds[channel] = level;
			}
			m_transport->SendCommand(GetOscilloscopeChannel(channel)->GetHwname() + ":THRESH " + to_string(level));
			break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checking for validity of configurations

/**
	@brief Returns the total number of analog channels which are currently enabled
 */
size_t PicoOscilloscope::GetEnabledAnalogChannelCount()
{
	size_t ret = 0;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(IsChannelEnabled(i))
			ret ++;
	}
	return ret;
}

/**
	@brief Returns the total number of 8-bit MSO pods which are currently enabled
 */
size_t PicoOscilloscope::GetEnabledDigitalPodCount()
{
	size_t n = 0;
	if(IsDigitalPodActive(0))
		n++;
	if(IsDigitalPodActive(1))
		n++;
	return n;
}

/**
	@brief Returns the total number of analog channels in the requested range which are currently enabled
 */
size_t PicoOscilloscope::GetEnabledAnalogChannelCountRange(size_t start, size_t end)
{
	if(end >= m_analogChannelCount)
		end = m_analogChannelCount - 1;

	size_t n = 0;
	for(size_t i = start; i <= end; i++)
	{
		if(IsChannelEnabled(i))
			n ++;
	}
	return n;
}

/**
	@brief Check if a MSO pod is present
 */
bool PicoOscilloscope::IsDigitalPodPresent(size_t npod)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_digitalBankPresent.find(npod) != m_digitalBankPresent.end())
			return m_digitalBankPresent[npod];
	}

	int present = stoi(m_transport->SendCommandQueuedWithReply(to_string(npod + 1) + "D:PRESENT?"));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(present)
	{
		m_digitalBankPresent[npod] = true;
		return true;
	}
	else
	{
		m_digitalBankPresent[npod] = false;
		return false;
	}
}

/**
	@brief Check if any channels in an MSO pod are enabled
 */
bool PicoOscilloscope::IsDigitalPodActive(size_t npod)
{
	size_t base = m_digitalChannelBase + 8*npod;
	for(size_t i=0; i<8; i++)
	{
		if(IsChannelEnabled(base+i))
			return true;
	}
	return false;
}

/**
	@brief Checks if a channel index refers to a MSO channel
 */
bool PicoOscilloscope::IsChannelIndexDigital(size_t i)
{
	return (i >= m_digitalChannelBase) && (i < m_digitalChannelBase + m_digitalChannelCount);
}

bool PicoOscilloscope::CanEnableChannel(size_t i)
{
	//If channel is already on, of course it can stay on
	if(IsChannelEnabled(i))
		return true;

	//Digital channels
	if(IsChannelIndexDigital(i))
	{
		size_t npod = GetDigitalPodIndex(i);

		//If the pod isn't here, we can't enable it
		if(!IsDigitalPodPresent(npod))
			return false;

		//If other channels in the pod are already active, we can enable them
		if(IsDigitalPodActive(npod))
			return true;
	}

	//Fall back to the main path if we get here
	switch(m_picoSeries)
	{
		case 2:
			return CanEnableChannel2000Series8Bit(i);
			break;

		case 3:
			switch(m_adcBits)
			{
				case 8:
					return CanEnableChannel3000Series8Bit(i);
				case 10:
					return CanEnableChannel3000Series10Bit(i);
				default:
					return false;
			}
			break;

		case 4:
			switch(m_adcBits)
			{
				case 12:
					return CanEnableChannel4000Series12Bit(i);
				case 14:
					return CanEnableChannel4000Series14Bit(i);
				default:
					return false;
			}
			break;

		case 5:
			switch(m_adcBits)
			{
				case 8:
					return CanEnableChannel5000Series8Bit(i);
				case 12:
					return CanEnableChannel5000Series12Bit(i);
				case 14:
					return CanEnableChannel5000Series14Bit(i);
				case 15:
					return CanEnableChannel5000Series15Bit(i);
				case 16:
					return CanEnableChannel5000Series16Bit(i);
				default:
					return false;
			}
			break;

		case 6:
			switch(m_adcBits)
			{
				case 8:
					return CanEnableChannel6000Series8Bit(i);
				case 10:
					return CanEnableChannel6000Series10Bit(i);
				case 12:
					return CanEnableChannel6000Series12Bit(i);
				default:
					return false;
			}
			break;

		default:
			return false;
	}

	//When in doubt, assume all channels are available
	LogWarning("PicoOscilloscope::CanEnableChannel: Unknown ADC mode\n");
	return true;
}

/**
	@brief Checks if we can enable a channel on a 6000 series scope configured for 8-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel6000Series8Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//5 Gsps is the most restrictive configuration.
	if(rate >= RATE_5GSPS)
	{
		//If we already have too many channels/MSO pods active, we're out of RAM bandwidth.
		if(EnabledChannelCount >= 2)
			return false;

		//6403E only allows *one* 5 Gsps channel
		else if(m_model.find("6403") != string::npos)
			return (EnabledChannelCount == 0);

		//No banking restrictions for MSO pods if we have enough memory bandwidth
		else if(IsChannelIndexDigital(i))
			return true;

		//On 8 channel scopes, we can use one channel from the left bank (ABCD) and one from the right (EFGH).
		else if(m_analogChannelCount == 8)
		{
			//Can enable a left bank channel if there's none in use
			if(i < 4)
				return (GetEnabledAnalogChannelCountAToD() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountEToH() == 0);
		}

		//On 4 channel scopes, we can use one channel from the left bank (AB) and one from the right (CD)
		else
		{
			//Can enable a left bank channel if there's none in use
			if(i < 2)
				return (GetEnabledAnalogChannelCountAToB() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountCToD() == 0);
		}
	}

	//2.5 Gsps allows more stuff
	else if(rate >= RATE_2P5GSPS)
	{
		//If we already have too many channels/MSO pods active, we're out of RAM bandwidth.
		if(EnabledChannelCount >= 4)
			return false;

		//No banking restrictions for MSO pods if we have enough memory bandwidth
		else if(IsChannelIndexDigital(i))
			return true;

		//6403E allows up to 2 channels, one AB and one CD
		else if(m_model.find("6403") != string::npos)
		{
			//Can enable a left bank channel if there's none in use
			if(i < 2)
				return (GetEnabledAnalogChannelCountAToB() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountCToD() == 0);
		}

		//8 channel scopes allow up to 4 channels but only one from A/B, C/D, E/F, G/H
		else if(m_analogChannelCount == 8)
		{
			if(i < 2)
				return (GetEnabledAnalogChannelCountAToB() == 0);
			else if(i < 4)
				return (GetEnabledAnalogChannelCountCToD() == 0);
			else if(i < 6)
				return (GetEnabledAnalogChannelCountEToF() == 0);
			else
				return (GetEnabledAnalogChannelCountGToH() == 0);
		}

		//On 4 channel scopes, we can run everything at 2.5 Gsps
		else
			return true;
	}

	//1.25 Gsps - just RAM bandwidth check
	else if( (rate >= RATE_1P25GSPS) && (EnabledChannelCount <= 7) )
		return true;

	//Slow enough that there's no capacity limits
	else
		return true;
}

/**
	@brief Checks if we can enable a channel on a 6000 series scope configured for 10-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel6000Series10Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//5 Gsps is only allowed on a single channel/pod
	if(rate >= RATE_5GSPS)
		return (EnabledChannelCount == 0);

	//2.5 Gsps is allowed up to two channels/pods
	else if(rate >= RATE_2P5GSPS)
	{
		//Out of bandwidth
		if(EnabledChannelCount >= 2)
			return false;

		//No banking restrictions on MSO pods
		else if(IsChannelIndexDigital(i))
			return true;

		//8 channel scopes require the two channels to be in separate banks
		else if(m_analogChannelCount == 8)
		{
			//Can enable a left bank channel if there's none in use
			if(i < 4)
				return (GetEnabledAnalogChannelCountAToD() == 0);

			//Can enable a right bank channel if there's none in use
			else
				return (GetEnabledAnalogChannelCountEToH() == 0);
		}

		//No banking restrictions on 4 channel scopes
		else
			return true;
	}

	//1.25 Gsps is allowed up to 4 total channels/pods with no banking restrictions
	else if(rate >= RATE_1P25GSPS)
		return (EnabledChannelCount <= 3);

	//625 Msps allowed up to 8 total channels/pods with no banking restrictions
	else if(rate >= RATE_625MSPS)
		return (EnabledChannelCount <= 7);

	//Slow enough that there's no capacity limits
	else
		return true;
}

/**
	@brief Checks if we can enable a channel on a 6000 series scope configured for 12-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel6000Series12Bit(size_t i)
{
	int64_t rate = GetSampleRate();

	//Too many channels enabled?
	if(GetEnabledAnalogChannelCount() >= 2)
		return false;

	else if(rate > RATE_1P25GSPS)
		return false;

	//No banking restrictions on MSO pods
	else if(IsChannelIndexDigital(i))
		return true;

	else if(m_analogChannelCount == 8)
	{
		//Can enable a left bank channel if there's none in use
		if(i < 4)
			return (GetEnabledAnalogChannelCountAToD() == 0);

		//Can enable a right bank channel if there's none in use
		else
			return (GetEnabledAnalogChannelCountEToH() == 0);
	}

	else
	{
		//Can enable a left bank channel if there's none in use
		if(i < 2)
			return (GetEnabledAnalogChannelCountAToB() == 0);

		//Can enable a right bank channel if there's none in use
		else
			return (GetEnabledAnalogChannelCountCToD() == 0);
	}
}

/**
	@brief Checks if we can enable a channel on a 5000 series scope configured for 8-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel5000Series8Bit(size_t /*i*/)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	if(rate > RATE_1GSPS)
		return false;

	//1 Gsps allows only one channel/pod
	else if(rate >= RATE_500MSPS)
		return (EnabledChannelCount == 0);

	//500 Msps is allowed up to 2 total channels/pods
	else if(rate >= RATE_250MSPS)
		return (EnabledChannelCount <= 1);

	//250 Msps is allowed up to 4 total channels/pods
	else if(rate >= RATE_125MSPS)
		return (EnabledChannelCount <= 3);

	//Slow enough that there's no capacity limits
	else
		return true;
}

/**
	@brief Checks if we can enable a channel on a 5000 series scope configured for 12-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel5000Series12Bit(size_t /*i*/)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//1 Gsps not allowed
	if(rate > RATE_500MSPS)
		return false;

	//500 Msps allows only one channel/pod
	else if(rate >= RATE_250MSPS)
		return (EnabledChannelCount == 0);

	//250 Msps is allowed up to 2 total channels/pods
	else if(rate >= RATE_125MSPS)
		return (EnabledChannelCount <= 1);

	//125 Msps is allowed up to 4 total channels/pods
	else if(rate >= RATE_62P5MSPS)
		return (EnabledChannelCount <= 3);

	//Slow enough that there's no capacity limits
	else
		return true;
}

/**
	@brief Checks if we can enable a channel on a 5000 series scope configured for 14-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel5000Series14Bit(size_t /*i*/)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	if(rate > RATE_125MSPS)
		return false;

	//125 Msps is allowed up to 4 total channels/pods
	else if(rate >= RATE_62P5MSPS)
		return (EnabledChannelCount <= 3);

	//Slow enough that there's no capacity limits
	else
		return true;
}

/**
	@brief Checks if we can enable a channel on a 5000 series scope configured for 15-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel5000Series15Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//15-bit allows up to 2 channels at 125 Msps plus one or two digital channels
	if(rate > RATE_125MSPS)
		return false;

	//No banking restrictions on MSO pods
	else if(IsChannelIndexDigital(i))
		return true;

	//Too many channels enabled?
	else if(GetEnabledAnalogChannelCount() >= 2)
		return false;

	//125 Msps is allowed up to 2 channels
	else
		return (EnabledChannelCount <= 1);
}

/**
	@brief Checks if we can enable a channel on a 5000 series scope configured for 16-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel5000Series16Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//16-bit allows just one channel at 62.5 Msps plus one or two digital channels
	if(rate > RATE_62P5MSPS)
		return false;

	//No banking restrictions on MSO pods
	else if(IsChannelIndexDigital(i))
		return true;

	//Too many channels enabled?
	else if(GetEnabledAnalogChannelCount() >= 1)
		return false;

	//125 Msps is allowed only one channel
	else
		return (EnabledChannelCount == 0);
}

/**
	@brief Checks if we can enable a channel on a 4000 series scope configured for 12-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel4000Series12Bit(size_t /*i*/)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	if(m_model.find("4444") != string::npos)
	{
		if(rate > RATE_400MSPS)
			return false;

		else if(rate >= RATE_200MSPS)
			return (EnabledChannelCount == 0);

		else if(rate >= RATE_100MSPS)
			return (EnabledChannelCount <= 1);

		else
			return (EnabledChannelCount <= 3);
	}
	else
	{
		if(rate > RATE_80MSPS)
			return false;

		//80 Msps is allowed up to 4 total channels
		else if(rate >= RATE_40MSPS)
			return (EnabledChannelCount <= 3);

		//40 Msps is allowed up to 8 total channels
		else
			return (EnabledChannelCount <= 7);
	}
}

/**
	@brief Checks if we can enable a channel on a 4000 series scope configured for 14-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel4000Series14Bit(size_t /*i*/)
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//Only 4444 can do 14 bit
	if(m_model.find("4444") == string::npos)
		return false;

	else if(rate > RATE_50MSPS)
		return false;

	//50 Msps is allowed up to 4 total channels
	else
		return (EnabledChannelCount <= 3);
}

/**
	@brief Checks if we can enable a channel on a 3000 series scope configured for 8-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel3000Series8Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledDigitalChannelCount = GetEnabledDigitalPodCount();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + EnabledDigitalChannelCount;

	if(m_model[4] == 'E')
	{
		if( (IsChannelIndexDigital(i)) || (EnabledDigitalChannelCount > 0) )
		{
			if(rate > RATE_1P25GSPS)
				return false;

			//1.25 Gsps is allowed up to 4 total channels/pods
			else if(rate >= RATE_625MSPS)
				return (EnabledChannelCount <= 3);

			//Slow enough that there's no capacity limits
			else
				return true;
		}
		else
		{
			if(rate > RATE_5GSPS)
				return false;

			//5 Gsps allows only one channel/pod
			else if(rate >= RATE_2P5GSPS)
				return (EnabledChannelCount == 0);

			//2.5 Gsps is allowed up to 2 total channels/pods
			else if(rate >= RATE_1P25GSPS)
				return (EnabledChannelCount <= 1);

			//1.25 Gsps is allowed up to 4 total channels/pods
			else if(rate >= RATE_625MSPS)
				return (EnabledChannelCount <= 3);

			//Slow enough that there's no capacity limits
			else
				return true;
		}
	}
	else
	{
		if(rate > RATE_1GSPS)
			return false;

		//1 Gsps allows only one channel/pod
		else if(rate >= RATE_500MSPS)
			return (EnabledChannelCount == 0);

		//500 Msps is allowed up to 2 total channels/pods
		else if(rate >= RATE_250MSPS)
			return (EnabledChannelCount <= 1);

		//250 Msps is allowed up to 4 total channels/pods
		else if(rate >= RATE_125MSPS)
			return (EnabledChannelCount <= 3);

		//Slow enough that there's no capacity limits
		else
			return true;
	}
}

/**
	@brief Checks if we can enable a channel on a 3000 series scope configured for 10-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel3000Series10Bit(size_t i)
{
	int64_t rate = GetSampleRate();
	size_t EnabledDigitalChannelCount = GetEnabledDigitalPodCount();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + EnabledDigitalChannelCount;

		if( (IsChannelIndexDigital(i)) || (EnabledDigitalChannelCount > 0) )
		{
			if(rate > RATE_1P25GSPS)
				return false;

			//1.25 Gsps is allowed up to 2 total channels/pods
			else if(rate >= RATE_625MSPS)
				return (EnabledChannelCount <= 1);

			//625 Msps is allowed up to 4 total channels/pods
			else if(rate >= RATE_625MSPS/2)
				return (EnabledChannelCount <= 3);

			//Slow enough that there's no capacity limits
			else
				return true;
		}
		else
		{
			if(rate > RATE_2P5GSPS)
				return false;

			//2.5 Gsps allows only one channel/pod
			else if(rate >= RATE_1P25GSPS)
				return (EnabledChannelCount == 0);

			//1.25 Gsps is allowed up to 2 total channels/pods
			else if(rate >= RATE_625MSPS)
				return (EnabledChannelCount <= 1);

			//625 Msps is allowed up to 4 total channels/pods
			else if(rate >= RATE_625MSPS/2)
				return (EnabledChannelCount <= 3);

			//Slow enough that there's no capacity limits
			else
				return true;
		}
}

/**
	@brief Checks if we can enable a channel on a 2000 series scope configured for 8-bit ADC resolution
 */
bool PicoOscilloscope::CanEnableChannel2000Series8Bit(size_t /*i*/)
{
	return true;
}

/**
	@brief Checks if higher ADC resolutions are available
 */

bool PicoOscilloscope::Is10BitModeAvailable()
{
	//10-bit only available for 6x2xE and 3000E models
	if( !( (m_model[4] == 'E') and (m_model[2] != '0') ) )
		return false;

	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	//5 Gsps is easy, just a bandwidth cap
	if(rate >= RATE_5GSPS)
		return (EnabledChannelCount <= 1);

	//2.5 Gsps has banking restrictions on 8 channel scopes
	else if(rate >= RATE_2P5GSPS)
	{
		if(EnabledChannelCount > 2)
			return false;

		else if(m_analogChannelCount == 8)
		{
			if(GetEnabledAnalogChannelCountAToB() > 1)
				return false;
			else if(GetEnabledAnalogChannelCountCToD() > 1)
				return false;
			else if(GetEnabledAnalogChannelCountEToF() > 1)
				return false;
			else if(GetEnabledAnalogChannelCountGToH() > 1)
				return false;
			else
				return true;
		}

		else
			return true;
	}

	//1.25 Gsps and 625 Msps are just bandwidth caps
	else if(rate >= RATE_1P25GSPS)
		return (EnabledChannelCount <= 4);
	else if(rate >= RATE_625MSPS)
		return (EnabledChannelCount <= 8);

	//No capacity limits
	else
		return true;
}

bool PicoOscilloscope::Is12BitModeAvailable()
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	switch(m_picoSeries)
	{
		case 4:
			return true;

		case 5:
			//12 bit mode only available at 500 Msps and below
			if(rate > RATE_500MSPS)
				return false;
			//500 Msps only one channel
			else if(rate > RATE_250MSPS)
				return (EnabledChannelCount <= 1);
			//250 Msps allows 2 channels
			else if(rate > RATE_125MSPS)
				return (EnabledChannelCount <= 2);
			//125 Msps allows 4 channels
			else if(rate > RATE_62P5MSPS)
				return (EnabledChannelCount <= 4);
			//62.5 Msps allows more than 4 channels
			else
				return true;

		case 6:
			//12 bit mode only available at 1.25 Gsps and below
			if(rate > RATE_1P25GSPS)
				return false;

			//1.25 Gsps and below have the same banking restrictions: at most one channel from the left and right half
			else
			{
				if(m_analogChannelCount == 8)
					return (GetEnabledAnalogChannelCountAToD() <= 1) && (GetEnabledAnalogChannelCountEToH() <= 1);
				else
					return (GetEnabledAnalogChannelCountAToB() <= 1) && (GetEnabledAnalogChannelCountCToD() <= 1);
			}

		default:
			return false;
	}
}

bool PicoOscilloscope::Is14BitModeAvailable()
{
	int64_t rate = GetSampleRate();
	size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	switch(m_picoSeries)
	{
		case 4:
			if(m_model.find("4444") == string::npos)
				return false;
			else
			{
				//14 bit mode only available at 50 Msps and below
				if(rate > RATE_50MSPS)
					return false;
				else
					return true;
			}

		case 5:
			//14 bit mode only available at 125 Msps and below
			if(rate > RATE_125MSPS)
				return false;
			//125 Msps allows 4 channels
			else if(rate > RATE_62P5MSPS)
				return (EnabledChannelCount <= 4);
			//62.5 Msps allows more than 4 channels
			else
				return true;

		default:
			return false;
	}
}

bool PicoOscilloscope::Is15BitModeAvailable()
{
	int64_t rate = GetSampleRate();
	//size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	switch(m_picoSeries)
	{
		case 5:
			//15 bit mode only available at 125 Msps and below
			if(rate > RATE_125MSPS)
				return false;
			//125 Msps allows 2 channels plus one or two digital channels, but no more
			else
				return (GetEnabledAnalogChannelCount() <= 2);

		default:
			return false;
	}
}

bool PicoOscilloscope::Is16BitModeAvailable()
{
	int64_t rate = GetSampleRate();
	//size_t EnabledChannelCount = GetEnabledAnalogChannelCount() + GetEnabledDigitalPodCount();

	switch(m_picoSeries)
	{
		case 5:
			//16 bit mode only available at 62.5 Msps and below
			if(rate > RATE_62P5MSPS)
				return false;
			//62.5 Msps allows 1 channel plus one or two digital channels, but no more
			else
				return (GetEnabledAnalogChannelCount() <= 1);

		default:
			return false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator

vector<FunctionGenerator::WaveShape> PicoOscilloscope::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(FunctionGenerator::SHAPE_SINE);
	ret.push_back(FunctionGenerator::SHAPE_SQUARE);
	ret.push_back(FunctionGenerator::SHAPE_TRIANGLE);
	ret.push_back(FunctionGenerator::SHAPE_DC);
	ret.push_back(FunctionGenerator::SHAPE_NOISE);
	ret.push_back(FunctionGenerator::SHAPE_SAWTOOTH_UP);
	ret.push_back(FunctionGenerator::SHAPE_SAWTOOTH_DOWN);
	ret.push_back(FunctionGenerator::SHAPE_SINC);
	ret.push_back(FunctionGenerator::SHAPE_GAUSSIAN);
	ret.push_back(FunctionGenerator::SHAPE_HALF_SINE);
	ret.push_back(FunctionGenerator::SHAPE_PRBS_NONSTANDARD);
	return ret;
}

bool PicoOscilloscope::GetFunctionChannelActive(int /*chan*/)
{
	return m_awgEnabled;
}

void PicoOscilloscope::SetFunctionChannelActive(int /*chan*/, bool on)
{
	m_awgEnabled = on;

	if(on)
		m_transport->SendCommandQueued("AWG:START");
	else
		m_transport->SendCommandQueued("AWG:STOP");
}

float PicoOscilloscope::GetFunctionChannelDutyCycle(int /*chan*/)
{
	return m_awgDutyCycle;
}

void PicoOscilloscope::SetFunctionChannelDutyCycle(int /*chan*/, float duty)
{
	m_awgDutyCycle = duty;
	m_transport->SendCommandQueued(string("AWG:DUTY ") + to_string(duty));
}

float PicoOscilloscope::GetFunctionChannelAmplitude(int /*chan*/)
{
	return m_awgRange;
}

void PicoOscilloscope::SetFunctionChannelAmplitude(int /*chan*/, float amplitude)
{
	m_awgRange = amplitude;

	//Rescale if load is not high-Z
	if(m_awgImpedance == IMPEDANCE_50_OHM)
		amplitude *= 2;

	m_transport->SendCommandQueued(string("AWG:RANGE ") + to_string(amplitude));
}

float PicoOscilloscope::GetFunctionChannelOffset(int /*chan*/)
{
	return m_awgOffset;
}

void PicoOscilloscope::SetFunctionChannelOffset(int /*chan*/, float offset)
{
	m_awgOffset = offset;

	//Rescale if load is not high-Z
	if(m_awgImpedance == IMPEDANCE_50_OHM)
		offset *= 2;

	m_transport->SendCommandQueued(string("AWG:OFFS ") + to_string(offset));
}

float PicoOscilloscope::GetFunctionChannelFrequency(int /*chan*/)
{
	return m_awgFrequency;
}

void PicoOscilloscope::SetFunctionChannelFrequency(int /*chan*/, float hz)
{
	m_awgFrequency = hz;
	m_transport->SendCommandQueued(string("AWG:FREQ ") + to_string(hz));
}

FunctionGenerator::WaveShape PicoOscilloscope::GetFunctionChannelShape(int /*chan*/)
{
	return m_awgShape;
}

void PicoOscilloscope::SetFunctionChannelShape(int /*chan*/, WaveShape shape)
{
	m_awgShape = shape;

	switch(shape)
	{
		case SHAPE_SINE:
			m_transport->SendCommandQueued(string("AWG:SHAPE SINE"));
			break;

		case SHAPE_SQUARE:
			m_transport->SendCommandQueued(string("AWG:SHAPE SQUARE"));
			break;

		case SHAPE_TRIANGLE:
			m_transport->SendCommandQueued(string("AWG:SHAPE TRIANGLE"));
			break;

		case SHAPE_DC:
			m_transport->SendCommandQueued(string("AWG:SHAPE DC"));
			break;

		case SHAPE_NOISE:
			m_transport->SendCommandQueued(string("AWG:SHAPE WHITENOISE"));
			break;

		case SHAPE_SAWTOOTH_UP:
			m_transport->SendCommandQueued(string("AWG:SHAPE RAMP_UP"));
			break;

		case SHAPE_SAWTOOTH_DOWN:
			m_transport->SendCommandQueued(string("AWG:SHAPE RAMP_DOWN"));
			break;

		case SHAPE_SINC:
			m_transport->SendCommandQueued(string("AWG:SHAPE SINC"));
			break;

		case SHAPE_GAUSSIAN:
			m_transport->SendCommandQueued(string("AWG:SHAPE GAUSSIAN"));
			break;

		case SHAPE_HALF_SINE:
			m_transport->SendCommandQueued(string("AWG:SHAPE HALF_SINE"));
			break;

		case SHAPE_PRBS_NONSTANDARD:
			m_transport->SendCommandQueued(string("AWG:SHAPE PRBS"));
			//per Martyn at Pico:
			//lfsr42 <= lfsr42(lfsr42'HIGH - 1 downto 0) & (lfsr42(41) xnor lfsr42(40)
			//xnor lfsr42(19) xnor lfsr42(18));
			break;

		default:
			break;
	}
}

bool PicoOscilloscope::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return false;
}

FunctionGenerator::OutputImpedance PicoOscilloscope::GetFunctionChannelOutputImpedance(int /*chan*/)
{
	return m_awgImpedance;
}

void PicoOscilloscope::SetFunctionChannelOutputImpedance(int chan, OutputImpedance z)
{
	//Save old offset/amplitude
	float off = GetFunctionChannelOffset(chan);
	float amp = GetFunctionChannelAmplitude(chan);

	m_awgImpedance = z;

	//Restore with new impedance
	SetFunctionChannelAmplitude(chan, amp);
	SetFunctionChannelOffset(chan, off);
}
