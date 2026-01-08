/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of ThunderScopeOscilloscope
	@ingroup scopedrivers
 */

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "ThunderScopeOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;

enum ThunderscopeDataType_e {
	DATATYPE_I8 = 2,
	DATATYPE_I16 = 4
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport connected to a TS.NET instance
 */
ThunderScopeOscilloscope::ThunderScopeOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, RemoteBridgeOscilloscope(transport, true)
	, m_diag_hardwareWFMHz(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ))
	, m_diag_receivedWFMHz(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ))
	, m_diag_totalWFMs(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS))
	, m_diag_droppedWFMs(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS))
	, m_diag_droppedPercent(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT))
	, m_nextWaveformWriteBuffer(0)
	, m_adcMode(MODE_8BIT)
	, m_lastSeq(0)
	, m_dropUntilSeq(0)
{
	m_analogChannelCount = 4;

	//Add analog channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = "CHAN" + to_string(i + 1);

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

		chan->SetDisplayName(chname);

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 1;
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_DC_1M);
		SetChannelOffset(i, 0,  0);
		SetChannelVoltageRange(i, 0, 5);
	}

	//Set up the data plane socket
	auto csock = dynamic_cast<SCPITwinLanTransport*>(m_transport);
	if(!csock)
		LogFatal("ThunderScopeOscilloscope expects a SCPITwinLanTransport\n");

	//Request entry to credit-based flow control mode rather than lock-step mode
	const uint8_t r = 'C';
	m_transport->SendRawData(1, &r);

	//set initial bandwidth on all channels to full
	m_bandwidthLimits.resize(4);
	for(size_t i=0; i<4; i++)
		SetChannelBandwidthLimit(i, 0);

	//Set all channels off by default
	for(size_t i=0; i<4; i++)
		DisableChannel(i);

	//Set initial memory configuration: 1M point depth @ 1 Gsps
	//This must happen before the trigger is configured, since trigger validation depends on knowing memory depth
	SetSampleRate(1000000000L);
	SetSampleDepth(1000000);

	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(GetOscilloscopeChannel(0)));
	SetTrigger(trig);
	SetTriggerOffset(1000000000); //1us to allow trigphase interpolation
	//don't need a second PushTrigger() call, SetTriggerOffset will implicitly do one

	m_diagnosticValues["Hardware WFM/s"] = &m_diag_hardwareWFMHz;
	m_diagnosticValues["Received WFM/s"] = &m_diag_receivedWFMHz;
	m_diagnosticValues["Total Waveforms Received"] = &m_diag_totalWFMs;
	m_diagnosticValues["Received Waveforms Dropped"] = &m_diag_droppedWFMs;
	m_diagnosticValues["% Received Waveforms Dropped"] = &m_diag_droppedPercent;

	ResetPerCaptureDiagnostics();

	//Initialize waveform buffers. Allocate one extra so we can overlap download and conversion
	for(size_t i=0; i<m_analogChannelCount + 1; i++)
	{
		m_analogRawWaveformBuffers.push_back(std::make_unique<AcceleratorBuffer<int16_t> >());
		m_analogRawWaveformBuffers[i]->SetCpuAccessHint(AcceleratorBuffer<int16_t>::HINT_LIKELY);
		m_analogRawWaveformBuffers[i]->SetGpuAccessHint(AcceleratorBuffer<int16_t>::HINT_LIKELY);
	}

	//Create Vulkan objects for the waveform conversion
	m_queue = g_vkQueueManager->GetComputeQueue("ThunderScopeOscilloscope.queue");
	vk::CommandPoolCreateInfo poolInfo(
		vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		m_queue->m_family );
	m_pool = make_unique<vk::raii::CommandPool>(*g_vkComputeDevice, poolInfo);

	vk::CommandBufferAllocateInfo bufinfo(**m_pool, vk::CommandBufferLevel::ePrimary, 1);
	m_cmdBuf = make_unique<vk::raii::CommandBuffer>(
		std::move(vk::raii::CommandBuffers(*g_vkComputeDevice, bufinfo).front()));

	if(g_hasDebugUtils)
	{
		string poolname = "ThunderScopeOscilloscope.pool";
		string bufname = "ThunderScopeOscilloscope.cmdbuf";

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

	m_conversion8BitPipeline = make_unique<ComputePipeline>(
		"shaders/Convert8BitSamples.spv", 2, sizeof(ConvertRawSamplesShaderArgs) );

	m_conversion16BitPipeline = make_unique<ComputePipeline>(
		"shaders/Convert16BitSamples.spv", 2, sizeof(ConvertRawSamplesShaderArgs) );

	m_clippingBuffer.resize(1);

	//TODO: query ADC mode on hardware
}

/**
	@brief Reset performance counters at the start of a capture
 */
void ThunderScopeOscilloscope::ResetPerCaptureDiagnostics()
{
	m_diag_hardwareWFMHz.SetFloatVal(0);
	m_diag_receivedWFMHz.SetFloatVal(0);
	m_diag_totalWFMs.SetIntVal(0);
	m_diag_droppedWFMs.SetIntVal(0);
	m_diag_droppedPercent.SetFloatVal(1);
	m_receiveClock.Reset();
}

/**
	@brief Color the channels based on our standard color sequence (blue-red-green-yellow)
 */
string ThunderScopeOscilloscope::GetChannelColor(size_t i)
{
	switch(i % 4)
	{
		case 0:
			return "#4040ff";

		case 1:
			return "#ff4040";

		case 2:
			return "#208020";

		case 3:
		default:
			return "#ffff00";
	}
}

ThunderScopeOscilloscope::~ThunderScopeOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int ThunderScopeOscilloscope::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t ThunderScopeOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

///@brief Return the driver name "thunderscope"
string ThunderScopeOscilloscope::GetDriverNameInternal()
{
	return "thunderscope";
}

void ThunderScopeOscilloscope::FlushConfigCache()
{
	//Refresh sample rate from hardware
	RefreshSampleRate();
}

void ThunderScopeOscilloscope::RefreshSampleRate()
{
	auto reply = m_transport->SendCommandQueuedWithReply("ACQ:RATE?");
	m_srate = stoi(reply);
}

void ThunderScopeOscilloscope::EnableChannel(size_t i)
{
	RemoteBridgeOscilloscope::EnableChannel(i);
	RefreshSampleRate();
}

double ThunderScopeOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void ThunderScopeOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	double oldAtten = m_channelAttenuations[i];
	m_channelAttenuations[i] = atten;

	//Rescale channel voltage range and offset
	double delta = atten / oldAtten;
	m_channelVoltageRanges[i] *= delta;
	m_channelOffsets[i] *= delta;
}

unsigned int ThunderScopeOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_bandwidthLimits[i];
}

void ThunderScopeOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_bandwidthLimits[i] = limit_mhz;
	}

	if(limit_mhz == 0)
		m_transport->SendCommandQueued(string(":") + m_channels[i]->GetHwname() + ":BAND FULL");
	else
		m_transport->SendCommandQueued(string(":") + m_channels[i]->GetHwname() + ":BAND " + to_string(limit_mhz) + "M");
}

vector<unsigned int> ThunderScopeOscilloscope::GetChannelBandwidthLimiters([[maybe_unused]] size_t i)
{
	vector<unsigned int> ret;
	ret.push_back(20);
	ret.push_back(100);
	ret.push_back(200);
	ret.push_back(350);
	ret.push_back(650);
	ret.push_back(750);
	ret.push_back(0);
	return ret;
}

OscilloscopeChannel* ThunderScopeOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

void ThunderScopeOscilloscope::BackgroundProcessing()
{
	//Call the base class to flush the transport etc
	RemoteBridgeOscilloscope::BackgroundProcessing();

	//Push any previously acquired waveforms to the RX buffer if we have them
	lock_guard<recursive_mutex> wipLock(m_wipWaveformMutex);
	PushPendingWaveformsIfReady();
}

Oscilloscope::TriggerMode ThunderScopeOscilloscope::PollTrigger()
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

bool ThunderScopeOscilloscope::AcquireData()
{
	return DoAcquireData(true);
}

bool ThunderScopeOscilloscope::DoAcquireData(bool keep)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range range("ThunderScopeOscilloscope::DoAcquireData");
	#endif

	//Read Version No.
	uint8_t version;
	if(!m_transport->ReadRawData(sizeof(version), (uint8_t*)&version))
		return false;

	//Read the sequence number of the current waveform
	if(!m_transport->ReadRawData(sizeof(m_lastSeq), (uint8_t*)&m_lastSeq))
		return false;

	//Acknowledge receipt of this waveform
	m_transport->SendRawData(4, (uint8_t*)&m_lastSeq);

	if(!keep)
		LogTrace("Dropping waveform %u\n",m_lastSeq);
	else
	{
		//LogTrace("Keeping waveform %u\n",m_lastSeq);
	}

	//Read the number of channels in the current waveform
	uint16_t numChannels;
	if(!m_transport->ReadRawData(sizeof(numChannels), (uint8_t*)&numChannels))
		return false;

	//Get the sample interval.
	//May be different from m_srate if we changed the rate after the trigger was armed
	uint64_t fs_per_sample;
	if(!m_transport->ReadRawData(sizeof(fs_per_sample), (uint8_t*)&fs_per_sample))
		return false;

	//Get the de-facto trigger position.
	int64_t trigger_fs;
	if(!m_transport->ReadRawData(sizeof(trigger_fs), (uint8_t*)&trigger_fs))
		return false;

	{
		lock_guard<recursive_mutex> lock(m_mutex);
		if (m_triggerOffset != trigger_fs)
		{
			AddDiagnosticLog("Correcting trigger offset by " + to_string(m_triggerOffset - trigger_fs));
			m_triggerOffset = trigger_fs;
		}
	}

	//Get the de-facto hardware capture rate.
	double wfms_s;
	if(!m_transport->ReadRawData(sizeof(wfms_s), (uint8_t*)&wfms_s))
		return false;

	if(keep)
		m_diag_hardwareWFMHz.SetFloatVal(wfms_s);

	//Acquire data for each channel
	uint8_t chnum;
	uint8_t dataType;
	uint64_t memdepth;
	float config[3];
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	lock_guard<recursive_mutex> wipLock(m_wipWaveformMutex);

	//Analog channels get processed separately
	vector<UniformAnalogWaveform*> awfms;
	vector<size_t> achans;
	vector<float> scales;
	vector<float> offsets;

	bool processedWaveformsOnGPU = true;

	for(size_t i=0; i<numChannels; i++)
	{
		//Get channel ID and memory depth (samples, not bytes)
		if(!m_transport->ReadRawData(sizeof(chnum), (uint8_t*)&chnum))
			return false;
		if(!m_transport->ReadRawData(sizeof(memdepth), (uint8_t*)&memdepth))
			return false;

		//Grab the next free buffer
		auto& abuf = m_analogRawWaveformBuffers[m_nextWaveformWriteBuffer];
		m_nextWaveformWriteBuffer = (m_nextWaveformWriteBuffer + 1) % m_analogRawWaveformBuffers.size();
		abuf->resize(memdepth);
		abuf->PrepareForCpuAccess();
		achans.push_back(chnum);

		#ifdef HAVE_NVTX
			nvtx3::scoped_range range2(string("Channel ") + to_string(chnum));
		#endif

		//Analog channels
		if(chnum < m_analogChannelCount)
		{
			auto buf = abuf->GetCpuPointer();

			//Scale and offset are sent in the header since they might have changed since the capture began
			if(!m_transport->ReadRawData(sizeof(config), (uint8_t*)&config))
				return false;

			float scale = config[0];
			float offset = config[1];
			float trigphase = config[2];
			scale *= GetChannelAttenuation(chnum);
			offset *= GetChannelAttenuation(chnum);

			bool clipping;
			if(!m_transport->ReadRawData(sizeof(clipping), (uint8_t*)&clipping))
				return false;

			if(!m_transport->ReadRawData(sizeof(dataType), (uint8_t*)&dataType))
				return false;

			//TODO: stream timestamp from the server
			uint32_t depth = memdepth * sizeof(int8_t);
			if(dataType == DATATYPE_I16)
				depth = memdepth * sizeof(int16_t);
			if(!m_transport->ReadRawData(depth, (uint8_t*)buf))
				return false;
			abuf->MarkModifiedFromCpu();

			//If discarding data, stop processing at this point
			if(!keep)
				continue;

			//Create our waveform
			auto cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(i)->GetHwname());
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = trigphase;
			cap->m_startTimestamp = t;
			cap->m_startFemtoseconds = fs;
			if (clipping)
				cap->m_flags |= WaveformBase::WAVEFORM_CLIPPING;

			cap->Resize(memdepth);
			awfms.push_back(cap);
			scales.push_back(scale);
			offsets.push_back(offset);

			//Clear out any previously pending waveforms before we queue up this one
			if(i == 0)
				PushPendingWaveformsIfReady();

			m_wipWaveforms[GetOscilloscopeChannel(chnum)] = cap;

			//Kick off the GPU-side processing of the waveform to run nonblocking while we download the next
			//Wait for any previous waveform processing to finish first, since we're reusing the command buffer
			if(!keep)
			{}
			else if(g_hasShaderInt8 && g_hasPushDescriptor && (dataType == DATATYPE_I8))
			{
				m_queue->WaitIdle();
				m_cmdBuf->begin({});

				m_conversion8BitPipeline->Bind(*m_cmdBuf);
				m_conversion8BitPipeline->BindBufferNonblocking(0, cap->m_samples, *m_cmdBuf, true);
				m_conversion8BitPipeline->BindBufferNonblocking(1, *abuf, *m_cmdBuf);

				ConvertRawSamplesShaderArgs args;
				args.size = cap->size();
				args.gain = scales[i];
				args.offset = -offsets[i];

				const uint32_t compute_block_count = GetComputeBlockCount(cap->size(), 64);
				m_conversion8BitPipeline->DispatchNoRebind(
					*m_cmdBuf, args,
					min(compute_block_count, 32768u),
					compute_block_count / 32768 + 1);

				cap->MarkModifiedFromGpu();
				m_cmdBuf->end();
				m_queue->Submit(*m_cmdBuf);
			}
			else if(g_hasShaderInt16 && g_hasPushDescriptor && (dataType == DATATYPE_I16))
			{
				m_queue->WaitIdle();
				m_cmdBuf->begin({});

				m_conversion16BitPipeline->Bind(*m_cmdBuf);
				m_conversion16BitPipeline->BindBufferNonblocking(0, cap->m_samples, *m_cmdBuf, true);
				m_conversion16BitPipeline->BindBufferNonblocking(1, *abuf, *m_cmdBuf);

				ConvertRawSamplesShaderArgs args;
				args.size = cap->size();
				args.gain = scales[i];
				args.offset = -offsets[i];

				const uint32_t compute_block_count = GetComputeBlockCount(cap->size(), 64);
				m_conversion16BitPipeline->DispatchNoRebind(
					*m_cmdBuf, args,
					min(compute_block_count, 32768u),
					compute_block_count / 32768 + 1);

				cap->MarkModifiedFromGpu();
				m_cmdBuf->end();
				m_queue->Submit(*m_cmdBuf);
			}

			//Not available, fall back to CPU side processing
			else
				processedWaveformsOnGPU = false;
		}
		else
		{
			LogFatal("???\n");
		}
	}

	if(!keep)
		return true;

	//Fallback path if GPU doesn't have suitable integer support
	if(!processedWaveformsOnGPU)
	{
		//Process analog captures in parallel
		#pragma omp parallel for
		for(size_t i=0; i<awfms.size(); i++)
		{
			auto cap = awfms[i];
			cap->PrepareForCpuAccess();
			if(dataType == DATATYPE_I8)
			{
				Convert8BitSamples(
					(float*)&cap->m_samples[0],
					(int8_t*)m_analogRawWaveformBuffers[achans[i]]->GetCpuPointer(),
					scales[i],
					offsets[i],
					cap->m_samples.size());
			}
			else if(dataType == DATATYPE_I16)
			{
				Convert16BitSamples(
					(float*)&cap->m_samples[0],
					(int16_t*)m_analogRawWaveformBuffers[achans[i]]->GetCpuPointer(),
					scales[i],
					offsets[i],
					cap->m_samples.size());
			}
			cap->MarkModifiedFromCpu();
		}

		//If we did CPU side conversion, push the waveforms to our queue now
		PushPendingWaveformsIfReady();
	}

	m_receiveClock.Tick();
	m_diag_receivedWFMHz.SetFloatVal(m_receiveClock.GetAverageHz());

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

/**
	@brief Wait for waveform conversion to finish, then push it to the pending waveforms buffer
 */
void ThunderScopeOscilloscope::PushPendingWaveformsIfReady()
{
	if(m_wipWaveforms.empty())
		return;

	//Wait up to 1ms for GPU side conversion to finish and return if it's not done
	if(!m_queue->WaitIdleWithTimeout(1000 * 1000))
		return;

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(m_wipWaveforms);

	//Bump waveform performance counters
	FilterParameter* param = &m_diag_totalWFMs;
	int total = param->GetIntVal() + 1;
	param->SetIntVal(total);

	//If we got backed up, drop the extra waveforms
	param = &m_diag_droppedWFMs;
	int dropped = param->GetIntVal();
	while (m_pendingWaveforms.size() > 2)
	{
		LogTrace("Dropping waveform due to excessive pend queue depth\n");

		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			AddWaveformToAnalogPool(it.second);
		m_pendingWaveforms.pop_front();

		dropped++;
	}

	//Update dropped waveform perf counter
	param->SetIntVal(dropped);
	param = &m_diag_droppedPercent;
	param->SetFloatVal((float)dropped / (float)total);

	m_pendingWaveformsMutex.unlock();

	m_wipWaveforms.clear();

	#ifdef HAVE_NVTX
		nvtxMark("Pushed waveforms");
	#endif
}

void ThunderScopeOscilloscope::Start()
{
	m_triggerArmed = true; //FIXME

	m_transport->SendCommandQueued("NORMAL");
	m_transport->SendCommandQueued("RUN");

	m_triggerArmed = true;
	m_triggerOneShot = false;

	ResetPerCaptureDiagnostics();
}

void ThunderScopeOscilloscope::Stop()
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

void ThunderScopeOscilloscope::StartSingleTrigger()
{
	RemoteBridgeOscilloscope::StartSingleTrigger();
	ResetPerCaptureDiagnostics();
}

void ThunderScopeOscilloscope::ForceTrigger()
{
	m_transport->SendCommandQueued("SINGLE");
	m_transport->SendCommandQueued("FORCE");

	m_triggerArmed = true;
	m_triggerOneShot = true;

	ResetPerCaptureDiagnostics();
}

void ThunderScopeOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	//Type
	m_transport->SendCommandQueued("TRIG:TYPE EDGE");

	//Delay
	m_transport->SendCommandQueued("TRIG:DELAY " + to_string(m_triggerOffset));

	//Source
	auto chan = dynamic_cast<OscilloscopeChannel*>(trig->GetInput(0).m_channel);
	m_transport->SendCommandQueued("TRIG:SOU " + chan->GetHwname());

	//Level
	char buf[128];
	snprintf(buf, sizeof(buf), "TRIG:EDGE:LEV %f", trig->GetLevel() / chan->GetAttenuation());
	m_transport->SendCommandQueued(buf);

	//Slope
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommandQueued("TRIG:EDGE:DIR RISING");
			break;
		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommandQueued("TRIG:EDGE:DIR FALLING");
			break;
		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommandQueued("TRIG:EDGE:DIR ANY");
			break;
		default:
			LogWarning("Unknown edge type\n");
			return;
	}
}

void ThunderScopeOscilloscope::SetSampleDepth(uint64_t depth)
{
	m_transport->SendCommandQueued(string("ACQ:DEPTH ") + to_string(depth));
	m_mdepth = depth;
}

void ThunderScopeOscilloscope::SetSampleRate(uint64_t rate)
{
	m_srate = rate;
	m_transport->SendCommandQueued(string("ACQ:RATE ") + to_string(rate));
}

vector<uint64_t> ThunderScopeOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	string rates = m_transport->SendCommandQueuedWithReply("ACQ:RATES?");

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = rates.find(',', i+1);
		if(i == string::npos)
			break;

		auto block = rates.substr(istart, i-istart);
		auto hz = stol(block);
		ret.push_back(hz);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> ThunderScopeOscilloscope::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

//interleaving not supported
bool ThunderScopeOscilloscope::CanInterleave()
{
	return false;
}

bool ThunderScopeOscilloscope::HasInterleavingControls()
{
	return false;
}

set<Oscilloscope::InterleaveConflict> ThunderScopeOscilloscope::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> ThunderScopeOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

	string depths = m_transport->SendCommandQueuedWithReply("ACQ:DEPTHS?");

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = depths.find(',', i+1);
		if(i == string::npos)
			break;

		ret.push_back(stol(depths.substr(istart, i-istart)));

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> ThunderScopeOscilloscope::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

bool ThunderScopeOscilloscope::IsInterleaving()
{
	//interleaving not supported
	return false;
}

bool ThunderScopeOscilloscope::SetInterleaving(bool /*combine*/)
{
	//interleaving not supported
	return false;
}

vector<OscilloscopeChannel::CouplingType> ThunderScopeOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

void ThunderScopeOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	vector<OscilloscopeChannel::CouplingType> available = GetAvailableCouplings(i);

	if (!count(available.begin(), available.end(), type))
	{
		return;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":COUP AC");
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":TERM 1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":COUP DC");
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":TERM 1M");
			break;

		case OscilloscopeChannel::COUPLE_AC_50:
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":COUP AC");
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":TERM 50");
			break;

		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":COUP DC");
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":TERM 50");
			break;

		default:
			LogError("Coupling not supported in ThunderScopeOscilloscope: %d\n", type);
			return;
	}

	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelCouplings[i] = type;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ADC modes

bool ThunderScopeOscilloscope::IsADCModeConfigurable()
{
	return true;
}

vector<string> ThunderScopeOscilloscope::GetADCModeNames([[maybe_unused]] size_t channel)
{
	vector<string> ret;
	ret.push_back("8 bit");
	ret.push_back("12 bit");
	return ret;
}

size_t ThunderScopeOscilloscope::GetADCMode([[maybe_unused]] size_t channel)
{
	return m_adcMode;
}

void ThunderScopeOscilloscope::SetADCMode([[maybe_unused]] size_t channel, size_t mode)
{
	switch(mode)
	{
		case 0:
			m_adcMode = MODE_8BIT;
			m_transport->SendCommandQueued("ACQ:RES 8");
			break;

		//12 bit mode has lower Fmax so need to refresh sample rate in case the scope clamped us
		case 1:
			m_adcMode = MODE_12BIT;
			m_transport->SendCommandQueued("ACQ:RES 12");
			RefreshSampleRate();
			break;

		default:
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checking for validity of configurations

bool ThunderScopeOscilloscope::CanEnableChannel(size_t /*i*/)
{
	return true;
}
