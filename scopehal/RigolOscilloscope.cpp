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

#include "scopehal.h"
#include "RigolOscilloscope.h"
#include "EdgeTrigger.h"

#include <cinttypes>

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
// #pragma GCC diagnostic warning "-Wswitch-enum"


using namespace std;

template<typename T>
static optional<T> parseNumeric(string const &input, const string &fmt) {
	T output {};
	if (sscanf(input.c_str(), fmt.c_str(), &output) < 1)
		return nullopt;
	return output;
}

static auto parseDouble(string const &input, const string &fmt = "%lf")
{
	return parseNumeric<double>(input, fmt);
}

static auto parseU64(string const &input, const string &fmt = "%" PRIu64)
{
	return parseNumeric<uint64_t>(input, fmt);
}

//TODO: this would make sens to move to some utility library
template<typename T>
struct CallOnEOC {
	T onExit;
	CallOnEOC(T onExit_) : onExit(onExit_) {}
	~CallOnEOC() {
		onExit();
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

RigolOscilloscope::RigolOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_triggerArmed(false)
	, m_triggerWasLive(false)
	, m_triggerOneShot(false)
	, m_liveMode(false)
	, m_highDefinition(false)
{

	DecodeDeviceSeries();
	if(m_series == Series::UNKNOWN)
	{
		LogError("device series not recognized nor supported\n");
		return;
	}
	LogTrace("RigolOscilloscope: series: %d\n", int(m_series));

	AnalyzeDeviceCapabilities();

	for(auto i = 0U; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN") + to_string(i + 1);

		//Color the channels based on Rigol's standard color sequence (yellow-cyan-red-blue)
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#ffff00";
				break;

			case 1:
				color = "#00ffff";
				break;

			case 2:
				color = "#ff00ff";
				break;

			case 3:
				color = "#336699";
				break;
		}

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this, chname, color, Unit(Unit::UNIT_FS), Unit(Unit::UNIT_VOLTS), Stream::STREAM_TYPE_ANALOG, i);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();
	}

	if (m_digitalChannelCount != 0)
	{
		m_digitalBanks.push_back({});
		for(auto i = 0U; i < m_digitalChannelCount; i++)
		{
			//Hardware name of the channel
			string chname = string("D") + to_string(i);
	
			//Color the channels based on Rigol's standard color sequence (yellow-cyan-red-blue)
			string color = "#66ff00";
	
			//Create the channel
			auto chan = new OscilloscopeChannel(
				this, chname, color, Unit(Unit::UNIT_FS), Unit(Unit::UNIT_VOLTS), Stream::STREAM_TYPE_DIGITAL, m_analogChannelCount + i);
			m_channels.push_back(chan);
			if (m_digitalBanks.back().size() == DIGITAL_BANK_SIZE)
				m_digitalBanks.push_back({});
			m_digitalBanks.back().push_back(chan);
			chan->SetDefaultDisplayName();
		}
	}
	// Add the external trigger input
	auto const has_ext_trigger = [&]() -> bool
	{
		switch (m_series)
		{
			case Series::DHO800:
				// from DHO800/900 prog. manual: "EXT is only available for DHO812 and DHO802"
				// assuming it is based on analog channel count == 2 ...
				return m_analogChannelCount == 2;

			case Series::DS1000:
			case Series::MSODS1000Z:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO900:
				return true;
	
			case Series::MSO5000:
			case Series::UNKNOWN:
				return false;
		}
		return false;
	}();
	if (has_ext_trigger)
	{
		m_extTrigChannel = new OscilloscopeChannel(
			this, "External", "#FFFFFF", Unit(Unit::UNIT_FS), Unit(Unit::UNIT_VOLTS), Stream::STREAM_TYPE_TRIGGER, m_channels.size());
		m_channels.push_back(m_extTrigChannel);
		m_extTrigChannel->SetDefaultDisplayName();
	}

	// Add AC line external trigger input
	switch (m_series)
	{
		case Series::MSODS1000Z:
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DS1000:
			m_aclTrigChannel = new OscilloscopeChannel(
			this, "ACL", "#FF0000", Unit(Unit::UNIT_FS), Unit(Unit::UNIT_VOLTS), Stream::STREAM_TYPE_TRIGGER, m_channels.size());
			m_channels.push_back(m_aclTrigChannel);
			m_aclTrigChannel->SetDisplayName("ACLine");
			m_aclTrigChannel->SetDefaultDisplayName();
			break;
			
		case Series::DHO800:
		case Series::DHO900:
		case Series::UNKNOWN:
			break;
	}

	//Configure acquisition modes
	switch (m_series) {
		case Series::DS1000:
			// m_transport->SendCommandQueued(":WAV:POIN:MODE RAW");
			// this command is not listed anywhere in the docs
			break;
		
		case Series::MSODS1000Z:
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			m_transport->SendCommandQueued(string(":WAV:FORM ") + (m_highDefinition ? "WORD" : "BYTE"));
			m_transport->SendCommandQueued(":WAV:MODE RAW");
			break;

		case Series::UNKNOWN:
			break;
	}
	
	for(size_t i = 0; i < m_analogChannelCount; i++)
		m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":VERN ON");

	switch (m_series) {
		case Series::MSODS1000Z:
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			m_transport->SendCommandQueued(":TIM:VERN ON");
		break;
		
		case Series::DS1000:
		case Series::UNKNOWN:
			break;
	}

	FlushConfigCache();
	
	UpdateDynamicCapabilities();
	//make sure all setup commands finish before we proceed
	m_transport->FlushCommandQueue();
}

RigolOscilloscope::~RigolOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int RigolOscilloscope::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t RigolOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

void RigolOscilloscope::DecodeDeviceSeries()
{
	LogTrace("Decoding device series\n");
	// Called once from the ctor so we don't lock any mutex as there is only one reference to this object
	// and no concurrent access is possible at this time.

	// Last digit of the model number is the number of channels
	m_series = [&]() -> Series
	{
		// scope name is always, no numeric prefix, followed by numeric model number optionally followed by alfanumeric suffix
		auto cursor = m_model.begin();
		{
			// extract
			m_modelNew.prefix.clear();
			m_modelNew.prefix.resize(10); // preallocate space
			int length;
			if (1 != sscanf(cursor.base(), "%4[^0-9]%n", m_modelNew.prefix.data(), &length))
			{
				LogError("could not parse scope series prefix, got more than maximum expected length 3\n");
				return Series::UNKNOWN;
			}
			m_modelNew.prefix.resize(length);
			LogTrace("parsed model prefix %s\n", m_modelNew.prefix.c_str());
			cursor += length;
		}

		{
			// parse model number
			int length;
			if (1 != sscanf(cursor.base(), "%5u%n", &m_modelNew.number, &length))
			{
				LogError("could not parse scope model number\n");
				return Series::UNKNOWN;
			}
			LogTrace("parsed model numer %d\n", m_modelNew.number);
			cursor += length;
		}

		// extract suffix - just a remainder after model number
		m_modelNew.suffix.assign(cursor, m_model.end());

		// decode into device family
		if(m_modelNew.prefix == "DS" or m_modelNew.prefix == "MSO")
		{
			switch(m_modelNew.number / 1000)
			{
				case 1:
						if(m_modelNew.suffix.size() < 1)
							break;
						if(m_modelNew.suffix[0] == 'D' || m_modelNew.suffix[0] == 'E')
							return Series::DS1000;
						else if(m_modelNew.suffix[0] == 'Z')
							return Series::MSODS1000Z;
					break;
				case 5: return Series::MSO5000;
				default: break;
			}
		}
		else if (m_modelNew.prefix == "DHO")
		{
			if (m_modelNew.number < 1000)
			{
				switch (m_modelNew.number / 100)
				{
					case 8: return Series::DHO800;
					case 9: return Series::DHO900;
					default: break;
				}
			}
			else
			{
				switch (m_modelNew.number / 1000)
				{
					case 1: return Series::DHO1000;
					case 4: return Series::DHO4000;
					default: break;
				}
			}
		}
		LogError("model %s was not recognized\n", m_model.c_str());
			return Series::UNKNOWN;
	}();
}

void RigolOscilloscope::AnalyzeDeviceCapabilities() {
	// Called once from the ctor so we don't lock any mutex as there is only one reference to this object
	// and no concurrent access is possible at this time.
	LogTrace("Analyzing scope capabilities\n");

	// Last digit of the model number is the number of channels
	switch(m_series) {

		case Series::DS1000:
			m_analogChannelCount = m_modelNew.number % 10;
			m_bandwidth = m_modelNew.number % 1000 - m_analogChannelCount;
			//TODO:  there are DS1000D devices with LA
			break;

		case Series::MSODS1000Z:
		{
			m_analogChannelCount = m_modelNew.number % 10;
			m_bandwidth = m_modelNew.number % 1000 - m_analogChannelCount;

			m_digitalChannelCount = m_modelNew.prefix == "MSO" ? 16 : 0;

			{
				// Probe 24M memory depth option.
				// Hacky workaround since DS1000Z does not have a way how to query installed options
				// Only enable chan 1
				// TODO: what about LA channels on MSOs?
				m_transport->SendCommandQueued("CHAN1:DISP 1");
				m_transport->SendCommandQueued("CHAN2:DISP 0");
				if(m_analogChannelCount > 2)
				{
					m_transport->SendCommandQueued("CHAN3:DISP 0");
					m_transport->SendCommandQueued("CHAN4:DISP 0");
				}

				auto original_state = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:STAT?"));
				// Set in run mode to be able to set memory depth
				m_transport->SendCommandQueued("RUN");
				auto originalMdepth = Trim(m_transport->SendCommandQueuedWithReply("ACQ:MDEP?"));
				m_transport->SendCommandQueued("ACQ:MDEP 24000000");
				m_opt24M = Trim(m_transport->SendCommandQueuedWithReply("ACQ:MDEP?")) == "24000000";
				if (m_opt24M)
					LogTrace("DS1000Z: 24 Mpts memory option detected\n");
	
				// Reset memory depth to original value
				m_transport->SendCommandQueued("ACQ:MDEP " + originalMdepth);
				if (original_state == "STOP")
					m_transport->SendCommandQueued("STOP");
			}

			break;
		}

		case Series::MSO5000:
		{
			m_analogChannelCount = m_modelNew.number % 10;
			m_digitalChannelCount = 16;
			
			// Hacky workaround since :SYST:OPT:STAT doesn't work properly on some scopes
			// Only enable chan 1
			m_transport->SendCommandQueued("CHAN1:DISP 1");
			m_transport->SendCommandQueued("CHAN2:DISP 0");
			if(m_analogChannelCount > 2)
			{
				m_transport->SendCommandQueued("CHAN3:DISP 0");
				m_transport->SendCommandQueued("CHAN4:DISP 0");
			}
			// Set in run mode to be able to set memory depth
			m_transport->SendCommandQueued("RUN");

			auto originalMdepth = Trim(m_transport->SendCommandQueuedWithReply("ACQ:MDEP?"));
			m_transport->SendCommandQueued("ACQ:MDEP 200M");
			// Yes, it actually returns a stringified float, manual says "scientific notation"
			m_opt200M = Trim(m_transport->SendCommandQueuedWithReply("ACQ:MDEP?")) == "2.0000E+08";
			if (m_opt200M) {
				LogTrace("MSO5000: 200 Mpts memory option detected\n");
			}

			// Reset memory depth
			m_transport->SendCommandQueued("ACQ:MDEP 1M");
			string originalBandwidthLimit = m_transport->SendCommandQueuedWithReply("CHAN1:BWL?");

			// Figure out its actual bandwidth since :SYST:OPT:STAT is practically useless
			m_transport->SendCommandQueued("CHAN1:BWL 200M");
			

			// A bit of a tree, maybe write more beautiful code
			if(Trim(m_transport->SendCommandQueuedWithReply("CHAN1:BWL?")) == "200M")
				m_bandwidth = 350;
			else
			{
				m_transport->SendCommandQueued("CHAN1:BWL 100M");
				if(Trim(m_transport->SendCommandQueuedWithReply("CHAN1:BWL?")) == "100M")
					m_bandwidth = 200;
				else
				{
					if(m_modelNew.number % 1000 - m_modelNew.number % 10 == 100)
						m_bandwidth = 100;
					else
						m_bandwidth = 70;
				}
			}

			m_transport->SendCommandQueued("CHAN1:BWL " + originalBandwidthLimit);
			m_transport->SendCommandQueued("ACQ:MDEP " + originalMdepth);
			break;
		}

		//DHO are 12 bits (HD) models => default to high definition mode
		// DHO only attrs:
		// m_maxMdepth
		// m_maxSrate
		// m_lowSrate

		case Series::DHO800:
		{
			// - DHO802 (70MHz), DHO804 (70Mhz), DHO812 (100MHz),DHO814 (100MHz)
			m_analogChannelCount = m_modelNew.number % 10;
			m_digitalChannelCount = 0;
			m_bandwidth = m_modelNew.number % 100 / 10 * 100; // 814 -> 14 -> 1 -> 100
			if(m_bandwidth == 0) m_bandwidth = 70; // Fallback for DHO80x models

			m_highDefinition = true;
			m_maxMdepth = 25*1000*1000;
			m_maxSrate  = 1.25*1000*1000*1000;
			m_lowSrate = true;

			break;
		}
		
		case Series::DHO900:
		{
			// - DHO914/DHO914S (125MHz), DHO924/DHO924S (250MHz)
			m_analogChannelCount = m_modelNew.number % 10;
			m_digitalChannelCount = 16;
			m_bandwidth = m_modelNew.number % 100 / 10 * 125; // 914 -> 24 -> 2 -> 250

			m_highDefinition = true;
			m_maxMdepth = 50*1000*1000;
			m_maxSrate  = 1.25*1000*1000*1000;
			m_lowSrate = true;
			
			break;
		}

		case Series::DHO1000:
		{
			// - DHO1072 (70MHz), DHO1074 (70MHz), DHO1102 (100MHz), DHO1104 (100MHz), DHO1202 (200MHz), DHO1204 (200MHz)
			m_analogChannelCount = m_modelNew.number % 10;
			m_digitalChannelCount = 0;
			m_bandwidth = m_modelNew.number % 1000 / 10 * 10;

			m_highDefinition = true;

			m_maxMdepth = 50*1000*1000;
			m_maxSrate  = 2*1000*1000*1000;
			/* probe for bandwidth upgrades and memory upgrades on DHO1000 series */
			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? RLU\n")) == "1") {
				m_maxMdepth = 100*1000*1000;
				LogTrace("DHO1000: 100 Mpts memory option detected\n");
			}
			
			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW7T10\n")) == "1") {
				m_bandwidth = 100;
				LogTrace("DHO1000: 100 MHz bandwidth option detected\n");
			}

			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW7T20\n")) == "1") {
				m_bandwidth = 200;
				LogTrace("DHO1000: 200 MHz bandwidth option detected\n");
			}

			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW10T20\n")) == "1") {
				m_bandwidth = 200;
				LogTrace("DHO1000: 200 MHz bandwidth option detected\n");
			}

			break;
		}

		case Series::DHO4000:
		{
			// - DHO1072 (70MHz), DHO1074 (70MHz), DHO1102 (100MHz), DHO1104 (100MHz), DHO1202 (200MHz), DHO1204 (200MHz)
			m_analogChannelCount = m_modelNew.number % 10;
			m_digitalChannelCount = 0;
			m_bandwidth = m_modelNew.number % 1000 / 10 * 10;

			m_highDefinition = true;
			m_opt200M = false; // does not exist in DHO series
			m_lowSrate = false;

			m_maxMdepth = 250*1000*1000;
			m_maxSrate  = 4*1000*1000*1000U;
			/* probe for bandwidth upgrades and memory upgrades on DHO4000 series */
			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? RLU\n")) == "1") {
				m_maxMdepth = 500*1000*1000;
				LogTrace("DHO4000: 500 Mpts memory option detected\n");
			}
			
			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW2T4\n")) == "1") {
				m_bandwidth = 400;
				LogTrace("DHO4000: 400 MHz bandwidth option detected\n");
			}

			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW2T8\n")) == "1") {
				m_bandwidth = 800;
				LogTrace("DHO4000: 800 MHz bandwidth option detected\n");
			}

			if (Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW4T8\n")) == "1") {
				m_bandwidth = 800;
				LogTrace("DHO4000: 800 MHz bandwidth option detected\n");
			}
			break;
		}

		case Series::UNKNOWN: {
			LogError("RigolOscilloscope: unknown model, invalid state!\n");
		}
	}
	
	LogTrace("Device bandwidth: %d MHz\n", m_bandwidth);
}

static std::vector<uint64_t> dhoSampleDepths {
 	// Available sample depths for DHO models, regardless of model type and activated channels
	             1000,
	        10 * 1000,
	       100 * 1000,
	  1 * 1000 * 1000,
	  5 * 1000 * 1000, // only DHO800, since FW v00.01.03.00.04
	 10 * 1000 * 1000,
	 25 * 1000 * 1000,
	 50 * 1000 * 1000,
	100 * 1000 * 1000,
	250 * 1000 * 1000,
	500 * 1000 * 1000,
};

static std::vector<uint64_t> ds1000zSampleDepths {
	// Available sample depths for ds1000z models, regardless of model type and activated channels
	        12 * 1000,
	       120 * 1000,
	      1200 * 1000,
	 12 * 1000 * 1000,
	 24 * 1000 * 1000 // only as an option and only for 1 CH, presence checked at start.up
};

static std::vector<uint64_t> mso5000SampleDepths {
	             1000,
	        10 * 1000,
	       100 * 1000,
	      1000 * 1000,
	 10 * 1000 * 1000,
	 25 * 1000 * 1000,
	 50 * 1000 * 1000,
	100 * 1000 * 1000, // see GetMso5000AnalogBankUsage
	200 * 1000 * 1000  // see GetMso5000AnalogBankUsage
};

void RigolOscilloscope::UpdateDynamicCapabilities() {
	LogTrace("updating dynamic capabilities\n");
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	switch (m_series)
	{
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
		{// Mdepth depends on model (maxMemDepth) and number of enabled channels
			uint64_t maxMemDepth = m_maxMdepth / GetEnabledChannelCount();
			vector<uint64_t> depths;
			for (auto curMemDepth : dhoSampleDepths)
			{
				if (curMemDepth>maxMemDepth)
					break;
				if (curMemDepth == 5'000'000 and m_series != Series::DHO800)
					continue;
					// v00.01.03.00.04  2024/07/11
					// 3. DHO800 adds 5M storage depth.
				depths.push_back(curMemDepth);
			}
			m_depths = std::move(depths);
			return;
		}
		case Series::MSO5000:
		{
			// 1k|10k|100k|1M|10M|25M|50M|100M|200M
			// The maximum memory depth for the single channel is 200 M; the maximum memory
			// depth for the half-channel is 100 M; and the maximum memory depth for the
			// all-channel is 50 M.
			// -> remove N highest
			auto depths = mso5000SampleDepths;
			auto const bankUsage = GetMso5000AnalogBankUsage();
			if (bankUsage[0] == 2 or bankUsage[1] == 2)
			{
				depths.pop_back(); // 200M
				depths.pop_back(); // 100M
			}
			else if (not m_opt200M or (bankUsage[0] and bankUsage[1]))
				depths.pop_back(); // 200M
			m_depths = std::move(depths);
			return;
		}

		case Series::MSODS1000Z:
		{
			// For the analog channel:
			// ― 1 CH:   12000|120000|1200000|12000000|24000000
			// ― 2 CH:    6000| 60000| 600000| 6000000|12000000
			// ― 3/4 CH:  3000| 30000| 300000| 3000000| 6000000
			// -> 1 CH values appropriately divided
			auto divisor = GetMsods1000ZChannelDivisor();
			vector<std::uint64_t> depths;
			for (auto &depth : ds1000zSampleDepths)
			{
				if (depth == 24'000'000 and (not m_opt24M))
					continue;

				depths.emplace_back(depth/divisor);
			}
			m_depths = std::move(depths);
			m_mdepth.reset();
			m_srate.reset();
			return;
		}

		case Series::DS1000:
		case Series::UNKNOWN:
			break;
	}
}

std::size_t RigolOscilloscope::GetMsods1000ZChannelDivisor() {
	auto divisor = GetEnabledAnalogChannelCount();

	// MSO1000Z:
	// any active D0 ~  D7 (POD 1) disables CHAN3 and takes the resources
	// any active D8 ~ D15 (POD 2) disables CHAN4 and takes the resources
	for (auto d = 0U; d < 8; ++d)
	{
		if (IsChannelEnabled(m_analogChannelCount + d))
		{
			++divisor;
			break;
		}
	}
	for (auto d = 0U; d < 8; ++d)
	{
		if (IsChannelEnabled(m_analogChannelCount + d + 8))
		{
			++divisor;
			break;
		}
	}

	if (divisor <= 0)
		divisor = 1;
	else if (divisor >= 3)
		divisor = 4;
	return divisor;
}

// MSO5000 has it's own specific behavior of how enabled channels affect samplerate
// - (in this fcn) channel is considered enabled if it is enabled or if is trigger source
// - bank 0 are channels 1 dnd 2
// - bank 1 are channels 3 dnd 4
// if any bank has more than 1 channel enabled -> 4 and 8 Gsps rates are not available
// else if at one channel from each bank is enabled -> 8 Gsps are not available
// thus if e.g.: ch1 and ch2 are enabled (2 channels from single bank), only 2 Gsps max rate is available
// Sample applies for memory depth, but the highest depth is also only an option
std::array<std::size_t, 2> RigolOscilloscope::GetMso5000AnalogBankUsage() {
	std::array<std::size_t, 2> bankUsage {};

	for (auto idx = 0U; idx < m_analogChannelCount; ++idx)
	{
		bool enabled {};
		enabled |= IsChannelEnabled(idx);

		{
			auto trigger = GetTrigger();
			auto input = trigger->GetInput(0);
			enabled |= input.m_channel and input.m_channel->GetIndex() == idx;
		}
		if (not enabled)
			continue;
		switch (idx)
		{
			case 0:
			case 1:
				++bankUsage[0];
				break;
			case 2:
			case 3:
				++bankUsage[1];
				break;
			default:
				break;
		}
	}
	return bankUsage;
}

bool RigolOscilloscope::IsChannelAnalog(std::size_t i)
{
	return i < m_analogChannelCount;
}

bool RigolOscilloscope::IsChannelDigital(std::size_t i)
{
	return m_analogChannelCount <= i and i < m_analogChannelCount + m_digitalChannelCount;
}

std::optional<RigolOscilloscope::CapturePreamble> RigolOscilloscope::GetCapturePreamble() {
	//This is basically the same function as a LeCroy WAVEDESC, but much less detailed
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("WAV:PRE?"));
	// LogDebug("Preamble = %s\n", reply.c_str());

	CapturePreamble preamble {};
	int format;
	int type;

	auto parsed_length = sscanf(reply.c_str(),
		"%d,%d,%" PRIuLEAST32 ",%" PRIuLEAST32 ",%lf,%lf,%lf,%lf,%lf,%lf",
		// is there a way of getting rid of reinterpret_cast without sacrificing typed enums and without helper variables?
		&format,
		&type,
		&preamble.npoints,
		&preamble.averages,
		&preamble.sec_per_sample,
		&preamble.xorigin,
		&preamble.xreference,
		&preamble.yincrement,
		&preamble.yorigin,
		&preamble.yreference);

	if (parsed_length != 10) {
		LogError("Waveform data capture preamble parsing failed\n");
		return {};
	}

	// DHO QUIRK: models return page size instead of memory depth when paginating
	switch (m_series) {
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			preamble.npoints = GetSampleDepth();
			break;
		default:
			break;
	}

	// the other option was pointer reinterpret cast :/
	preamble.format = CaptureFormat(format);
	preamble.type = CaptureType(type);
	LogTrace("X: %" PRIuLEAST32 " points, %f origin, ref %f time/sample %lf\n", preamble.npoints, preamble.xorigin, preamble.xreference, preamble.sec_per_sample);
	LogTrace("Y: %f inc, %f origin, %f ref\n", preamble.yincrement, preamble.yorigin, preamble.yreference);
	return preamble;
}

string RigolOscilloscope::GetDriverNameInternal()
{
	return "rigol";
}

void RigolOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelAttenuations.clear();
	m_channelCouplings.clear();
	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelsEnabled.clear();
	m_channelBandwidthLimits.clear();
	m_bankThresholds.clear();

	m_srate.reset();
	m_mdepth.reset();
	m_triggerOffset.reset();
	m_laEnabled.reset();

	delete m_trigger;
	m_trigger = NULL;
}

std::vector<Oscilloscope::DigitalBank> RigolOscilloscope::GetDigitalBanks()
{
	return m_digitalBanks;
}

Oscilloscope::DigitalBank RigolOscilloscope::GetDigitalBank(std::size_t channel)
{
	for (auto &bank : m_digitalBanks)
		for (auto &bankChannel : bank)
			if (bankChannel->GetIndex() == channel)
				return bank;
	return {};
}

bool RigolOscilloscope::IsDigitalThresholdConfigurable()
{
	return m_digitalChannelCount > 0;
}

float RigolOscilloscope::GetDigitalThreshold(size_t channel)
{
	if (not IsChannelDigital(channel))
		return 0; //// should we rathger return NaN?

	auto const bankIdx = (channel - m_analogChannelCount) / DIGITAL_BANK_SIZE;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_bankThresholds.find(bankIdx) != m_bankThresholds.end())
			return m_bankThresholds[bankIdx];
	}
	
	// pods index values start at 1
	auto level = [&]() -> double {
		auto const level_str = m_transport->SendCommandQueuedWithReply(string(":LA:POD") + to_string(bankIdx + 1) + ":THR?");
		auto const level = parseDouble(level_str);
		if (level)
			return *level;
		LogError("could not parse channel %s threshold from %s\n", GetChannel(channel)->GetDisplayName().c_str(), level_str.c_str());
		return 0;
	}();

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_bankThresholds[bankIdx] = level;
	}

	return level;
}


void RigolOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	auto const bankIdx = (channel - m_analogChannelCount) / DIGITAL_BANK_SIZE;

	// pods index values start at 1
	m_transport->SendCommandQueued(string(":LA:POD") + to_string(bankIdx + 1) + ":THR " +  to_string(level));

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_bankThresholds[bankIdx] = level;
	}
}


size_t RigolOscilloscope::GetEnabledAnalogChannelCount()
{
	auto result = 0U;
	for(auto i = 0U; i < m_analogChannelCount; i++)
	{
		if(IsChannelEnabled(i))
			result++;
	}
	return result;
}

void RigolOscilloscope::LogLaNotPresent()
{
	LogError("RigolOscilloscope: LA is not present or not supported (yet) in this device (%s%d%s)", m_modelNew.prefix.c_str(), m_modelNew.number, m_modelNew.suffix.c_str());
}

bool RigolOscilloscope::IsLaEnabled() {
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_laEnabled.has_value())
			return *m_laEnabled;
	}

	bool enabled {};
	switch (m_series)
	{
		case Series::MSODS1000Z:
		case Series::MSO5000:
			enabled = Trim(m_transport->SendCommandQueuedWithReply(":LA:STAT?")) == "1";
			break;
		case Series::DHO900:
			enabled = Trim(m_transport->SendCommandQueuedWithReply(":LA:ENAB?")) == "1";
			break;
		case Series::DS1000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::UNKNOWN:
			LogLaNotPresent();
			return false;
	}
	
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_laEnabled = enabled;
	}
	return enabled;
}

void RigolOscilloscope::LaEnable()
{
	if (IsLaEnabled())
		return;

	switch (m_series)
	{
		case Series::MSODS1000Z:
		case Series::MSO5000:
			m_transport->SendCommandQueued(":LA:STAT ON");
			break;
		case Series::DHO900:
			m_transport->SendCommandQueued(":LA:ENAB ON");
			break;
		case Series::DS1000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::UNKNOWN:
			LogLaNotPresent();
			break;
	}
	
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		// we force the cached value refresh in case something caused our request to be ignored
		m_laEnabled.reset();
	}
}
void RigolOscilloscope::LaDisable()
{
	if (not IsLaEnabled())
		return;

	switch (m_series)
	{
		case Series::MSODS1000Z:
		case Series::MSO5000:
			m_transport->SendCommandQueued(":LA:STAT OFF");
			break;
		case Series::DHO900:
			m_transport->SendCommandQueued(":LA:ENAB OFF");
			break;
		case Series::DS1000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::UNKNOWN:
			LogLaNotPresent();
			break;
	}
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		// we force the cached value refresh in case something caused our request to be ignored
		m_laEnabled.reset();
	}
}

std::size_t RigolOscilloscope::IdxToAnalogChannelNumber(std::size_t i)
{
	return i;
}

std::size_t RigolOscilloscope::AnalogChannelNumberToIdx(std::size_t i)
{
	return i;
}

std::size_t RigolOscilloscope::IdxToDigitalChannelNumber(std::size_t i)
{
	return i - m_analogChannelCount;
}

std::size_t RigolOscilloscope::DigitalChannelNumberToIdx(std::size_t i)
{
	return i + m_analogChannelCount;
}

std::size_t RigolOscilloscope::IdxToDigitalBankIdx(std::size_t i)
{
	return (i - m_analogChannelCount) / DIGITAL_BANK_SIZE;
}


bool RigolOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(m_extTrigChannel and i == m_extTrigChannel->GetIndex())
		return false;

	if(m_aclTrigChannel and i == m_aclTrigChannel->GetIndex())
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	if (IsChannelAnalog(i))
	{
		auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":DISP?"));
		// FIXME: DS1000E response times out, not sure why, prog. manual lists this command
		// could be caused by some malformed command before
	
		{
			lock_guard<recursive_mutex> lock(m_cacheMutex);
			if(reply == "0")
			{
				m_channelsEnabled[i] = false;
				return false;
			}
			else
			{
				m_channelsEnabled[i] = true;
				return true;
			}
		}
	}
	else if (IsChannelDigital(i))
	{
		if (not IsLaEnabled())
			return false;
		
		bool enabled {};
		switch (m_series)
		{
			case Series::MSODS1000Z:
			case Series::MSO5000:
				enabled = Trim(m_transport->SendCommandQueuedWithReply(string(":LA:DISP? ") + m_channels[i]->GetHwname())) == "1";
				break;
			case Series::DHO900:
				enabled = Trim(m_transport->SendCommandQueuedWithReply(string(":LA:DIG:ENAB? ") + m_channels[i]->GetHwname())) == "1";
				break;
			case Series::DS1000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::UNKNOWN:
				LogLaNotPresent();
				return false;
		}
	
		{
			lock_guard<recursive_mutex> lock(m_cacheMutex);
			m_channelsEnabled[i] = enabled;
			return enabled;
		}
	}
	else
	{
		return false;
	}
	LogError("Channel id %zd is unknown", i);
	return false;
}

bool RigolOscilloscope::CanEnableChannel(size_t i)
{
	auto* channel = GetChannel(i);
	if (channel == nullptr)
		return false;

	switch (m_series)
	{
		case Series::MSODS1000Z:
			// CHAN3 collides with bank0 (D8 ~ D15)
			// CHAN4 collides with bank1 (D0 ~ D7), yeah, feels swapped, but that's the reality
			if (IsChannelAnalog(i))
			{
				if (m_digitalBanks.empty())
					return true;

				auto const analogChannelNumber = IdxToAnalogChannelNumber(i);
				switch (analogChannelNumber)
				{
					case 2: // attempting to enable CHAN3
						for (auto& dChannel : m_digitalBanks[1])
							if (IsChannelEnabled(dChannel->GetIndex()))
							{
								LogTrace("channel %zd (%s) can't be enabled, collision with (at least) channel %zd (%s) from bank %d\n",
									i, GetChannel(i)->GetDisplayName().c_str(),
									dChannel->GetIndex(), dChannel->GetDisplayName().c_str(),
									1U
								);
								return false;
							}
						break;
					case 3: // attempting to enable CHAN4
						for (auto& dChannel : m_digitalBanks[0])
							if (IsChannelEnabled(dChannel->GetIndex()))
							{
								LogTrace("channel %zd (%s) can't be enabled, collision with (at least) channel %zd (%s) from bank %d\n",
									i, GetChannel(i)->GetDisplayName().c_str(),
									dChannel->GetIndex(), dChannel->GetDisplayName().c_str(),
									0U
								);
								return false;
							}
						break;
					default: break;
				}
				return true;
			}

			if (IsChannelDigital(i)) {
				if (m_digitalBanks.empty())
					return true;

				auto const bankIdx = IdxToDigitalBankIdx(i);
				switch (bankIdx)
				{
					case 0: // bank 0
						if (IsChannelEnabled(AnalogChannelNumberToIdx(2))) // CHAN3
						{
							auto collidingCHannel = GetChannel(AnalogChannelNumberToIdx(2));
							LogTrace("channel %zd (%s) from bank %zd, can't be enabled, collision with analog channel %zd (%s)\n",
									i, GetChannel(i)->GetDisplayName().c_str(), bankIdx,
									collidingCHannel->GetIndex(), collidingCHannel->GetDisplayName().c_str()
							);
							return false;
						}
						break;
					case 1: // bank 1
						if (IsChannelEnabled(AnalogChannelNumberToIdx(3))) // CHAN4
						{
							auto collidingCHannel = GetChannel(AnalogChannelNumberToIdx(3));
							LogTrace("channel %zd (%s) from bank %zd, can't be enabled, collision with analog channel %zd (%s)\n",
									i, GetChannel(i)->GetDisplayName().c_str(), bankIdx,
									collidingCHannel->GetIndex(), collidingCHannel->GetDisplayName().c_str()
							);
							return false;
						}
						break;
					default : return false;
				}
				return true;
			}
			break;

		case Series::DS1000:
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			if (IsChannelAnalog(i)) return true;
			if (IsChannelDigital(i)) return true;
			break;
		case Series::UNKNOWN:
			break;
	}
	return false;
}

void RigolOscilloscope::EnableChannel(size_t i)
{
	if (not CanEnableChannel(i))
	{
		LogWarning("can't enable channel %zd\n", i);
		return;
	}
	//TODO: ignore request when cached value says enabled?
	if (IsChannelAnalog(i))
	{
		m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":DISP ON");
		switch (m_series)
		{
			// this is a quirk that changes to take effect immediately
			case Series::MSODS1000Z: // applies the configuration
			case Series::MSO5000: // ensures the srate readout reads correct value
				SetSampleDepth(GetSampleDepth());
				break;
				//TODO: check of other scopes require this too
			case Series::UNKNOWN:
			case Series::DS1000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::DHO900:
				break;
		}
	}
	else if (IsChannelDigital(i))
	{
		LaEnable();
		switch (m_series)
		{
			case Series::MSODS1000Z:
			case Series::MSO5000:
				m_transport->SendCommandQueued(":LA:DISP " + m_channels[i]->GetHwname() + ",ON");
				break;
			case Series::DHO900:
				m_transport->SendCommandQueued(":LA:DIG:ENAB " + m_channels[i]->GetHwname() + ",ON");
				break;
			case Series::DS1000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::UNKNOWN:
				LogLaNotPresent();
				return;
		}
	}
	else
	{
		LogWarning("Channel with idx %zd not known or does no support enable/disable control\n", i);
		return;
	}
	// invalidate channel enable cache until confirmed on next IsChannelEnabled
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled.erase(i);
		m_srate.reset();
	}
	UpdateDynamicCapabilities();
}

void RigolOscilloscope::DisableChannel(size_t i)
{
	//TODO: ignore request when cached value says enabled?
	if (IsChannelAnalog(i))
		m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":DISP OFF");
	else if (IsChannelDigital(i))
	{
		LaEnable();
		switch (m_series)
		{
			case Series::MSODS1000Z:
			case Series::MSO5000:
				m_transport->SendCommandQueued(":LA:DISP " + m_channels[i]->GetHwname() + ",OFF");
				break;
			case Series::DHO900:
				m_transport->SendCommandQueued(":LA:DIG:ENAB " + m_channels[i]->GetHwname() + ",OFF");
				break;
			case Series::DS1000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::UNKNOWN:
				LogLaNotPresent();
				return;
		}
		
	}
	else
	{
		LogError("Channel with idx %zd not known or does no support enable/disable control\n", i);
		return;
	}
	// invalidate channel enable cache until confirmed on next IsChannelEnabled
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled.erase(i);
		m_srate.reset();
	}
	UpdateDynamicCapabilities();
}

vector<OscilloscopeChannel::CouplingType> RigolOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> couplings;
	couplings.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	couplings.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	//TODO: some higher end models do have 50 ohm inputs... which ones?
	//ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	couplings.push_back(OscilloscopeChannel::COUPLE_GND);
	return couplings;
}

OscilloscopeChannel::CouplingType RigolOscilloscope::GetChannelCoupling(size_t i)
{
	if (not IsChannelAnalog(i))
		return OscilloscopeChannel::CouplingType::COUPLE_DC_1M; // TODO: what else should we return for DIGITAL non analog channels?

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":COUP?"));

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(reply == "AC")
			m_channelCouplings[i] = OscilloscopeChannel::COUPLE_AC_1M;
		else if(reply == "DC")
			m_channelCouplings[i] = OscilloscopeChannel::COUPLE_DC_1M;
		else /* if(reply == "GND") */
			m_channelCouplings[i] = OscilloscopeChannel::COUPLE_GND;
		return m_channelCouplings[i];
	}
}

void RigolOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if (not IsChannelAnalog(i))
	{
		LogError("Channel with idx %zd is not analog channel, only those suport coupling configuration", i);
		return;
	}

	bool valid = true;
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":COUP AC");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":COUP DC");
			break;

		case OscilloscopeChannel::COUPLE_GND:
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":COUP GND");
			break;

		default:
			LogError("Invalid coupling %d for channel %zd (%s)\n", type, i, GetChannel(i)->GetDisplayName().c_str());
			valid = false;
	}

	if(valid)
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelCouplings[i] = type;
	}
}

double RigolOscilloscope::GetChannelAttenuation(size_t i)
{
	if (not IsChannelAnalog(i))
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":PROB?"));

	double atten;
	sscanf(reply.c_str(), "%lf", &atten);
	//TODO: check sscanf return value for parsing errors

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
		return atten;
	}
}

void RigolOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if (not IsChannelAnalog(i))
	{
		LogError("Channel with idx %zd is not analog channel, only those suport attenuation configuration", i);
		return;
	}

	// TODO: refactor attenuation config to only check value presence in container and then use single command to push the value
	bool valid = true;
	//+ 0.1 in case atten is for example 0.049999 or so, to round it to 0.05 which turns to an int of 500
	switch((int)(atten * 10000 + 0.1))
	{
		case 1:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.0001");
			break;
		case 2:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.0002");
			break;
		case 5:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.0005");
			break;
		case 10:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.001");
			break;
		case 20:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.002");
			break;
		case 50:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.005");
			break;
		case 100:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.01");
			break;
		case 200:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.02");
			break;
		case 500:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.05");
			break;
		case 1000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.1");
			break;
		case 2000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.2");
			break;
		case 5000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 0.5");
			break;
		case 10000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 1");
			break;
		case 20000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 2");
			break;
		case 50000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 5");
			break;
		case 100000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 10");
			break;
		case 200000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 20");
			break;
		case 500000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 50");
			break;
		case 1000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 100");
			break;
		case 2000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 200");
			break;
		case 5000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 500");
			break;
		case 10000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 1000");
			break;
		case 20000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 2000");
			break;
		case 50000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 5000");
			break;
		case 100000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 10000");
			break;
		case 200000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 20000");
			break;
		case 500000000:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PROB 50000");
			break;
		default:
			LogError("Invalid attenuation %lf for channel %zd (%s)\n", atten, i, GetChannel(i)->GetDisplayName().c_str());
			valid = false;
	}

	if(valid)
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelAttenuations[i] = (int)(atten * 10000 + 0.1) * 0.0001;
	}
}

// Our requirements: has to be ordered, zero (full BW) position is not important
vector<unsigned int> RigolOscilloscope::GetChannelBandwidthLimiters(size_t i)
{
	if (not IsChannelAnalog(i)) // we assume, only analog channels support BW limmitters
		return {0};

	switch(m_series)
	{
		case Series::MSO5000:
			switch(m_bandwidth)
			{
				case 70:
				case 100:
					return {20, 0};
				case 200:
					return {20, 100, 0};
				case 350:
					return {20, 100, 200, 0};
				default:
					LogError("Invalid model bandwidth\n");
			}
			break;
		
		case Series::DHO800:
		case Series::DHO900:
		case Series::DHO1000:
		case Series::DHO4000:
			switch(m_bandwidth)
			{

				case 70:
				case 100:
				case 200:
					// TODO: 20 MHZ BW limit is forced when scale is below 200 uV/div (DHO4000 user manual)
					return {20, 0};
				case 400:
				case 800:
					// DHO4404/DHO480420 MHz, 250 MHz
					// TODO: 250 MHZ BW limit is forced when scale is below 500 uV/div (DHO4000 user manual)
					return {20, 250, 0};
				default:
					LogError("Invalid model bandwidth\n");
			}
			break;

		case Series::DS1000:
		case Series::MSODS1000Z:
			return {20, 0};
		
		case Series::UNKNOWN:
			LogError("RigolOscilloscope: unknown model, invalid state!\n");
			break;
	}
	return {};
}

unsigned int RigolOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if (not IsChannelAnalog(i))
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":BWL?"));

	unsigned int limit {};
	sscanf(reply.c_str(), "%uM", &limit);
	// parsing will fail when reply is `OFF` and result in default value 0, which means full BW

	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelBandwidthLimits[i] = limit;
	}
	LogTrace("Channel %zd, current BW limit: %u MHZ", i, limit);
	return limit;
}

void RigolOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	if (not IsChannelAnalog(i))
		return;

	auto const available_limits = GetChannelBandwidthLimiters(i);

	// `available_limits` is vector of increasing limits (with 0 at the end representing full BW)
	// Search for closest limit above `limit_mhz`, fall back to full rangeBW
	auto const new_limit = [&]() -> unsigned int {
		if (limit_mhz == 0)
			return 0;

		for (auto const limit : available_limits) // yeah, we also iterate over 0 (Full BW) at the end of the vector
		{
			if (limit_mhz <= limit)
				return limit;
		}
		// fallback to 0 which means full BW
		return 0;
	}();

	// `new_limit` now holds value of limit supported by the device or 0 in case of full BW

	if (new_limit > 0)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL " + to_string(new_limit) + "M");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL OFF");

	if (limit_mhz == 0)
		LogTrace("requested channel %zd, set no limit\n", i);
	else if (limit_mhz != new_limit)
		LogWarning("RigolOscilloscope: requested channel %zd, BW limit %d MHZ, set %d MHz\n", i, limit_mhz, new_limit);
	else
		LogTrace("requested channel %zd, set BW limit %d MHZ\n", i, new_limit);

	{
		//TODO: shouldn't we just rather invalidate cached value in `m_channelBandwidthLimits` and get a value by readback?
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelBandwidthLimits[i] = new_limit;
	}
}

float RigolOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{	
	// TODO: what should be the range of digital channel?
	if (not IsChannelAnalog(i))
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	string reply;
	switch (m_series) {
		case Series::MSODS1000Z:
			reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":RANGE?"));
			break;

		// we can probably use `default` here, as all modern (sane?) Rigol scopes use SCALE command
		case Series::DS1000:
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":SCALE?"));
			break;

		case Series::UNKNOWN:
			return {};
	}

	float range;
	sscanf(reply.c_str(), "%f", &range);
	//TODO: check sscanf return value for parsing errors
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		switch (m_series) {
			case Series::MSODS1000Z:
				return m_channelVoltageRanges[i] = range;

			case Series::DS1000:
				return m_channelVoltageRanges[i] = 10 * range;

			case Series::MSO5000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::DHO900:
				return m_channelVoltageRanges[i] = 8 * range;
				
			case Series::UNKNOWN:
				break;
		}
	}
	return {};
}

void RigolOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	// only analog channels have adjustable offset
	if (not IsChannelAnalog(i))
		return;

	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
	}

	switch (m_series) {
			case Series::MSODS1000Z:
				m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":RANGE " + to_string(range));
				return;
			
			// we can probably use `default` here, as all modern (sane?) Rigol scopes use SCALE command
			case Series::DS1000:
			case Series::MSO5000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::DHO900:
				m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":SCALE " + to_string(range / 8));
				return;

			case Series::UNKNOWN:
				return;
		}
}

OscilloscopeChannel* RigolOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

float RigolOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	if (not IsChannelAnalog(i))
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":OFFS?"));

	float offset;
	sscanf(reply.c_str(), "%f", &offset);
	//TODO: check sscanf return value for parsing errors

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
	}
	return offset;
}

void RigolOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	if (not IsChannelAnalog(i))
		return;

	m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":OFFS " + to_string(offset));

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
	}
}

Oscilloscope::TriggerMode RigolOscilloscope::PollTrigger()
{
	if(m_liveMode)
		return TRIGGER_MODE_TRIGGERED;

	LogTrace("m_triggerArmed %d, m_triggerWasLive %d\n", m_triggerArmed, m_triggerWasLive);

	if (m_series == Series::MSODS1000Z) {
		// DS1000Z report trigger status in unreliable way.
		// When triggered, it reports STOP for some time.
		// Then it goes though RUN->WAIT->TRIG and ends up in STOP,
		// but sometimes it completely skips transition to other states (not quick enough poll) and
		// remains in STOP mode for a whole time even though there are new data.
		// As a workaround, we monitor output WAV:PREamble which also report sample count.
		// Once it matches configured memory depth, we consider capture to be complete.
		// This is also much faster than waiting for trigger states.

		// TODO: check if other series could use this method
		// TODO: consider to invalidate cached depth on trigger command
		if (m_triggerArmed)
		{
			const auto sampleDepth = GetSampleDepth();
			const auto preamble = GetCapturePreamble();
			if (sampleDepth and preamble.has_value())
			{
				if (preamble->npoints == sampleDepth) {
					// reached the target sample count
					m_triggerArmed = false;
					m_triggerWasLive = false;
					return TRIGGER_MODE_TRIGGERED;
				}
	
				if (m_triggerWasLive) // was live and sampling not finished
					return TRIGGER_MODE_RUN;
	
				// was not live and no points were sampled yet or stale value from last acq
				// acq have not started yet
				if (preamble->npoints == 0 or preamble->npoints == m_pointsWhenStarted)
					return TRIGGER_MODE_WAIT;
	
				// wa not live, but something was sampled -> switch to live
				m_triggerWasLive = true;
				return TRIGGER_MODE_RUN;
			}
			else
			{
				if (not sampleDepth)
					LogError("failed to get sample depth\n");
				else if (not preamble)
					LogError("failed to get preamble\n");
			}
		}
		else
		{
			if (m_triggerWasLive)
			{
				// after manually stopped acquisition
				LogTrace("Last poll after manually stopped partial acquisition\n");
				m_triggerWasLive = false;
				// return TRIGGER_MODE_
				// TRIGGERED; // returning triggered could result in (partial) data download
				return TRIGGER_MODE_STOP;
			}
			return TRIGGER_MODE_STOP;
		}
	}

	auto stat = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:STAT?"));
	
	if(stat != "STOP")
		m_triggerWasLive = true;

	if(stat == "TD")
	{
		if (m_series == Series::MSO5000)
			// MSO5000Z reports triggered right at the triggering moment,
			// but that does not imply data availability (returning TRIGGER_MODE_TRIGGERED does).
			// So we return TRIGGER_MODE_RUN to not indicate data availability
			// and rely on the `STOP` to arrive afterwards.
			// This can be easily caused by long sampling times (e.g.: 10 ksps and 100k mem. depth)
			return TRIGGER_MODE_RUN;
		else
			// TODO: check if other families trigger TD state is also affected or not
			return TRIGGER_MODE_TRIGGERED;
	}
	else if(stat == "RUN")
		return TRIGGER_MODE_RUN;
	else if(stat == "WAIT")
		return TRIGGER_MODE_WAIT;
	else if(stat == "AUTO")
		return TRIGGER_MODE_AUTO;
	else
	{
		//The "TD" state is not sticky on Rigol scopes, unlike the equivalent LeCroy status register bit.
		//The scope will go from "run" to "stop" state on trigger with only a momentary pass through "TD".
		//If we armed the trigger recently and we're now stopped, this means we must have triggered.
		// DS1000 QUIRK

		// It also takes sime time before the scope transitiona _from_ STOP to any other state
		if(m_triggerArmed && (m_series != Series::DS1000 || m_triggerWasLive))
		{
			m_triggerArmed = false;
			m_triggerWasLive = false;
			return TRIGGER_MODE_TRIGGERED;
		}

		//Nope, we're actually stopped
		return TRIGGER_MODE_STOP;
	}
}

uint64_t RigolOscilloscope::GetPendingWaveformBlockLength()
{
	unsigned char header_size;
	{
		array<char, 3> header_size_raw {};
		m_transport->ReadRawData(2, reinterpret_cast<unsigned char*>(header_size_raw.data()));
		//LogWarning("Time %f\n", (GetTime() - start));

		if (sscanf(header_size_raw.data(), "#%c", &header_size) != 1)
			return 0;

		header_size = header_size - '0';

		if(header_size > 12)
		{
			header_size = 12;
		}
		
	}
	
	array<char,12> header {0};
	m_transport->ReadRawData(header_size, reinterpret_cast<unsigned char*>(header.data()));

	//Look up the block size
	//size_t blocksize = end - npoints;
	//LogDebug("Block size = %zu\n", blocksize);
	// Block size is provided in bytes, not in points
	uint64_t header_blocksize_bytes;
	sscanf(header.data(), "%" PRIu64, &header_blocksize_bytes);
	LogTrace("Parsed waveform block length %" PRIu64 "\n", header_blocksize_bytes);

	return header_blocksize_bytes;
}

bool RigolOscilloscope::AcquireData()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	LogIndenter li;

	LogTrace("Acquiring data\n");

	// Notify about download operation start
	ChannelsDownloadStarted();

	m_triggerWasLive = false; // premature stop may have resulted in `m_triggerWasLive` staying true (did not reach triggered state)

	// Rigol scopes do not have a capture time so we fake it
	double now = GetTime();

	//Grab the analog waveform data
	size_t npoints {};
	double sec_per_sample {};
	double yincrement {};
	double yorigin {};
	double yreference {};
	size_t maxpoints {};
	switch (m_series) {
		case Series::DS1000:
			maxpoints = 8192; // FIXME
			break;
		case Series::MSODS1000Z:
			// manual specifies 250k as a maximum for bytes output
			// maxpoints = 250 * 1000;

			// During experiments with my DS1054Z FW 00.04.05.SP2,
			// 250kB limits applies when all channels are enabled.
			// It is possible to use larger chunks with less channels.
			// With single channel and 1 MB block, around ~20% speed-up is observable.
			maxpoints = 1000 * 1000 / GetMsods1000ZChannelDivisor();
			break;
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			// keeping at 0, so it is updated to maximum from the value in preamble/memory depth
			break;
		case Series::UNKNOWN:
			return false;
	}
	vector<unsigned char> temp_buf;
	map<int, deque<WaveformBase*>> pending_waveforms;

	auto ts_download_start = chrono::steady_clock::now();

	LogTrace("Downloading analog waveforms\n");
	for(auto channelIdx = 0U; channelIdx < m_analogChannelCount; channelIdx++)
	{
		if(!IsChannelEnabled(channelIdx))
		{
			// ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_NONE, 0);

			continue;
		}

		//LogDebug("Channel %zu\n", i);

		int64_t fs_per_sample = 0;

		// DS10000 QUIRK
		switch (m_series) {
			case Series::DS1000:
			{
				yreference = 0;
				npoints = maxpoints;
		
				yincrement = GetChannelVoltageRange(channelIdx, 0) / 256.0f;
				yorigin = GetChannelOffset(channelIdx, 0);
		
				auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[channelIdx]->GetHwname() + ":OFFS?"));
				sscanf(reply.c_str(), "%lf", &yorigin);
				//TODO: check sscanf return value for parsing errors
		
				/* for these scopes, this is seconds per div */
				reply = Trim(m_transport->SendCommandQueuedWithReply(":TIM:SCAL?"));
				sscanf(reply.c_str(), "%lf", &sec_per_sample);
				//TODO: check sscanf return value for parsing errors
				fs_per_sample = (sec_per_sample * 12 * FS_PER_SECOND) / npoints;
				break;
			}

			case Series::MSODS1000Z:
			case Series::MSO5000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::DHO900:
			{
				m_transport->SendCommandQueued(string("WAV:SOUR ") + m_channels[channelIdx]->GetHwname());
				
				auto preamble = GetCapturePreamble();
				if (not preamble.has_value()) {
					continue;
				}

				if (maxpoints == 0)
					maxpoints = preamble->npoints;

				if(preamble->sec_per_sample == 0)
				{	// Sometimes the scope might return a null value for xincrement => ignore waveform to prenvent an Arithmetic exception in WaveformArea::RasterizeAnalogOrDigitalWaveform 
					LogWarning("Got null sec_per_sample value from the scope, ignoring this waveform.\n");
					continue;
				}
				fs_per_sample = round(preamble->sec_per_sample * FS_PER_SECOND);

				npoints = preamble->npoints;
				sec_per_sample = preamble->sec_per_sample;
				// xorigin = preamble->xorigin;
				// xreference = preamble->xreference;
				yincrement = preamble->yincrement;
				yorigin = preamble->yorigin;
				yreference = preamble->yreference;
				//LogDebug("X: %d points, %f origin, ref %f fs/sample %ld\n", (int) npoints, xorigin, xreference, (long int) fs_per_sample);
				//LogDebug("Y: %f inc, %f origin, %f ref\n", yincrement, yorigin, yreference);
				break;
			}

			case Series::UNKNOWN:
				return false;
		}

		if (maxpoints == 0)
		{
			LogError("was not updated, this a bug! Ignoring waveform\n");
			continue;
		}

		temp_buf.resize((m_highDefinition ? (maxpoints * 2) : maxpoints) + 1);

		//If we have zero points in the reply, skip reading data from this channel
		if(npoints == 0)
			continue;

		LogTrace("Channel %u(%s) samplerate %s\n", channelIdx, GetChannel(channelIdx)->GetDisplayName().c_str(), Unit(Unit::UNIT_SAMPLERATE).PrettyPrint(FS_PER_SECOND/fs_per_sample).c_str());

		//Set up the capture we're going to store our data into
		auto cap {AllocateAnalogWaveform(m_nickname + "." + GetChannel(channelIdx)->GetHwname())};
		cap->clear();
		cap->Reserve(npoints);
		cap->m_timescale = fs_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = floor(now);
		cap->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;

		// when waveforms is not transferred to pending_waveforms, move it back to the pool to prevent memory leak
		auto waveformCleanup = CallOnEOC([&](){
			if (cap)
			{
				AddWaveformToAnalogPool(cap);
				cap = nullptr;
			}
		});

		ChannelsDownloadStatusUpdate(channelIdx, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, 0.0);

		// Downloading the waveform is a pain in the butt, because most series can download only limitted amount of points at a time! (Unless you have a MSO5)
		auto ts_channel_download_start = chrono::steady_clock::now();
		for(size_t npoint = 0; npoint < npoints;)
		{

			switch (m_series) {
				case Series::DS1000:
					m_transport->SendCommandQueued(string(":WAV:DATA? ") + m_channels[channelIdx]->GetHwname());
					break;
					
				case Series::MSODS1000Z:
				case Series::MSO5000:
				case Series::DHO1000:
				case Series::DHO4000:
				case Series::DHO800:
				case Series::DHO900:
					m_transport->SendCommandQueued("*WAI");
					m_transport->SendCommandQueued(string("WAV:STAR ") + to_string(npoint+1));	//ONE based indexing WTF
					m_transport->SendCommandQueued(string("WAV:STOP ") + to_string(min(npoint + maxpoints, npoints)));
					m_transport->SendCommandQueued("WAV:DATA?");
					break;

				case Series::UNKNOWN:
					LogError("RigolOscilloscope: unknown model, invalid state!\n");
					return false;
			}

			m_transport->FlushCommandQueue();

			//Read block header, should be maximally 11 long on MSO5 scope with >= 100 MPoints
			
			auto header_blocksize_bytes = GetPendingWaveformBlockLength();

			if(header_blocksize_bytes == 0)
			{
				LogWarning("Ran out of data after %zu points\n", npoint);
				unsigned char sink;
				m_transport->ReadRawData(1, &sink); //discard the trailing newline
				break;
			}

			auto header_blocksize = header_blocksize_bytes;
			if(m_highDefinition)
				header_blocksize = header_blocksize_bytes/2;
			if(header_blocksize > maxpoints)
			{
				header_blocksize = maxpoints;
			}

			//Read actual block content and decode it
			//Scale: (value - Yorigin - Yref) * Yinc

			size_t bytesToRead = header_blocksize_bytes + 1; //trailing newline after data block
			auto downloadCallback = [channelIdx, this, npoint, npoints, bytesToRead] (float progress) {
				/* we get the percentage of this particular download; convert this into linear percentage across all chunks */
				float bytes_progress = npoint * (m_highDefinition ? 2 : 1) + progress * bytesToRead;
				float bytes_total = npoints * (m_highDefinition ? 2 : 1);
				// LogTrace("download progress %5.3f\n", bytes_progress / bytes_total);
				ChannelsDownloadStatusUpdate(channelIdx, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, bytes_progress / bytes_total);
			};
			auto const bytesRead = m_transport->ReadRawData(bytesToRead, temp_buf.data(), downloadCallback);
			downloadCallback(1);
			// in case the transport did not call the progress callback (e.g. ScpiLxi), call it manually al least once after the transport finishes

			if (bytesRead != bytesToRead)
				LogWarning("requested %zd , got %zd B\n", bytesToRead, bytesRead);


			double ydelta = yorigin + yreference;
			cap->Resize(cap->size() + header_blocksize);
			cap->PrepareForCpuAccess();

			for(size_t j = 0; j < header_blocksize; j++)
			{	// Handle 8bit / 16bit acquisition modes
				auto &sample = cap->m_samples[npoint + j];
				uint16_t raw_sample {};
				if(m_highDefinition)
					memcpy(&raw_sample, temp_buf.data() + (j * 2), sizeof(raw_sample));
				else
					raw_sample = temp_buf[j];

				
				switch(m_series)
				{
					case Series::DS1000:
						sample = (128 - static_cast<float>(raw_sample)) * yincrement - ydelta;
						break;
					
					default:
						sample = (static_cast<float>(raw_sample) - ydelta) * yincrement;
						// DS/MSO1000Z manual says (0x8E - YORigin - YREFerence) x YINCrement
				}
				//LogDebug("V = %.3f, raw=%d, delta=%f, inc=%f\n", v, raw_sample, ydelta, yincrement);
			}
			cap->MarkSamplesModifiedFromCpu();

			npoint += header_blocksize;
		}

		auto ts_channel_download_end = chrono::steady_clock::now();
		LogTrace("Channel %s download took %ld ms\n", GetChannel(channelIdx)->GetDisplayName().c_str(), chrono::duration_cast<chrono::milliseconds>(ts_channel_download_end - ts_channel_download_start).count());

		// Notify about end of download for this channel
		ChannelsDownloadStatusUpdate(channelIdx, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);

		pending_waveforms[channelIdx].push_back(cap);
		cap = nullptr; // we have to clean the ptr, otherwise the waveformCleanup will move it back to the pool
	}

	// download digital channel data (if any)
	LogTrace("Downloading digital waveforms\n");
	
	// put active channels into their pods, makes the extration easier
	struct DigitalChannelCapture {
		InstrumentChannel* metadata;
		SparseDigitalWaveform* capture;

		DigitalChannelCapture(InstrumentChannel* channel_, SparseDigitalWaveform *capture_) :
			metadata(channel_),
			capture{capture_} {}
		};
	map<size_t, vector<DigitalChannelCapture>> banksToDownload;
	
	for(auto channelIdx = 0U; channelIdx < m_digitalChannelCount; channelIdx++)
	{
		if(!IsChannelEnabled(DigitalChannelNumberToIdx(channelIdx)))
			continue;
		auto const bankIdx = channelIdx / DIGITAL_BANK_SIZE;
		auto capture {AllocateDigitalWaveform(m_nickname + "." + GetChannel(channelIdx)->GetHwname())};
		capture->clear();
		// capture->Reserve(npoints); // as we don't know the necessary size, the ubffer will grow on demand (still in blocks)
		// capture->m_timescale = // will be udapted later
		capture->m_triggerPhase = 0;
		capture->m_startTimestamp = floor(now);
		capture->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;

		if (auto bank = banksToDownload.find(bankIdx); bank != banksToDownload.end())
			bank->second.emplace_back(GetChannel(DigitalChannelNumberToIdx(channelIdx)), capture);
		else
			banksToDownload[bankIdx] = {{GetChannel(DigitalChannelNumberToIdx(channelIdx)), capture}};
	}

	// download data for each bank

	m_srate.reset();
	int64_t fs_per_sample = FS_PER_SECOND / GetSampleRate(); // MSO1000Z returns invalid increment value for digital channels, so we take it from here
	for(auto bank = banksToDownload.begin(); bank != banksToDownload.end(); ++bank)
	{
		auto const bankIdx = bank->first;
		auto & bankChannels = bank->second;

		// when some waveforms are not transferred to pending_waveforms, move them back to the pool to prevent memory leak
		auto waveformCleanup = CallOnEOC([&](){
			for (auto& channel : bankChannels)
				if (channel.capture)
				{
					AddWaveformToAnalogPool(channel.capture);
					channel.capture = nullptr;
				}
		});

		struct ReleaseWaveforms {
			vector<DigitalChannelCapture> &bankChannels;
			~ReleaseWaveforms() {
				
			}
		};

		// we set the source to the lowest channel in the POD(bank), could be any
		m_transport->SendCommandQueued(string("WAV:SOUR ") + GetChannel(DigitalChannelNumberToIdx(bankIdx * DIGITAL_BANK_SIZE))->GetHwname()); 

		auto preamble = GetCapturePreamble();
		if (not preamble.has_value())
			continue;

		npoints = preamble->npoints;
		//If we have zero points in the reply, skip reading data from this bank
		if(npoints == 0)
			continue;

		switch (m_series) {
			case Series::MSODS1000Z:
			 	// MSO1000Z return weirdly scaled xincrement values for digital channels (25 or 50 times higher)
				break;
			case Series::MSO5000:
			case Series::DHO900:
			{
				//TODO: check that these series respond with meaningfule data in digital chanel preamble
				if(preamble->sec_per_sample == 0)
					break; // Sometimes the scope might return a null value for xincrement => do not update `fs_per_sample`
				
				fs_per_sample = preamble->sec_per_sample * FS_PER_SECOND; // prefer value from preamble rather than one obtained from GetSamplerate()
				break;
			}
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::DS1000:
			case Series::UNKNOWN:
				LogLaNotPresent();
				return false;
		}

		if (fs_per_sample == 0)
		{
			LogWarning("Invalid fs_per_sample, ignoring waveform.\n");
			continue;
		}

		LogTrace("Bank %zd samplerate %s\n", bankIdx, Unit(Unit::UNIT_SAMPLERATE).PrettyPrint(FS_PER_SECOND/fs_per_sample).c_str());

		for (auto& channel : bankChannels)
		{
			channel.capture->m_timescale = fs_per_sample;
			LogTrace("%s m_timescale %" PRIi64 "\n", channel.metadata->GetDisplayName().c_str(), channel.capture->m_timescale);
			ChannelsDownloadStatusUpdate(channel.metadata->GetIndex(), InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, 0.0);
		}

		// Downloading the waveform is a pain in the butt, because most series can download only limitted amount of points at a time! (Unless you have a MSO5)
		auto ts_bank_download_start = chrono::steady_clock::now();
		for(size_t npoint = 0; npoint < npoints;)
		{
			switch (m_series) {
				break;
				
				case Series::MSODS1000Z:
				case Series::MSO5000:
				case Series::DHO900:
					m_transport->SendCommandQueued("*WAI");
					m_transport->SendCommandQueued(string("WAV:STAR ") + to_string(npoint+1));
					m_transport->SendCommandQueued(string("WAV:STOP ") + to_string(min(npoint + maxpoints, npoints)));
					m_transport->SendCommandQueued("WAV:DATA?");
				break;
				
				case Series::DHO1000:
				case Series::DHO4000:
				case Series::DHO800:
				case Series::DS1000:
				case Series::UNKNOWN:
					LogError("RigolOscilloscope: unknown model, invalid state!\n");
					return false;
			}

			m_transport->FlushCommandQueue();

			//Read block header, should be maximally 11 long on MSO5 scope with >= 100 MPoints
			
			auto header_blocksize_bytes = GetPendingWaveformBlockLength();

			if(header_blocksize_bytes == 0)
			{
				LogWarning("Ran out of data after %zu points\n", npoint);
				unsigned char sink;
				m_transport->ReadRawData(1, &sink); //discard the trailing newline

				// If this happened after zero samples, free the waveform so it doesn't leak
				if(npoint == 0)
					for (auto& channel : bankChannels)
					{
						AddWaveformToAnalogPool(channel.capture);
						channel.capture = nullptr;
					}
				break;
			}

			auto header_blocksize = header_blocksize_bytes;

			size_t bytesToRead = header_blocksize_bytes + 1; //trailing newline after data block
			auto downloadCallback = [&bankChannels, this, npoint, npoints, bytesToRead] (float progress) {
				/* we get the percentage of this particular download; convert this into linear percentage across all chunks */
				// TODO: check taht DHO9000 also packs groups of 8 digital channels (pods) into bytes
				float bytes_progress = npoint + progress * bytesToRead;
				float bytes_total = npoints;
				// LogTrace("download progress %5.3f\n", bytes_progress / bytes_total);
				// Update all active channels in this bank
				auto totalProgress = bytes_progress / bytes_total;
				for (auto const& channel : bankChannels)
					ChannelsDownloadStatusUpdate(channel.metadata->GetIndex(), InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, totalProgress);
			};
			m_transport->ReadRawData(bytesToRead, temp_buf.data(), downloadCallback);
			downloadCallback(1);
			// in case the transport did not call the progress callback (e.g. ScpiLxi), call it manually al least once after the transport finishes

			// extract individual channels from the downloaded data
			// raw data are samples with constant samplerate, but we use Sparse waveform for digital channels (to save resources)
			// thus while extzracting the data, we only record level changes
			for (auto const& channel : bankChannels)
			{
				auto& cap = channel.capture;
				auto samplesStored = cap->size();
				cap->Resize(samplesStored + header_blocksize); // in case we have alternating 010101... sequence, otehrwise we will use less samples and shrink afterwards
				cap->PrepareForCpuAccess();

				uint8_t const mask = 1 << (IdxToDigitalChannelNumber(channel.metadata->GetIndex()) % DIGITAL_BANK_SIZE);

				if (samplesStored == 0) {
					// not a single sample in the capture buffer yet
					// we push the first one unconditionally, and we need to have there at leqst one for following deduplication process
					cap->m_samples[samplesStored] = temp_buf[0] & mask;
					cap->m_durations[samplesStored] = 1;
					cap->m_offsets[samplesStored] = 0;
					++samplesStored;
				}

				auto lastValue = cap->m_samples[samplesStored - 1];
				auto *lastDuration = &cap->m_durations[samplesStored - 1];
	
				for(size_t j = 1U; j < header_blocksize; j++)
				{
					bool currentValue = temp_buf[j] & mask;
					if (currentValue == lastValue)
						++(*lastDuration); // bit values stayed the same
					else
					{	// bit value changed, create new samples with current value
						cap->m_samples[samplesStored] = currentValue;
						// lastOffset = cap->m_offsets[samplesStored] = lastOffset + *lastDuration;
						cap->m_offsets[samplesStored] = npoint + j;
						cap->m_durations[samplesStored] = 1;
						lastDuration = &cap->m_durations[samplesStored];
						lastValue = currentValue;
						++samplesStored;
					}
	
				}
				cap->Resize(samplesStored);
				cap->MarkSamplesModifiedFromCpu();
			}

			npoint += header_blocksize;
		}

		auto ts_bank_download_end = chrono::steady_clock::now();
		LogTrace("Digital bank %zd download took %ld ms\n", bankIdx, chrono::duration_cast<chrono::milliseconds>(ts_bank_download_end - ts_bank_download_start).count());

		// download finished
		for (auto& channel : bankChannels)
		{
			ChannelsDownloadStatusUpdate(channel.metadata->GetIndex(), InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
			{	// workaround for https://github.com/ngscopeclient/scopehal-apps/issues/919
				auto samples = channel.capture->size();
				// add out dummy sample
				channel.capture->Resize(++samples);
				channel.capture->m_samples[samples - 1] = channel.capture->m_samples[samples - 2];
				channel.capture->m_offsets[samples - 1] = channel.capture->m_offsets[samples - 2] + channel.capture->m_durations[samples - 2];
				channel.capture->m_durations[samples - 1] = 0;
			}
			channel.capture->m_samples.shrink_to_fit();
			channel.capture->m_offsets.shrink_to_fit();
			channel.capture->m_durations.shrink_to_fit();
			channel.capture->MarkModifiedFromCpu();
			pending_waveforms[channel.metadata->GetIndex()].push_back(channel.capture);
			channel.capture = nullptr; // we have to clean the ptr, otherwise the waveformCleanup will move it back to the pool
		}
	}

	auto ts_download_end = chrono::steady_clock::now();
	LogTrace("Whole download took %ld ms\n", chrono::duration_cast<chrono::milliseconds>(ts_download_end - ts_download_start).count());

	//Now that we have all of the pending waveforms, save them in sets across all channels
	{
		lock_guard<mutex> lock2(m_pendingWaveformsMutex);
		size_t num_pending = 1;	   //TODO: segmented capture support
		for(size_t i = 0; i < num_pending; i++)
		{
			SequenceSet s;
			// move all captures to the set
			for(size_t j = 0; j < m_analogChannelCount + m_digitalChannelCount; j++)
			{
				if(auto waveform = pending_waveforms.find(j); waveform != pending_waveforms.end())
				{
					s[GetOscilloscopeChannel(j)] = waveform->second.front();
					waveform->second.pop_front();
					if (waveform->second.empty())
						pending_waveforms.erase(waveform);
				}
			}
			m_pendingWaveforms.push_back(s);
		}
		if (not pending_waveforms.empty())
			LogError("pending_waveforms resource management errorl, pending_waveforms should have been amepty now!\n");
	}

	//Clean up
	temp_buf = {};

	// Tell the download monitor that waveform download has finished
	ChannelsDownloadFinished();

	//TODO: support digital channels

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		switch (m_series)
		{
			case Series::DS1000:
				m_transport->SendCommandQueued(":STOP");
				m_transport->SendCommandQueued(":TRIG:EDGE:SWE SING");
				m_transport->SendCommandQueued(":RUN");
				break;

			case Series::MSODS1000Z:
			case Series::MSO5000:
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::DHO900:
				if(!m_liveMode)
				{
					m_transport->SendCommandQueued(":SING");
					m_transport->SendCommandQueued("*WAI");
				}
				break;

			case Series::UNKNOWN:
				return false;
		}
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");

	return true;
}

void RigolOscilloscope::StartPre() 
{
	m_liveMode = false;
	m_mdepth.reset(); // Memory depth might have been changed on scope
	switch (m_series)
	{
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			// DHO models need to set raw mode off and on again or vice versa to reset the number of points according to the current memory depth
			if(m_liveMode)
			{
				m_transport->SendCommandQueued(":WAV:MODE RAW");
				m_transport->SendCommandQueued(":WAV:MODE NORM");
			}
			else
			{
				m_transport->SendCommandQueued(":WAV:MODE NORM");
				m_transport->SendCommandQueued(":WAV:MODE RAW");
			}
			break;

		case Series::DS1000:
		case Series::MSODS1000Z:
		case Series::MSO5000:
		case Series::UNKNOWN:
			break;
	}
}

void RigolOscilloscope::StartPost() 
{
	m_triggerArmed = true;

	{
		auto preamble = GetCapturePreamble();
		if (preamble.has_value())
		{
			m_pointsWhenStarted = preamble->npoints;
			LogTrace("set m_pointsWhenStarted to %" PRIuLEAST32 "\n", m_pointsWhenStarted);
		}
		else
			LogError("empty preable\n");
	}
}


void RigolOscilloscope::Start()
{
	//TODO: consider locking transport as it should not get interrupted by something else (or can it?)
	LogTrace("Start\n");
	StartPre();
	m_triggerOneShot = false;
	switch (m_series)
	{
		case Series::DS1000:
			m_transport->SendCommandQueued(":TRIG:EDGE:SWE SING");
			m_transport->SendCommandQueued(":RUN");
			break;

		case Series::MSODS1000Z:
		case Series::MSO5000:
			m_transport->SendCommandQueued(":SING");
			m_transport->SendCommandQueued("*WAI");
			break;

		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			// Check for memory depth : if it is 1k, switch to live mode for better performance
			// Limit live mode to one channel setup to prevent grabbing waveforms from to different triggers on seperate channels
			if(GetEnabledChannelCount()==1)
			{
				m_mdepth.reset();
				GetSampleDepth();
				m_liveMode = (m_mdepth == 1000);
			}
			else
			{
				m_liveMode = false;
			}
			m_transport->SendCommandQueued(m_liveMode ? ":RUN" : ":SING");
			m_transport->SendCommandQueued("*WAI");
			break;

		case Series::UNKNOWN:
			break;
	}
	StartPost();
}

void RigolOscilloscope::StartSingleTrigger()
{
	LogTrace("Start single trigger\n");
	
	StartPre();
	m_triggerOneShot = true;
	switch (m_series)
	{
		case Series::DS1000:
			m_transport->SendCommandQueued(":TRIG:EDGE:SWE SING");
			m_transport->SendCommandQueued(":RUN");
			break;

		case Series::MSODS1000Z:
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			m_transport->SendCommandQueued(":SING");
			m_transport->SendCommandQueued("*WAI");
			break;

		case Series::UNKNOWN:
			break;
	}
	StartPost();
}

void RigolOscilloscope::Stop()
{
	LogTrace("Explicit STOP requested");
	m_transport->SendCommandQueued(":STOP");
	m_liveMode = false;
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

void RigolOscilloscope::ForceTrigger()
{
	m_liveMode = false;
	m_mdepth.reset(); // Memory depth might have been changed on scope
	m_triggerOneShot = true;
	StartPre();
	switch (m_series)
	{
		case Series::MSODS1000Z:
		case Series::MSO5000:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			m_transport->SendCommandQueued(":TFOR");
			break;

		case Series::DS1000:
		case Series::UNKNOWN:
			LogError("RigolOscilloscope::ForceTrigger not implemented for this model\n");
			break;
	}
	StartPost();
}

bool RigolOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

static std::map<uint64_t, double> dhoLowSampleRates {
	// Available sample rates for DHO 800/900 models, regardless of activated channels
	// Map each sample rate to its horizontal scale ratio (this ratio changes based on srate on DHO models)
	{                200, 10},
	{                500, 10},
	{               1000, 10},
	{               2000, 10},
	{               5000, 10},
	{          10 * 1000, 10},
	{          20 * 1000, 10},
	{          50 * 1000, 10},
	{         100 * 1000, 10},
	{         125 * 1000, 16},
	{         250 * 1000, 20},
	{         500 * 1000, 20},
	{        1250 * 1000, 16},
	{        2500 * 1000, 20},
	{        6250 * 1000, 16},
	{       12500 * 1000, 16},
	{       31250 * 1000, 16},
	{       62500 * 1000, 16},
	{      156250 * 1000, 12.8},
	{      312500 * 1000, 16},
	{      625000 * 1000, 16},
	{ 1250 * 1000 * 1000, 16},
};

static std::map<uint64_t, double> dhoHighSampleRates {
	// Available sample rates for DHO 1000/4000 models, regardless of activated channels
	// Map each sample rate to its horizontal scale ratio (this ratio changes based on srate on DHO models)
	{                    100, 10},
	{                    200, 10},
	{                    500, 10},
	{                   1000, 10},
	{                   2000, 10},
	{                   5000, 10},
	{              10 * 1000, 10},
	{              20 * 1000, 10},
	{              50 * 1000, 10},
	{             100 * 1000, 10},
	{             200 * 1000, 10},
	{             500 * 1000, 10},
	{        1 * 1000 * 1000, 10},
	{        2 * 1000 * 1000, 10},
	{        5 * 1000 * 1000, 10},
	{       10 * 1000 * 1000, 10},
	{       20 * 1000 * 1000, 10},
	{       50 * 1000 * 1000, 10},
	{      100 * 1000 * 1000, 10},
	{      200 * 1000 * 1000, 10},
	{      500 * 1000 * 1000, 10},
	{ 1 * 1000 * 1000 * 1000, 10},
	{ 2 * 1000 * 1000 * 1000, 10},
	{ 4 * 1000 * 1000 * 1000U, 10},
};

struct Ds1000zSrate {
	uint64_t m_value;
	uint8_t  m_mdepthMask {};

	bool supportsMdepth(uint64_t mdepth, size_t channelCount) const {
		if (mdepth * channelCount == 24'000'000) {
			//TODO: this is somewhat hacky approach
			return m_mdepthMask & 16;
		}
		// as ds1000z samplerates are alway order of magnitude different,
		// we can just count trailing zeros in decadic mode
		// this way, we easily ignore rate division caused by channel count
		size_t zeros {};
		while (mdepth != 0 and mdepth % 10 == 0) {
			++zeros;
			mdepth /= 10;
		}
		return m_mdepthMask & (1 << (zeros - 3));
	}
};

static std::vector<Ds1000zSrate> ds1000zSampleRates {
	//          1 CH sr                          1CH mDepths                  2 CH sr  3/4 CH sr  display halt of memory?
	{                20, 1                 }, // 12k                           10        5         N
	{                50, 1                 }, // 12k                           25       12 !       N   this special case looks to be caused by integer representation of the samplerate
	{               100, 1                 }, // 12k                           50       25         N
	{               200, 1 | 2             }, // 12k 120k                     100       50         N
	{               500, 1 | 2             }, // 12k 120k                     250      125         N
	{              1000, 1 | 2             }, // 12k 120k                     500      250         N
	{              2000, 1 | 2 | 4         }, // 12k 120k 1M2                  1k      500         N
	{              5000, 1 | 2 | 4         }, // 12k 120k 1M2                  2k5      1k25       N
	{         10 * 1000, 1 | 2 | 4         }, // 12k 120k 1M2                  5k       2k5        N
	{         20 * 1000, 1 | 2 | 4         }, // 12k 120k 1M2 12M             10k       5k         N
	{         40 * 1000,                 16}, //                  24M         20k       10k        N
	{         50 * 1000, 1 | 2 | 4 | 8     }, // 12k 120k 1M2 12M             25k      12k5        N
	{        100 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M         50k      25k         N
	{        200 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M        100k      50k         N
	{        400 * 1000,                 16}, //                  24M        200k     100k         N
	{        500 * 1000, 1 | 2 | 4 | 8     }, // 12k 120k 1M2 12M            250k     125k         N
	{   1 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M        500k     250k         N
	{   2 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M          1M     500k         N
	{   4 * 1000 * 1000,                 16}, //                  24M          2M       1M         N
	{   5 * 1000 * 1000, 1 | 2 | 4 | 8     }, // 12k 120k 1M2 12M              2M5      1M25       N
	{  10 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M          5M       2M5        N
	// 10 Msps is magical threshold above which the regularity breaks
	{  25 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M         10M !     5M !       Y
	{  50 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M         25M      12M5        Y
	{ 125 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M         50M !    25M !       Y
	{ 250 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M        125M      50M !       Y
	{ 500 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}, // 12k 120k 1M2 12M 24M        250M     125M         Y
	{1000 * 1000 * 1000, 1 | 2 | 4 | 8 | 16}  // 12k 120k 1M2 12M 24M        500M     250M         Y
};

static std::vector<uint64_t> mso5000SampleRates {
	                   100UL,
	                   200UL,
	                   500UL,
	                  1000UL,
	                  2000UL,
	                  5000UL,
	             10 * 1000UL,
	             20 * 1000UL,
	             50 * 1000UL,
	            100 * 1000UL,
	            200 * 1000UL,
	            500 * 1000UL,
	       1 * 1000 * 1000UL,
	       2 * 1000 * 1000UL,
	       5 * 1000 * 1000UL,
	      10 * 1000 * 1000UL,
	      20 * 1000 * 1000UL,
	      50 * 1000 * 1000UL,
	     100 * 1000 * 1000UL,
	     200 * 1000 * 1000UL,
	     500 * 1000 * 1000UL,
	1 * 1000 * 1000 * 1000UL,
	2 * 1000 * 1000 * 1000UL,
	4 * 1000 * 1000 * 1000UL, // see GetMso5000AnalogBankUsage
	8 * 1000 * 1000 * 1000UL  // see GetMso5000AnalogBankUsage
};

vector<uint64_t> RigolOscilloscope::GetSampleRatesNonInterleaved()
{
	LogTrace("GetSampleRatesNonInterleaved called");

	//TODO: cache samplerates

	//FIXME
	switch (m_series)
	{
		case Series::MSODS1000Z:
		{
			vector<uint64_t> rates {};
			auto divisor = GetMsods1000ZChannelDivisor();
			auto mdepth = GetSampleDepth();
			for (const auto& rate : ds1000zSampleRates)
				if (rate.supportsMdepth(mdepth, divisor))
				{
					// 125M is never divided by 2
					// 125M /2 -> 50M  /2 -> 25M
					// 250M /2 -> 125M /2 -> 50M
					if (rate.m_value == 125'000'000 and divisor != 1)
						rates.push_back(100'000'000 / divisor);
					else if (rate.m_value == 25'000'000 and divisor != 1)
						rates.push_back(20'000'000 / divisor);
					else if (rate.m_value == 250'000'000 and divisor == 4)
						rates.push_back(200'000'000 / divisor);
					else
						rates.push_back(rate.m_value / divisor);
				}
			return rates;
		}


		case Series::MSO5000:
		{
			auto rates = mso5000SampleRates;
			auto const bankUsage = GetMso5000AnalogBankUsage();
			if (bankUsage[0] == 2 or bankUsage[1] == 2)
			{
				rates.pop_back();
				rates.pop_back();
			}
			else if (bankUsage[0] and bankUsage[1])
				rates.pop_back();
			return rates;
		}
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
		{

			// For DHO model, srates depend on high/low sample rate models, max srate and number of enabled channels
			uint64_t maxSrate = m_maxSrate / GetEnabledChannelCount();
			vector<uint64_t> ret;
			auto& srates = [&]() -> std::map<uint64_t, double>& {
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				return (m_lowSrate ? dhoLowSampleRates : dhoHighSampleRates);
			}();
			for (auto curSrateItem : srates)
			{
				auto curSrate = curSrateItem.first;
				if(curSrate<=maxSrate)
				{
					ret.push_back(curSrate);
				}
				else break;
			}
			return ret;
		}

		case Series::DS1000:
		case Series::UNKNOWN:
			LogError("RigolOscilloscope::GetSampleRatesNonInterleaved not implemented for this model\n");
			break;
	}
	return {};
}

vector<uint64_t> RigolOscilloscope::GetSampleRatesInterleaved()
{
	//FIXME
	LogError("RigolOscilloscope::GetSampleRatesInterleaved not implemented for this model\n");
	return {};
}

set<Oscilloscope::InterleaveConflict> RigolOscilloscope::GetInterleaveConflicts()
{
	switch (m_series)
	{
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			// No interleave conflicts possible on DHO models
			return {};
		case Series::UNKNOWN:
		case Series::DS1000:
		case Series::MSODS1000Z:
		case Series::MSO5000:
			break;
	}
	//FIXME
	LogError("RigolOscilloscope::GetInterleaveConflicts not implemented for this model\n");
	return {};
}

vector<uint64_t> RigolOscilloscope::GetSampleDepthsNonInterleaved()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_depths;
}

vector<uint64_t> RigolOscilloscope::GetSampleDepthsInterleaved()
{
	switch (m_series)
	{
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			// Sample Depths are dynamical (depending on the number of active channels) in DHO models
			return GetSampleDepthsNonInterleaved();
		
		case Series::DS1000:
		case Series::MSODS1000Z:
		case Series::MSO5000:
		case Series::UNKNOWN:
			break;
	}
	
	//FIXME
	LogError("RigolOscilloscope::GetSampleDepthsInterleaved not implemented for this model\n");
	return {};
}

uint64_t RigolOscilloscope::GetSampleRate()
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_srate.has_value())
			return *m_srate;
	
		LogTrace("samplerate updating\n");
	}
	// m_transport->SendCommandQueued("*WAI");

	auto ret = Trim(m_transport->SendCommandQueuedWithReply(":ACQ:SRAT?"));

	double rate;
	sscanf(ret.c_str(), "%lf", &rate);
	//TODO: check sscanf return value for parsing errors
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_srate = (uint64_t)rate;
		LogTrace("samplerate updated, m_srate %" PRIu64 "\n", *m_srate);
		return rate;
	}
}

uint64_t RigolOscilloscope::GetSampleDepth()
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_mdepth.has_value())
			return *m_mdepth;
	
		LogTrace("mem depth updating\n");
	}

	auto depth = [&]() -> uint64_t {
		for(auto i = 2u; i; --i)
		{
			// it may fail in the first loop, but whould get valid value in the second
			// this way we avoid recursion
			auto const depth_str = m_transport->SendCommandQueuedWithReply(":ACQ:MDEP?");
			auto const depth = parseU64(depth_str);
			if (depth)
				return *depth;
			LogError("could not parse sampled depth from %s. Falling back to lowest one\n", depth_str.c_str());
			auto depths = GetSampleDepthsNonInterleaved();
			if (depths.empty())
			{
				LogError("no known sampled depths known!\n");
				return 0;
			}
			SetSampleDepth(GetSampleDepthsNonInterleaved()[0]);
		}
		return 0;
	}();

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_mdepth = (uint64_t)depth;
		LogTrace("mem depth updated, m_mdepth %" PRIu64 "\n", *m_mdepth);
		return *m_mdepth;
	}
}

void RigolOscilloscope::SetSampleDepth(uint64_t depth)
{
	{
		auto depths = GetSampleDepthsNonInterleaved();
		auto requestedValueisValid = false;
		for (auto const& depth_item : depths)
			if (depth == depth_item)
			{
				requestedValueisValid = true;
				break;
			}
		if (not requestedValueisValid)
		{
			LogWarning("requested sample depth %" PRIu64 " is not any of %zd of supported by this device\n", depth, depths.size());
			return;
		}
	}

	switch (m_series)
	{
		case Series::MSO5000:
		{
			// The MSO5 series will only process a sample depth setting if the oscilloscope is in auto or normal mode.
			// It's frustrating, but to accommodate, we'll grab the current mode and status for restoration later, then stick the
			// scope into auto mode
			lock_guard<recursive_mutex> lock(m_transport->GetMutex()); // this sequence may not be interrupted by others
			string trigger_sweep_mode = m_transport->SendCommandQueuedWithReply(":TRIG:SWE?");
			string trigger_status = m_transport->SendCommandQueuedWithReply(":TRIG:STAT?");
			m_transport->SendCommandQueued(":TRIG:SWE AUTO");
			m_transport->SendCommandQueued(":RUN");
			switch(depth)
			{
				case 1000:
					m_transport->SendCommandQueued("ACQ:MDEP 1k");
					break;
				case 10000:
					m_transport->SendCommandQueued("ACQ:MDEP 10k");
					break;
				case 100000:
					m_transport->SendCommandQueued("ACQ:MDEP 100k");
					break;
				case 1000000:
					m_transport->SendCommandQueued("ACQ:MDEP 1M");
					break;
				case 10000000:
					m_transport->SendCommandQueued("ACQ:MDEP 10M");
					break;
				case 25000000:
					m_transport->SendCommandQueued("ACQ:MDEP 25M");
					break;
				case 50000000:
					if(m_opt200M)
						m_transport->SendCommandQueued("ACQ:MDEP 50M");
					else
						LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
					break;
				case 100000000:
					//m_transport->SendCommandQueued("ACQ:MDEP 100M");
					LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
					break;
				case 200000000:
					//m_transport->SendCommandQueued("ACQ:MDEP 200M");
					LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
					break;
				default:
					LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
			}
			m_transport->SendCommandQueued(":TRIG:SWE " + trigger_sweep_mode);
			// This is a little hairy - do we want to stop the instrument again if it was stopped previously? Probably?
			if(trigger_status == "STOP")
				m_transport->SendCommandQueued(":STOP");
			break;
		}

		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
			m_transport->SendCommandQueued("ACQ:MDEP " + to_string(depth));
			break;

		case Series::MSODS1000Z:
		{
			if (depth == 24'000'000 and not m_opt24M) {
				LogError("This DS1000Z device does not have 24M option installed\n");
				return;
			}
			// memory depth is configurable only when scope is not stopped
			{
				lock_guard<recursive_mutex> lock(m_transport->GetMutex()); // this sequence may not be interrupted by others
				string original_trigger_status = m_transport->SendCommandQueuedWithReply(":TRIG:STAT?");
				m_transport->SendCommandQueued(":RUN");
				m_transport->SendCommandQueued("ACQ:MDEP " + to_string(depth));
				//TODO: whould we also use switch to accept only valid values?
				// m_transport->SendCommandQueued("ACQ:MDEP " + to_string(depth));
				if(original_trigger_status == "STOP")
					m_transport->SendCommandQueued(":STOP");
				m_transport->FlushCommandQueue();
			}
			{
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				m_srate.reset(); // changing depth and keeping timebase quite often results in chnage of srate
			}
			break;
		}

		case Series::DS1000:
		case Series::UNKNOWN:
			LogError("Memory depth setting not implemented for this series");
			break;
	}
	
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_mdepth.reset();
	}
}

void RigolOscilloscope::SetSampleRate(uint64_t rate)
{
	// Rigol scopes do not have samplerate controls. Only timebase can be adjusted :TIMebase:SCALe
	{
		bool requestedValueisValid {};
		auto rates = GetSampleRatesNonInterleaved();
		for (auto const& rate_item : rates)
			if (rate_item == rate)
			{
				requestedValueisValid = true;
				break;
			}
		if (not requestedValueisValid)
		{
			LogWarning("requested sample rate %" PRIu64 " is not any of %zd of supported by this device\n", rate, rates.size());
			return;
		}
	}
	double sampletime = GetSampleDepth() / (double)rate;
	// locally cache current value before we change the timebase,
	// so w can restore it after th timebase change
	auto const triggerOffset = GetTriggerOffset();
	
	switch (m_series)
	{
		case Series::DHO800:
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO900:
		{	// Scale factor is not constant across all sample rates for DHO models
			double timeScaleFactor = 10;
			
			auto& srates = [&]() -> std::map<uint64_t, double>& {
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				return (m_lowSrate ? dhoLowSampleRates : dhoHighSampleRates);
			}();

			auto d = srates.find(rate);
			if (d != srates.end())
			{
				timeScaleFactor = d->second;
			}
			m_transport->SendCommandQueued(string(":TIM:SCAL ") + to_string(sampletime / timeScaleFactor));
			// To ghet back the current samplerate, the scope has to trigger at least once!
			// RUN mode is not enough and neither is fidlding with memory depth
			auto singleshot = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:SWE?")) == "SING";
			auto was_stopped = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:STAT?")) == "STOP";
			if (was_stopped)
				m_transport->SendCommandQueued("RUN");
			m_transport->SendCommandQueued("TFOR");
			if (was_stopped)
				m_transport->SendCommandQueued("STOP");
			else if (singleshot)
				m_transport->SendCommandQueued(":SING");

			break;
		}

		case Series::MSO5000:
		{
			//TODO: consoder defining available time scales and do lookup of closes one (equal or lower) then we get from formula
			LogTrace("setting target samplerate %lu\n", rate);
			switch (rate)
			{
				// 4 Gpss result in 25 us, but the scope's native step is 20 us, anything above results in 
				case 4'000'000'000UL:
					m_transport->SendCommandQueued(string(":TIM:SCAL ") + to_string(2e-5));
					break;
				
				// 8 Gpss result in 12.5 us, but the scope's native step is 10 us, anything above results in 
				case 8'000'000'000UL:
					m_transport->SendCommandQueued(string(":TIM:SCAL ") + to_string(1e-5));
					break;

				default:
					m_transport->SendCommandQueued(string(":TIM:SCAL ") + to_string(sampletime / 10));
					break;
				
			}
			{
				// TODO: is is really necessary to reset cached sample depth value?
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				m_mdepth.reset();
			}
			SetSampleDepth(GetSampleDepth()); // see note at MSODO1000Z
			break;
		}

		case Series::MSODS1000Z:
		{
			// The following equation describes the relationship among memory depth, sample rate, and waveform length:
			//     Memory Depth = Sample Rate x Waveform Length
			// Wherein, the Memory Depth can be set using the :ACQuire:MDEPth command, and
			// the Waveform Length is the product of the horizontal timebase (set by
			// the :TIMebase[:MAIN]:SCALe command) times the number of the horizontal scales
			// (12 for DS1000Z).
			//     Mdepth = Srate * wlength
			//     Mdepth = Srate * Tscale * 12
			//     Mdepth / (Srate * 12) =  * Tscale
			auto const divisor = GetMsods1000ZChannelDivisor();
			LogTrace("setting target samplerate %lu, divisor %zu\n", rate, divisor);
			auto const timescale = [&]() -> float {
				if (divisor != 4 and (rate / divisor) >= 25'000'000)
					return sampletime / 12 / 2;
				return sampletime / 12;
			}();
			m_transport->SendCommandQueued(string(":TIM:SCAL ") + to_string(timescale));
			// Due to unknown reason, when we read SCAL right fter changing, we get the old value
			// solution is to execute _any_ command with response (e.g. *IDN? works).
			// Another requirement is that this read has to happen after the acquisition finishes which could take a long time on samplerates
			// (this is visible even when you operate the scope manually)
			// Workaround to both is to (re-)write memory depth, don't ask me why.
			{
				// TODO: is is really necessary to reset cached sample depth value?
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				m_mdepth.reset();
			}
			SetSampleDepth(GetSampleDepth());
			break;
		}

		case Series::UNKNOWN:
		case Series::DS1000:
			LogWarning("This scope does nto support samplerate settings (yet)");
			break;
	}


	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_srate.reset();
		m_mdepth.reset();
	}
	// To prevent trigger offset travelling on Srate change (timebase change),
	// re-set trigger location, because difference (time) between
	// our trigger reference point (start of sample buffer) and
	// scope trigger reference point (mid of the sample buffer) changed.
	SetTriggerOffset(triggerOffset);
	
}

void RigolOscilloscope::SetTriggerOffset(int64_t offset)
{
	//Rigol standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	auto rate = GetSampleRate();
	auto depth = GetSampleDepth();
	auto width_fs = static_cast<int64_t>(round(FS_PER_SECOND * depth / rate));
	auto halfwidth_fs = width_fs /2;
	if (offset > width_fs)
		offset = width_fs; // we want to ensure, the trigger is inside the capture range 0~mdepth
	
	// DHO800 allows very limited trigger offset range in stop mode
	// let's assume this is behadior of all DHOs for now...
	// TODO: check if it is necessary for other DHO series

	auto const resume_into_stop = [&]() -> bool
	{
		switch (m_series)
		{
			case Series::DHO1000:
			case Series::DHO4000:
			case Series::DHO800:
			case Series::DHO900:
			{
				// Set in run mode to be able to set trigger offset in full range
				if (Trim(m_transport->SendCommandQueuedWithReply(":TRIG:STAT?")) == "STOP")
				{
					m_transport->SendCommandQueued("RUN");
					return true;
				}
				return false;
			}

			case Series::DS1000:
			case Series::MSODS1000Z:
			case Series::MSO5000:
			case Series::UNKNOWN:
				break;
		}
		return false;
	}();

	m_transport->SendCommandQueued(string(":TIM:MAIN:OFFS ") + to_string((halfwidth_fs - offset) * SECONDS_PER_FS));
	
	if (resume_into_stop)
		m_transport->SendCommandQueued("STOP");

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_triggerOffset.reset();
	}

}

int64_t RigolOscilloscope::GetTriggerOffset()
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_triggerOffset.has_value())
			return *m_triggerOffset;
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":TIM:MAIN:OFFS?"));


	//Result comes back in scientific notation
	double offsetval;
	sscanf(reply.c_str(), "%lf", &offsetval);
	auto offset = static_cast<int64_t>(round(offsetval * FS_PER_SECOND));

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_triggerOffset = halfwidth - offset;
		return *m_triggerOffset;
	}
}

bool RigolOscilloscope::HasInterleavingControls()
{
	return false;
}

bool RigolOscilloscope::CanInterleave()
{
	return false;
}

bool RigolOscilloscope::IsInterleaving()
{
	return false;
}

bool RigolOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void RigolOscilloscope::PullTrigger()
{
	//Figure out what kind of trigger is active.
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:MODE?"));
	if(reply == "EDGE")
		PullEdgeTrigger();

	//Unrecognized trigger type
	else
	{
		LogWarning("Unknown trigger type \"%s\"\n", reply.c_str());
		// Pull Edge trigger anyway to prevent looping on this method call
		PullEdgeTrigger();
		return;
	}
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void RigolOscilloscope::PullEdgeTrigger()
{
	auto et = [&]() -> EdgeTrigger* {
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		//Clear out any triggers of the wrong type
		if((m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL))
		{
			delete m_trigger;
			m_trigger = NULL;
		}
	
		//Create a new trigger if necessary
		if(m_trigger == NULL)
			m_trigger = new EdgeTrigger(this);
		return dynamic_cast<EdgeTrigger*>(m_trigger);
	}();

	//Source
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:EDGE:SOUR?"));
	auto chan = GetOscilloscopeChannelByHwName(reply);
	et->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source %s\n", reply.c_str());

	//Level
	reply = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:EDGE:LEV?"));
	et->SetLevel(stof(reply));

	//Edge slope
	reply = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:EDGE:SLOPE?"));
	if(reply == "POS")
		et->SetType(EdgeTrigger::EDGE_RISING);
	else if(reply == "NEG")
		et->SetType(EdgeTrigger::EDGE_FALLING);
	else if(reply == "RFAL")
		et->SetType(EdgeTrigger::EDGE_ANY);
}

void RigolOscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void RigolOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	//Type
	m_transport->SendCommandQueued(":TRIG:MODE EDGE");

	//Source
	m_transport->SendCommandQueued(":TRIG:EDGE:SOUR " + trig->GetInput(0).m_channel->GetHwname());

	//Level
	m_transport->SendCommandQueued(string("TRIG:EDGE:LEV ") + to_string(trig->GetLevel()));

	//Slope
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommandQueued(":TRIG:EDGE:SLOPE POS");
			break;
		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommandQueued(":TRIG:EDGE:SLOPE NEG");
			break;
		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommandQueued(":TRIG:EDGE:SLOPE RFAL");
			break;
		default:
			LogWarning("Unknown edge type\n");
			return;
	}
}

/**
	@brief Forces 16-bit transfer mode on/off when for HD models
 */
void RigolOscilloscope::ForceHDMode(bool mode)
{
	switch (m_series)
	{
		case Series::DHO1000:
		case Series::DHO4000:
		case Series::DHO800:
		case Series::DHO900:
		{
			bool highDefinition {};
			{
				lock_guard<recursive_mutex> lock(m_cacheMutex);
				if (mode == m_highDefinition)
					break;
				m_highDefinition = highDefinition = mode;
			}
			m_transport->SendCommandQueued(string(":WAV:FORM ") + (highDefinition ? "WORD" : "BYTE"));
			break;
		}


		case Series::UNKNOWN:
		case Series::DS1000:
		case Series::MSODS1000Z:
		case Series::MSO5000:
			//TODO: report/log invalidity of this
			break;
	}
}

#pragma GCC diagnostic pop