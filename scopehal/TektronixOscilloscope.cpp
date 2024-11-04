/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of TektronixOscilloscope

	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "TektronixOscilloscope.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "DropoutTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "WindowTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport connected to the scope
 */
TektronixOscilloscope::TektronixOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_sampleRateValid(false)
	, m_sampleRate(0)
	, m_sampleDepthValid(false)
	, m_sampleDepth(0)
	, m_triggerOffsetValid(false)
	, m_triggerOffset(0)
	, m_spanValid(false)
	, m_span(0)
	, m_rbwValid(false)
	, m_rbw(0)
	, m_dmmAutorangeValid(false)
	, m_dmmAutorange(false)
	, m_dmmChannelValid(false)
	, m_dmmChannel(0)
	, m_dmmModeValid(false)
	, m_dmmMode(Multimeter::DC_VOLTAGE)
	, m_digitalChannelBase(0)
	, m_digitalChannelCount(0)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_maxBandwidth(1000)
	, m_hasDVM(false)
	, m_hasAFG(false)
	, m_afgEnabled(false)
	, m_afgAmplitude(0.5)
	, m_afgOffset(0)
	, m_afgFrequency(100000)
	, m_afgDutyCycle(0.5)
	, m_afgShape(FunctionGenerator::SHAPE_SINE)
	, m_afgImpedance(FunctionGenerator::IMPEDANCE_HIGH_Z)
{
	//If we are reconnecting after a crash or something went wrong, the scope might have a reply or two queued up
	//If this happens, every time we send a command we'll get a reply meant for something else!
	//Resync twice to verify we get good alignment after
	ResynchronizeSCPI();
	ResynchronizeSCPI();

	//Figure out what device family we are
	if(m_model.find("MSO5") == 0)
		m_family = FAMILY_MSO5;
	else if(m_model.find("MSO6") == 0)
		m_family = FAMILY_MSO6;
	else
		m_family = FAMILY_UNKNOWN;

	//Last digit of the model number is the number of channels
	string model_number = m_model;
	model_number.erase(
		std::remove_if(
			model_number.begin(),
			model_number.end(),
			[]( char const& c ) -> bool { return !std::isdigit(c); }
		),
		model_number.end()
	);
	int nchans = stoi(model_number) % 10;

	// No header in the reply of queries
	m_transport->SendCommandQueued("HEAD 0");

	//Device specific initialization
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued("ACQ:MOD SAM");				//actual sampled data, no averaging etc
			m_transport->SendCommandQueued("VERB OFF");					//Disable verbose mode (send shorter commands)
			m_transport->SendCommandQueued("ACQ:STOPA SEQ");			//Stop after acquiring a single waveform
			m_transport->SendCommandQueued("HOR:MODE MAN");				//Enable manual sample rate and record length
			m_transport->SendCommandQueued("HOR:DEL:MOD ON");			//Horizontal position is in time units
			m_transport->SendCommandQueued("SV:RBWMODE MAN");			//Manual resolution bandwidth control
			m_transport->SendCommandQueued("SV:LOCKCENTER 0");			//Allow separate center freq per channel

			m_maxBandwidth = 1e-6 * stof(
				m_transport->SendCommandQueuedWithReply("CONFIG:ANALO:BANDW?"));	//Figure out what bandwidth we have
																					//(so we know what probe BW is)
			break;

		default:
			// 8-bit signed data
			m_transport->SendCommandQueued("DATA:ENC RIB;WID 1");
			m_transport->SendCommandQueued("DATA:SOURCE CH1, CH2, CH3, CH4;START 0; STOP 100000");
			m_transport->SendCommandQueued("ACQ:STOPA SEQ;REPE 1");
			break;
	}

	//TODO: get colors for channels 5-8 on wide instruments
	const char* colors_default[4] = { "#ffff00", "#32ff00", "#5578ff", "#ff0084" };	//yellow-green-violet-pink
	const char* colors_mso5[6] = { "#faf539", "#23cdda", "#ee435f",
	                               "#90ce3b", "#fa9a32", "#2526bb" };	            //yellow-cyan-red-green-orange-blue
	                                                                                //picked from a remoting screenshot,
	                                                                                //seems like they might be a little dim
	const char* colors_mso6[4] = { "#ffff00", "#20d3d8", "#f23f59", "#f16727" };	//yellow-cyan-pink-orange

	for(int i=0; i<nchans; i++)
	{
		//Color the channels based on Tektronix's standard color sequence
		string color = "#ffffff";
		switch(m_family)
		{
			case FAMILY_MSO5:
				color = colors_mso5[i % 6];
				break;

			case FAMILY_MSO6:
				color = colors_mso6[i % 4];
				break;

			default:
				color = colors_default[i % 4];
				break;
		}

		//Create the channel
		m_channels.push_back(
			new OscilloscopeChannel(
				this,
				string("CH") + to_string(i+1),
				color,
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_VOLTS),
				Stream::STREAM_TYPE_ANALOG,
				i));
	}
	m_analogChannelCount = nchans;

	//Add spectrum view channels
	m_spectrumChannelBase = m_channels.size();
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			for(size_t i=0; i<m_analogChannelCount; i++)
			{
				m_channels.push_back(
					new SpectrumChannel(
					this,
					string("CH") + to_string(i+1) + "_SPECTRUM",
					GetOscilloscopeChannel(i)->m_displaycolor,
					m_channels.size()));
			}
			break;

		default:
			//no spectrum view
			break;
	}

	//Add all possible digital channels
	m_digitalChannelBase = m_channels.size();
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			for(size_t i=0; i<m_analogChannelCount; i++)
			{
				for(size_t j=0; j<8; j++)
				{
					//TODO: pick colors properly
					auto chan = new OscilloscopeChannel(
						this,
						GetOscilloscopeChannel(i)->GetHwname() + "_D" + to_string(j),
						GetOscilloscopeChannel(i)->m_displaycolor,
						Unit(Unit::UNIT_FS),
						Unit(Unit::UNIT_COUNTS),
						Stream::STREAM_TYPE_DIGITAL,
						m_channels.size());

					m_flexChannelParents[chan] = i;
					m_flexChannelLanes[chan] = j;
					m_channels.push_back(chan);
					m_digitalChannelCount++;
				}
			}
			break;

		default:
			break;
	}

	//Add the external trigger input
	switch(m_family)
	{
		//MSO5 does not appear to have an external trigger input
		//except in low-profile rackmount models (not yet supported)
		case FAMILY_MSO5:
			m_extTrigChannel = NULL;
			break;

		//MSO6 calls it AUX, not EXT
		case FAMILY_MSO6:
			m_extTrigChannel = new OscilloscopeChannel(
				this,
				"AUX",
				"",
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_VOLTS),
				Stream::STREAM_TYPE_TRIGGER,
				m_channels.size());
			m_channels.push_back(m_extTrigChannel);
			break;

		default:
			m_extTrigChannel = new OscilloscopeChannel(
				this,
				"EX",
				"",
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_VOLTS),
				Stream::STREAM_TYPE_TRIGGER,
				m_channels.size());
			m_channels.push_back(m_extTrigChannel);
			break;
	}

	string reply = m_transport->SendCommandQueuedWithReply("LICENSE:APPID?", false);
	reply = reply.substr(1, reply.size() - 2); // Chop off quotes
	vector<string> apps;
	stringstream s_stream(reply);
	while(s_stream.good()) {
		string substr;
		getline(s_stream, substr, ',');
		apps.push_back(substr);
	}

	for (auto app : apps)
	{
		if (app == "DVM")
		{
			m_hasDVM = true;
			LogDebug(" * Tek has DVM\n");
		}
		else if (app == "AFG")
		{
			m_hasAFG = true;
			LogDebug(" * Tek has AFG\n");
		}
		else
		{
			LogDebug("(* Tek also has '%s' (ignored))\n", app.c_str());
		}

		// Bandwidth expanding options reflected in earlier query for max B/W
	}

	//Add AWG channel
	if(m_hasAFG)
	{
		m_awgChannel = new FunctionGeneratorChannel(this, "AWG", "#808080", m_channels.size());
		m_channels.push_back(m_awgChannel);
	}
	else
		m_awgChannel = nullptr;

	//Figure out what probes we have connected
	DetectProbes();
}

TektronixOscilloscope::~TektronixOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant driver name string "tektronix"
string TektronixOscilloscope::GetDriverNameInternal()
{
	return "tektronix";
}

unsigned int TektronixOscilloscope::GetInstrumentTypes() const
{
	unsigned int mask = Instrument::INST_OSCILLOSCOPE;
	if(m_hasDVM)
		mask |= Instrument::INST_DMM;
	if(m_hasAFG)
		mask |= Instrument::INST_FUNCTION;
	return mask;
}

uint32_t TektronixOscilloscope::GetInstrumentTypesForChannel(size_t i) const
{
	if(m_hasAFG && (i == m_awgChannel->GetIndex()))
		return Instrument::INST_FUNCTION;

	//If we get here, it's an oscilloscope channel
	//Report DMM functionality if available
	if(m_hasDVM && (i < m_analogChannelCount))
		return Instrument::INST_OSCILLOSCOPE | Instrument::INST_DMM;
	else
		return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

string TektronixOscilloscope::GetProbeName(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_probeNames[i];
}

/**
	@brief Look at each input channel and determine what kind of probe, if any, is connected
 */
void TektronixOscilloscope::DetectProbes()
{
	std::vector<bool> currentlyEnabled;
	for (size_t i = 0; i < m_channels.size(); i++)
	{
		bool isEnabled = IsChannelEnabled(i);
		currentlyEnabled.push_back(isEnabled);
	}

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

			//Figure out what kind of probe is attached (analog or digital).
			//If a digital probe (TLP058), disable this channel and mark as not usable
			for(size_t i=0; i<m_analogChannelCount; i++)
			{
				string reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":PROBETYPE?");

				if(reply == "DIG")
				{
					lock_guard<recursive_mutex> lock(m_cacheMutex);
					m_probeTypes[i] = PROBE_TYPE_DIGITAL_8BIT;
					m_probeNames[i] = "DIG";
				}

				//Treat anything else as analog. See what type
				else
				{
					string id = TrimQuotes(m_transport->SendCommandQueuedWithReply(
						GetOscilloscopeChannel(i)->GetHwname() + ":PROBE:ID:TYP?"));

					lock_guard<recursive_mutex> lock(m_cacheMutex);
					m_probeNames[i] = reply;

					if(id == "TPP1000")
						m_probeTypes[i] = PROBE_TYPE_ANALOG_250K;
					else if(id.find("TCP") == 0)
						m_probeTypes[i] = PROBE_TYPE_ANALOG_CURRENT;
					else
						m_probeTypes[i] = PROBE_TYPE_ANALOG;
				}
			}
			break;

		default:
			break;
	}

	for (size_t i = 0; i < m_channels.size(); i++)
	{
		auto ochan = GetOscilloscopeChannel(i);
		if(!ochan)
			continue;

		if (currentlyEnabled[i])
			EnableChannel(i);
		else
			DisableChannel(i);
	}
}

void TektronixOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelDigitalThresholds.clear();
	m_channelCouplings.clear();
	m_channelsEnabled.clear();
	m_probeTypes.clear();
	m_channelDeskew.clear();
	m_channelUnits.clear();
	m_channelCenterFrequencies.clear();

	//Clear cached display name of all channels
	for(auto c : m_channels)
	{
		if(GetInstrumentTypesForChannel(c->GetIndex()) & Instrument::INST_OSCILLOSCOPE)
			c->ClearCachedDisplayName();
	}

	m_sampleRateValid = false;
	m_sampleDepthValid = false;
	m_triggerOffsetValid = false;
	m_spanValid = false;
	m_rbwValid = false;
	m_dmmAutorangeValid = false;
	m_dmmChannelValid = false;
	m_dmmModeValid = false;

	delete m_trigger;
	m_trigger = NULL;

	//Once we've flushed everything, re-detect what probes are present
	DetectProbes();
}

bool TektronixOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(m_extTrigChannel && i == m_extTrigChannel->GetIndex())
		return false;

	//Not a scope channel (AWG)? Skip it
	auto ochan = GetOscilloscopeChannel(i);
	if(!ochan)
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_probeTypes.find(i) != m_probeTypes.end())
		{
			//Pre-checks based on type
			if(IsDigital(i))
			{
				//If the parent analog channel doesn't have a digital probe, we're disabled
				size_t parent = m_flexChannelParents[GetOscilloscopeChannel(i)];
				if(m_probeTypes[parent] != PROBE_TYPE_DIGITAL_8BIT)
					return false;
			}
			else if(IsAnalog(i))
			{
				//If we're an analog channel with a digital probe connected, the analog channel is unusable
				if(m_probeTypes[i] == PROBE_TYPE_DIGITAL_8BIT)
					return false;
			}
			else if(IsSpectrum(i))
			{
				//If we're an analog channel with a digital probe connected, the analog channel is unusable
				if(m_probeTypes[i - m_spectrumChannelBase] == PROBE_TYPE_DIGITAL_8BIT)
					return false;
			}
		}

		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	string reply;
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

			//Undocumented command to toggle spectrum view state
			if(IsSpectrum(i))
			{
				reply = m_transport->SendCommandQueuedWithReply(
					m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE?");
			}
			else
			{
				reply = m_transport->SendCommandQueuedWithReply(
					string("DISP:WAVEV:") + ochan->GetHwname() + ":STATE?");

				if (IsDigital(i))
				{
					// Digital channels may report enabled, but not actually be displayed because the associated _DALL
					//  view is not enabled. Especially relevant after doing File -> Default Setup
					string dall_reply = m_transport->SendCommandQueuedWithReply(
						string("DISP:WAVEV:") + m_channels[m_flexChannelParents[ochan]]->GetHwname() + "_DALL:STATE?");

					if (dall_reply == "0")
					{
						reply = "0";
					}
				}
			}
			break;

		default:
			break;
	}

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

void TektronixOscilloscope::EnableChannel(size_t i)
{
	if(!CanEnableChannel(i))
		return;
	if(m_extTrigChannel && i == m_extTrigChannel->GetIndex())
		return;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if( IsAnalog(i) && (m_probeTypes[i] == PROBE_TYPE_DIGITAL_8BIT) )
			return;

		//If we're a digital channel with an analog probe connected, we're unusable
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				if(IsDigital(i))
				{
					//If the parent analog channel doesn't have a digital probe, we're disabled
					size_t parent = m_flexChannelParents[GetOscilloscopeChannel(i)];
					if(m_probeTypes[parent] != PROBE_TYPE_DIGITAL_8BIT)
						return;
				}
				break;

			default:
				break;
		}
	}

	//Keep the cache mutex locked while pushing the command into the queue.
	//This prevents races in which the trigger is armed before we can submit the actual enable command.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelEnableStatusDirty.emplace(i);
	m_channelsEnabled[i] = true;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(IsSpectrum(i))
				m_transport->SendCommandQueued(m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE ON");
			else
			{
				// <Commented out 2022-11-18> Was this needed on earlier scope firmware? Not needed
				//                            now and confuses the enable state of individual digital channels.
				// //Make sure the digital group is on
				// if(IsDigital(i))
				// {
				// 	size_t parent = m_flexChannelParents[GetOscilloscopeChannel(i)];
				// 	m_transport->SendCommandQueued(
				// 		string("DISP:WAVEV:") + m_channels[parent]->GetHwname() + "_DALL:STATE ON");
				// }
				// </Commented out 2022-11-18>

				//Difference between SELECT:CHx 1 vs this??
				m_transport->SendCommandQueued(string("DISP:WAVEV:") + GetOscilloscopeChannel(i)->GetHwname() + ":STATE ON");
			}
			break;

		default:
			break;
	}
}

bool TektronixOscilloscope::CanEnableChannel(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//If we're an analog channel with a digital probe connected, the analog channel is unusable
	if(IsAnalog(i) && (m_probeTypes[i] == PROBE_TYPE_DIGITAL_8BIT) )
		return false;

	//Can't use spectrum view if the parent channel has a digital channel connected
	if(IsSpectrum(i))
	{
		if(m_probeTypes[i - m_spectrumChannelBase] == PROBE_TYPE_DIGITAL_8BIT)
			return false;
	}

	//If the parent analog channel doesn't have a digital probe, we're unusable
	if(IsDigital(i))
	{
		size_t parent = m_flexChannelParents[GetOscilloscopeChannel(i)];
		if(m_probeTypes[parent] != PROBE_TYPE_DIGITAL_8BIT)
			return false;
	}

	return true;
}

void TektronixOscilloscope::DisableChannel(size_t i)
{
	//Not a scope channel (AWG)? Skip it
	auto ochan = GetOscilloscopeChannel(i);
	if(!ochan)
		return;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if( IsAnalog(i) && (m_probeTypes[i] == PROBE_TYPE_DIGITAL_8BIT) )
			return;
	}

	//Keep the cache mutex locked while pushing the command into the queue.
	//This prevents races in which the trigger is armed before we can submit the actual enable command.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelEnableStatusDirty.emplace(i);
	m_channelsEnabled[i] = false;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(IsSpectrum(i))
				m_transport->SendCommandQueued(m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE OFF");
			else
				m_transport->SendCommandQueued(string("DISP:WAVEV:") + ochan->GetHwname() + ":STATE OFF");
			break;

		default:
			break;
	}
}

OscilloscopeChannel::CouplingType TektronixOscilloscope::GetChannelCoupling(size_t i)
{
	//Check cache first
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	//If not analog, return default
	if(!IsAnalog(i))
		return OscilloscopeChannel::COUPLE_DC_50;

	OscilloscopeChannel::CouplingType coupling = OscilloscopeChannel::COUPLE_DC_1M;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				string coup = m_transport->SendCommandQueuedWithReply(
					GetOscilloscopeChannel(i)->GetHwname() + ":COUP?");
				float nterm = stof(m_transport->SendCommandQueuedWithReply(
					GetOscilloscopeChannel(i)->GetHwname() + ":TER?"));

				//TODO: Tek's 1 GHz passive probes are 250K ohm impedance at the scope side.
				//We report anything other than 50 ohm as 1M because scopehal doesn't have API support for that.
				if(coup == "AC")
					coupling = OscilloscopeChannel::COUPLE_AC_1M;
				else if(nterm == 50)
					coupling = OscilloscopeChannel::COUPLE_DC_50;
				else
					coupling = OscilloscopeChannel::COUPLE_DC_1M;
			}
			break;

		default:

			// FIXME
			coupling = OscilloscopeChannel::COUPLE_DC_1M;
		/*
		#if 0

			m_transport->SendCommandImmediate(GetOscilloscopeChannel(i)->GetHwname() + ":COUP?");
			string coup_reply = m_transport->ReadReply();
			m_transport->SendCommandImmediate(GetOscilloscopeChannel(i)->GetHwname() + ":IMP?");
			string imp_reply = m_transport->ReadReply();

			OscilloscopeChannel::CouplingType coupling;
			if(coup_reply == "AC")
				coupling = OscilloscopeChannel::COUPLE_AC_1M;
			else if(coup_reply == "DC")
			{
				if(imp_reply == "ONEM")
					coupling = OscilloscopeChannel::COUPLE_DC_1M;
				else if(imp_reply == "FIFT")
					coupling = OscilloscopeChannel::COUPLE_DC_50;
			}
			lock_guard<recursive_mutex> lock(m_cacheMutex);
			m_channelCouplings[i] = coupling;
			return coupling;
		#endif
		*/
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = coupling;

	// Different couplings imply different bandwidth limits
	m_channelBandwidthLimits.clear();
	return coupling;
}

vector<OscilloscopeChannel::CouplingType> TektronixOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	// ret.push_back(OscilloscopeChannel::COUPLE_GND);
	// TODO: This is not present on the MSO5, and it is not supported in SetChannelCoupling. Remove?
	return ret;
}

void TektronixOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(!IsAnalog(i))
		return;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_DC_50:
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TERM 50");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					if(m_probeTypes[i] == PROBE_TYPE_ANALOG_250K)
						m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TERM 250E3");
					else
						m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TERM 1E+6");
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					if(m_probeTypes[i] == PROBE_TYPE_ANALOG_250K)
						m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TERM 250E3");
					else
						m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
					break;

				default:
					LogError("Invalid coupling for channel\n");
			}
			break;

		default:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_DC_50:
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":IMP FIFT");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":IMP ONEM");
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":IMP ONEM");
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUP DC");
					break;

				default:
					LogError("Invalid coupling for channel\n");
			}
			break;
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelCouplings[i] = type;
}

double TektronixOscilloscope::GetChannelAttenuation(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	//If not analog, return default
	if(!IsAnalog(i))
		return 1;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				float probegain = stof(
					m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":PRO:GAIN?"));
				float extatten = stof(
					m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":PROBEF:EXTA?"));

				//Calculate the overall system attenuation.
				//Note that probes report *gain* while the external is *attenuation*.
				double atten = extatten / probegain;
				m_channelAttenuations[i] = atten;
				return atten;
			}
			break;

		default:
			// FIXME

			/*
			lock_guard<recursive_mutex> lock(m_mutex);

			m_transport->SendCommandImmediate(GetOscilloscopeChannel(i)->GetHwname() + ":PROB?");

			string reply = m_transport->ReadReply();
			double atten;
			sscanf(reply.c_str(), "%lf", &atten);

			lock_guard<recursive_mutex> lock2(m_cacheMutex);
			m_channelAttenuations[i] = atten;
			return atten;
			*/

			return 1.0;
	}
}

void TektronixOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if(!IsAnalog(i))
		return;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
	}

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//This function takes the overall system attenuation as an argument.
				//We need to scale this by the probe gain to figure out the necessary external attenuation.
				//At the moment, this isn't cached, but we probably should do this in the future.
				float probegain = stof(
					m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":PRO:GAIN?"));

				m_transport->SendCommandQueued(
					GetOscilloscopeChannel(i)->GetHwname() + ":PROBEF:EXTA " + to_string(atten * probegain));
			}
			break;

		default:
			//FIXME
			break;
	}
}

unsigned int TektronixOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	//If not analog, return default
	if(!IsAnalog(i))
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	unsigned int bwl = 0;
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				{
					string reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":BAN?");
					if(reply == "FUL")		//no limit
						bwl = 0;
					else
						bwl = stof(reply) * 1e-6;

					//If the returned bandwidth is the same as the instrument's upper bound, report "no limit"
					if(bwl == m_maxBandwidth)
						bwl = 0;
				}
				break;

			default:
				/*
				m_transport->SendCommandImmediate(GetOscilloscopeChannel(i)->GetHwname() + ":BWL?");
				string reply = m_transport->ReadReply();
				int bwl;
				if(reply == "1")
					bwl = 25;
				else
					bwl = 0;
				*/
				break;
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelBandwidthLimits[i] = bwl;
	return bwl;
}

vector<unsigned int> TektronixOscilloscope::GetChannelBandwidthLimiters(size_t i)
{
	//Don't allow bandwidth limits >1 GHz for 1M ohm inputs
	auto coupling = GetChannelCoupling(i);
	bool is_1m = (coupling == OscilloscopeChannel::COUPLE_AC_1M) || (coupling == OscilloscopeChannel::COUPLE_DC_1M);

	vector<unsigned int> ret;

	switch(m_family)
	{
		case FAMILY_MSO5:
			ret.push_back(20);
			ret.push_back(250);

			if (is_1m)
				ret.push_back(500);

			//Only show "unlimited" for 50 ohm channels
			//TODO: Behavior copied from MSO6. Appropriate?
			if (!is_1m)
				ret.push_back(0);

			break;

		case FAMILY_MSO6:

			ret.push_back(20);
			ret.push_back(200);
			ret.push_back(250);
			ret.push_back(350);
			if(!is_1m)
			{
				if(m_maxBandwidth > 1000)
					ret.push_back(1000);
				if(m_maxBandwidth > 2000)
					ret.push_back(2000);
				if(m_maxBandwidth > 2500)
					ret.push_back(2500);
				if(m_maxBandwidth > 3000)
					ret.push_back(3000);
				if(m_maxBandwidth >= 4000)
					ret.push_back(4000);
				if(m_maxBandwidth >= 5000)
					ret.push_back(5000);
				if(m_maxBandwidth >= 6000)
					ret.push_back(6000);
				if(m_maxBandwidth >= 7000)
					ret.push_back(7000);
			}
			else if(m_maxBandwidth >= 1000)
				ret.push_back(1000);

			//Only show "unlimited" for 50 ohm channels
			if(!is_1m)
				ret.push_back(0);

			break;

		default:
			break;
	}

	return ret;
}

void TektronixOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	if(!IsAnalog(i))
		return;

	//Update cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelBandwidthLimits[i] = limit_mhz;
	}

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Instrument wants Hz, not MHz, or "FUL" for no limit
				size_t limit_hz = limit_mhz;
				limit_hz *= 1000 * 1000;

				if(limit_mhz == 0)
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":BAN FUL");
				else
					m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":BAN " + to_string(limit_hz));
			}
			break;

		default:
			break;
	}
}

float TektronixOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	//If not analog, return a placeholder value
	if(!IsAnalog(i) && !IsSpectrum(i))
		return 1;

	//If unusable, return a placeholder value
	if(!CanEnableChannel(i) || !IsChannelEnabled(i))
		return 1;

	//We want total range, not per division
	float range = 1;
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				if(IsSpectrum(i))
				{
					range = 10 * stof(m_transport->SendCommandQueuedWithReply(
						string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) + ":VERT:SCA?"));
				}
				else
				{
					range = 10 * stof(m_transport->SendCommandQueuedWithReply(
						GetOscilloscopeChannel(i)->GetHwname() + ":SCA?"));
				}
				break;

			default:
				break;
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = range;
	return range;
}

void TektronixOscilloscope::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	//Update cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
	}

	//If not analog, skip it
	if(!IsAnalog(i) && !IsSpectrum(i))
		return;

	//If unusable or disabled do nothing
	if(!CanEnableChannel(i) || !IsChannelEnabled(i))
		return;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(IsSpectrum(i))
			{
				float divsize = range/10;
				float offset_div = (GetChannelOffset(i, stream) / divsize) - 5;

				m_transport->SendCommandQueued(string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) +
					":VERT:SCA " + to_string(divsize));

				//This seems to also mess up vertical position, so update that too to keep us centered
				m_transport->SendCommandQueued(string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) +
					":VERT:POS " + to_string(offset_div));
			}
			else
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":SCA " + to_string(range/10));
			break;

		default:
			break;
	}
}

OscilloscopeChannel* TektronixOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

Unit TektronixOscilloscope::GetYAxisUnit(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelUnits.find(i) != m_channelUnits.end())
			return m_channelUnits[i];
	}

	//Only do this for analog channels
	if(i < m_analogChannelCount)
	{
		Unit u(Unit::UNIT_VOLTS);
		auto reply = Trim(m_transport->SendCommandQueuedWithReply(
			GetOscilloscopeChannel(i)->GetHwname() + ":PROBE:UNI?"));

		//Note that the unit is *in quotes*!
		char unit[128] = {0};
		sscanf(reply.c_str(), "\"%127[^\"]", unit);
		string sunit(unit);

		if(sunit == "V")
			u = Unit(Unit::UNIT_VOLTS);
		else if(sunit == "A")
			u = Unit(Unit::UNIT_AMPS);
		else
		{
			LogWarning("Unrecognized unit %s for channel %s\n",
				unit,
				GetOscilloscopeChannel(i)->GetHwname().c_str());
		}

		//Add to cache
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelUnits[i] = u;
		return u;
	}

	else
		return Unit::UNIT_VOLTS;
}

bool TektronixOscilloscope::CanDegauss(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_probeTypes[i] == PROBE_TYPE_ANALOG_CURRENT)
			return true;
	}

	return false;
}

bool TektronixOscilloscope::ShouldDegauss(size_t i)
{
	//No current probe connected? Don't even ask
	if(!CanDegauss(i))
		return false;

	//See if we should degauss
	auto chan = GetOscilloscopeChannel(i);
	auto state = Trim(m_transport->SendCommandQueuedWithReply(chan->GetHwname() + ":PRO:DEGAUSS:STATE?"));

	if(state == "PASS")
		return false;
	else if(state == "FAIL")
		return true;
	//TODO: What are short versions of REQUIRED and RECOMMENDED?
	else if(state[0] == 'R')
		return true;

	return false;
}

void TektronixOscilloscope::Degauss(size_t i)
{
	auto chan = GetOscilloscopeChannel(i);
	m_transport->SendCommandQueued(chan->GetHwname() + ":PRO:DEGAUSS EXEC");
}

string TektronixOscilloscope::GetChannelDisplayName(size_t i)
{
	auto chan = GetOscilloscopeChannel(i);

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return m_extTrigChannel->GetHwname();

	//Spectrum channels don't have separate names from the time domain ones.
	//Store our own nicknames clientside for them.
	string name;

	//If we can't use the channel return the hwname as a placeholder
	if(!CanEnableChannel(i))
		name = chan->GetHwname();

	else if(!IsSpectrum(i))
	{
		switch(m_family)
		{
			//What a shocker!
			//Completely orthogonal design for analog and digital, and it even handles empty strings well!
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				name = TrimQuotes(m_transport->SendCommandQueuedWithReply(chan->GetHwname() + ":LAB:NAM?"));
				break;

			default:
				break;
		}
	}

	//Default to using hwname if no alias defined
	if(name == "")
		name = chan->GetHwname();

	return name;
}

void TektronixOscilloscope::SetChannelDisplayName(size_t i, string name)
{
	auto chan = GetOscilloscopeChannel(i);

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return;

	//Update in hardware if possible (spectrum channels only have clientside naming)
	if(!IsSpectrum(i))
	{
		//Hide the name if we type the channel name, no reason to have two labels
		if(name == chan->GetHwname())
			name = "";

		//Special case: analog channels are named CHx in hardware but displayed as Cx on the scope
		//We want this to be treated as "no name" too.
		if(name == string("C") + to_string(i+1))
			name = "";

		switch(m_family)
		{
			//What a shocker!
			//Completely orthogonal design for analog and digital, and it even handles empty strings well!
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				m_transport->SendCommandQueued(chan->GetHwname() + ":LAB:NAM \"" + name + "\"");
				break;

			default:
				break;
		}
	}
}

float TektronixOscilloscope::GetChannelOffset(size_t i, size_t stream)
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	//If not analog, return a placeholder value
	if(!IsAnalog(i) && !IsSpectrum(i))
		return 0;

	//If unusable, return a placeholder value
	if(!CanEnableChannel(i) || !IsChannelEnabled(i))
		return 0;

	//Read offset
	double offset = 0;
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				if(IsSpectrum(i))
				{
					//Position is reported in divisions, not dBm.
					//It also seems to be negative, and reported from the top of the display rather than the middle.
					float pos = stof(m_transport->SendCommandQueuedWithReply(
						string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) + ":VERT:POS?"));
					offset = (pos+5) * (GetChannelVoltageRange(i, stream)/10);
				}
				else
					offset = -stof(m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":OFFS?"));
				break;

			default:
				break;
		}
	}

	//Update cache
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void TektronixOscilloscope::SetChannelOffset(size_t i, size_t stream, float offset)
{
	//Update cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelOffsets[i] = offset;
	}

	//If not analog, skip it
	if(!IsAnalog(i) && !IsSpectrum(i))
		return;

	//If unusable, do nothing
	if(!CanEnableChannel(i) || !IsChannelEnabled(i))
		return;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(IsSpectrum(i))
			{
				float divsize = GetChannelVoltageRange(i, stream) / 10;
				float offset_div = (offset / divsize) - 5;

				m_transport->SendCommandQueued(string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) +
					":VERT:POS " + to_string(offset_div));
			}
			else
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":OFFS " + to_string(-offset));
			break;

		default:
			break;
	}
}

//Current implementation assumes MSO5/6, unsure about other models
Oscilloscope::TriggerMode TektronixOscilloscope::PollTrigger()
{
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	//If AcquireData() is in progress, block until it's completed before allowing the poll.
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	// Based on example from 6000 Series Programmer's Guide
	// Section 10 'Synchronizing Acquisitions' -> 'Polling Synchronization With Timeout'

	//Note, we need to push all pending commands
	//(to make sure the trigger is armed if we just submitted an arm request).
	m_transport->FlushCommandQueue();
	string ter = m_transport->SendCommandImmediateWithReply("TRIG:STATE?");

	if(ter == "SAV")
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	//Trigger is armed but not yet ready to go
	if(ter == "ARM")
		return TRIGGER_MODE_WAIT;

	if(ter == "REA")
		return TRIGGER_MODE_RUN;

	//TODO: AUTO, TRIGGER. For now consider that same as RUN
	return TRIGGER_MODE_RUN;
}

//Current implementation assumes MSO5/6, unsure about other models
bool TektronixOscilloscope::PeekTriggerArmed()
{
	//If AcquireData() is in progress, block until it's completed before allowing the poll.
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	//Note, we need to push all pending commands
	//(to make sure the trigger is armed if we just submitted an arm request).
	string ter = m_transport->SendCommandQueuedWithReply("TRIG:STATE?");

	if(ter == "REA")
		return true;
	else
		return false;
}

bool TektronixOscilloscope::AcquireData()
{
	//LogDebug("Acquiring data\n");

	map<int, vector<WaveformBase*> > pending_waveforms;

	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	LogIndenter li;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(!AcquireDataMSO56(pending_waveforms))
			{
				//Clean up any partially acquired data
				for(auto it : pending_waveforms)
				{
					auto vec = it.second;
					for(auto w : vec)
						delete w;
				}
				return false;
			}
			break;

		default:
			//m_transport->SendCommandImmediate("WFMPRE:" + GetOscilloscopeChannel(i)->GetHwname() + "?");
				/*
			//		string reply = m_transport->ReadReply();
			//		sscanf(reply.c_str(), "%u,%u,%lu,%u,%lf,%lf,%lf,%lf,%lf,%lf",
			//				&format, &type, &length, &average_count, &xincrement, &xorigin, &xreference, &yincrement, &yorigin, &yreference);

			for(int j=0;j<10;++j)
			m_transport->ReadReply();

			//format = 0;
			//type = 0;
			//average_count = 0;
			xincrement = 1000;
			//xorigin = 0;
			//xreference = 0;
			yincrement = 0.01;
			yorigin = 0;
			yreference = 0;
			length = 500;

			//Figure out the sample rate
			int64_t fs_per_sample = round(xincrement * FS_PER_SECOND);
			//LogDebug("%ld fs/sample\n", fs_per_sample);

			//LogDebug("length = %d\n", length);

			//Set up the capture we're going to store our data into
			//(no TDC data available on Tektronix scopes?)
			AnalogWaveform* cap = new AnalogWaveform;
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECONDf;

			//Ask for the data
			m_transport->SendCommandImmediate("CURV?");

			char tmp[16] = {0};

			//Read the length header
			m_transport->ReadRawData(2, (unsigned char*)tmp);
			tmp[2] = '\0';
			int numDigits = atoi(tmp+1);
			LogDebug("numDigits = %d\n", numDigits);

			// Read the number of points
			m_transport->ReadRawData(numDigits, (unsigned char*)tmp);
			tmp[numDigits] = '\0';
			int numPoints = atoi(tmp);
			LogDebug("numPoints = %d\n", numPoints);

			uint8_t* temp_buf = new uint8_t[numPoints / sizeof(uint8_t)];

			//Read the actual data
			m_transport->ReadRawData(numPoints, (unsigned char*)temp_buf);
			//Discard trailing newline
			m_transport->ReadRawData(1, (unsigned char*)tmp);

			//Format the capture
			cap->Resize(length);
			for(size_t j=0; j<length; j++)
			{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = yincrement * (temp_buf[j] - yreference) + yorigin;
			}

			//Done, update the data
			pending_waveforms[i].push_back(cap);

			//Clean up
			delete[] temp_buf;
			*/
			break;
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	//TODO: segmented capture support
	for(size_t i=0; i<num_pending; i++)
	{
		SequenceSet s;
		for(size_t j=0; j<m_channels.size(); j++)
		{
			if(IsChannelEnabled(j))
				s[GetOscilloscopeChannel(j)] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		lock_guard<recursive_mutex> lock3(m_cacheMutex);
		FlushChannelEnableStates();

		m_transport->SendCommandImmediate("ACQ:STATE ON");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

/**
	@brief Parses a waveform preamble

	@param[in] 	preamble_in		Raw string preamble
	@param[out]	preamble_out	Parsed preamble data
 */
bool TektronixOscilloscope::ReadPreamble(string& preamble_in, mso56_preamble& preamble_out)
{
	size_t semicolons = std::count(preamble_in.begin(), preamble_in.end(), ';');

	int read = 0;
	mso56_preamble& p = preamble_out;

	//New format seen on recent firmware in a demo unit: 24 fields, or 23 with missing WFID
	//but also has "VEC" at the end as a 23rd/24th field
	if(preamble_in.find("VEC") != string::npos)
	{
		if (semicolons == 24)
		{
			read = sscanf(preamble_in.c_str(),
				"%d;%d;%31[^;];%31[^;];%31[^;];%31[^;];%255[^;];%d;%c;%31[^;];"
				"%31[^;];%lf;%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
				&p.byte_num, &p.bit_num, p.encoding, p.bin_format, p.asc_format, p.byte_order, p.wfid,
				&p.nr_pt, p.pt_fmt, p.pt_order, p.xunit, &p.xincrement, &p.xzero,
				&p.pt_off, p.yunit,	&p.ymult, &p.yoff, &p.yzero, p.domain, p.wfmtype, &p.centerfreq, &p.span);

			if (read == 22) return true;
		}
		else if (semicolons == 23)
		{
			read = sscanf(preamble_in.c_str(),
				"%d;%d;%31[^;];%31[^;];%31[^;];%31[^;];%d;%c;%31[^;];" // wfid missing
				"%31[^;];%lf;%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
				&p.byte_num, &p.bit_num, p.encoding, p.bin_format, p.asc_format, p.byte_order,
				&p.nr_pt, p.pt_fmt, p.pt_order, p.xunit, &p.xincrement, &p.xzero,
				&p.pt_off, p.yunit,	&p.ymult, &p.yoff, &p.yzero, p.domain, p.wfmtype, &p.centerfreq, &p.span);
			strcpy(p.wfid, "<missing (Tek bug)>");

			LogDebug("!!Tek decided to not send the WFId in WFMO? preamble. Compensating!!\n");

			if (read == 21) return true;
		}
	}
	else if (semicolons >= 23)
	{
		read = sscanf(preamble_in.c_str(),
			"%d;%d;%31[^;];%31[^;];%31[^;];%31[^;];%255[^;];%d;%c;%31[^;];"
			"%31[^;];%lf;%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
			&p.byte_num, &p.bit_num, p.encoding, p.bin_format, p.asc_format, p.byte_order, p.wfid,
			&p.nr_pt, p.pt_fmt, p.pt_order, p.xunit, &p.xincrement, &p.xzero,
			&p.pt_off, p.yunit,	&p.ymult, &p.yoff, &p.yzero, p.domain, p.wfmtype, &p.centerfreq, &p.span);

		if (read == 22) return true;
	}
	else if (semicolons == 22)
	{
		read = sscanf(preamble_in.c_str(),
			"%d;%d;%31[^;];%31[^;];%31[^;];%31[^;];%d;%c;%31[^;];" // wfid missing
			"%31[^;];%lf;%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
			&p.byte_num, &p.bit_num, p.encoding, p.bin_format, p.asc_format, p.byte_order,
			&p.nr_pt, p.pt_fmt, p.pt_order, p.xunit, &p.xincrement, &p.xzero,
			&p.pt_off, p.yunit,	&p.ymult, &p.yoff, &p.yzero, p.domain, p.wfmtype, &p.centerfreq, &p.span);
		strcpy(p.wfid, "<missing (Tek bug)>");

		LogDebug("!!Tek decided to not send the WFId in WFMO? preamble. Compensating!!\n");

		if (read == 21) return true;
	}
	else
		LogError("Unsupported preamble format (semicolons=%zu)\n", semicolons);

	LogWarning("Preamble error (read only %d fields from %zu semicolons)\n", read, semicolons);
	LogDebug(" -> Failed preamble: %s\n", preamble_in.c_str());
	return false;
}

/**
	@brief Attempt to recover synchronization between ngscopeclient and the scope-side SCPI stack.

	This involves a lot of ugly hacks while attempting to undo behavior the scope should never have had in the first
	place (keeping application layer protocol state across client disconnect/reconnect events).

	We flush the receive buffer twice at the scope side, throw away anything in the socket RX buffer, then send a
	PRBS-3 of two different query commands with predictable output. By cross-correlating the replies with the sent
	commands, we can determine how many lines worth of garbage were in the instrument's TX buffer, discard them,
	and recover sync.

	In theory. It doesn't always work and sometimes things are completely hosed until the scope reboots.
 */
void TektronixOscilloscope::ResynchronizeSCPI()
{
	LogTrace("Resynchronizing\n");
	LogIndenter li;

	m_transport->SendCommandImmediate("*CLS");
	m_transport->SendCommandImmediate("*CLS");
	// The manual has very confusing things to say about this needing to follow an "<EOI>" which
	// appears to be a holdover from IEEE488 that IDK how is supposed to work (or not) over socket
	// transport. Who knows. Another option: set Protocol to Terminal in socket server settings and
	// use '!d' which issues the "DCL (Device CLear) control message" but this means dealing with
	// prompts and stuff like that.

	m_transport->FlushRXBuffer();

	//This is absolutely disgusting but will work as long as there's not more than a few lines worth of desync

	//Send a PRBS-3 encoded as SCPI commands and read back the responses
	const int prbslen = 7;
	int prbs3[prbslen] = {1, 0, 1, 1, 1, 0, 0};
	int replies[prbslen] = {0};
	for(int i=0; i<prbslen; i++)
	{
		string reply;
		if(prbs3[i])
			reply = m_transport->SendCommandQueuedWithReply("*IDN?");	//should return a string starting with "TEKTRONIX"
		else
			reply = m_transport->SendCommandQueuedWithReply("HOR:MODE:RECO?");	//should return a number

		if(reply.find("TEKTRONIX") != string::npos)
			replies[i] = 1;
		else
			replies[i] = 0;
	}

	//Debug print
	LogTrace("PRBS sent: %d%d%d%d%d%d%d\n",
		prbs3[0], prbs3[1], prbs3[2], prbs3[3],
		prbs3[4], prbs3[5], prbs3[6]);
	LogTrace("PRBS got:  %d%d%d%d%d%d%d\n",
		replies[0], replies[1], replies[2], replies[3],
		replies[4], replies[5], replies[6]);

	//Try shifts of up to 3 lines and see if we get a good alignment
	int delta = -1;
	for(int shift=0; shift<3; shift++)
	{
		bool match = true;
		for(int i=0; i<prbslen; i++)
		{
			if(i+shift >= prbslen)
				break;
			if(prbs3[i] != replies[i+shift])
			{
				match = false;
				break;
			}
		}
		if(match)
		{
			delta = shift;
			break;
		}
	}

	if(delta < 0)
	{
		LogError("SCPI resync failed, firmware is probably in a bad state. Try rebooting the scope.\n");
		exit(1);
	}

	if(delta != 0)
	{
		LogDebug("PRBS locked with offset of %d lines, discarding extra replies from buffer\n", delta);
		for(int i=0; i<delta; i++)
		{
			auto reply = m_transport->SendCommandQueuedWithReply("HEAD 0");	//send something so we can fetch a reply
			LogTrace("Extra reply: %s\n", reply.c_str());
		}
	}
	else
		LogTrace("PRBS locked with zero offset\n");

	/*
	//Resynchronize
	LogWarning("SCPI out of sync, attempting to recover\n");
	while(1)
	{
		m_transport->SendCommandImmediate("\n*CLS");
		// The manual has very confusing things to say about this needing to follow an "<EOI>" which
		// appears to be a holdover from IEEE488 that IDK how is supposed to work (or not) over socket
		// transport. Who knows. Another option: set Protocol to Terminal in socket server settings and
		// use '!d' which issues the "DCL (Device CLear) control message" but this means dealing with
		// prompts and stuff like that.

		m_transport->FlushRXBuffer();

		if (IDPing() != "")
			break;
	}
	*/
}

/**
	@brief Acquire data from a MSO5 or MSO6 scope

	@param[out] pending_waveforms 	Map of channel number -> waveform

	@return True on success, false on failure
 */
bool TektronixOscilloscope::AcquireDataMSO56(map<int, vector<WaveformBase*> >& pending_waveforms)
{
	//Seems like we might need a command before reading data after the trigger?
	IDPing();

	//Make sure record length is valid
	GetSampleDepth();

	//Ask for the analog data
	bool firstAnalog = true;
	size_t timebase = 0;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		//If channel is enabled but was just turned on/off, skip this channel
		if(IsEnableStateDirty(i))
		{
			pending_waveforms[i].push_back(NULL);
			continue;
		}

		//Update the Y axis units
		GetOscilloscopeChannel(i)->SetYAxisUnits(GetYAxisUnit(i), 0);

		bool succeeded = false;
		for (int retry = 0; retry < 3; retry++)
		{
			// Set source (before setting format)
			m_transport->SendCommandImmediate(string("DAT:SOU ") + GetOscilloscopeChannel(i)->GetHwname());

			if (firstAnalog || retry) // set again on retry
			{
				m_transport->SendCommandImmediate("DAT:WID 1");					//8-bit data in NORMAL mode
				m_transport->SendCommandImmediate("DAT:ENC SRI");				//signed, little endian binary
				firstAnalog = false;
			}

			//Ask for the waveform preamble
			string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
			mso56_preamble preamble;

			//Process it (grab the whole block, semicolons and all)
			if (!ReadPreamble(preamble_str, preamble))
				continue; // retry

			timebase = preamble.xincrement * FS_PER_SECOND;	//scope gives sec, not fs
			m_channelOffsets[i] = -preamble.yoff;

			//LogDebug("Channel %zu (%s)\n", i, GetOscilloscopeChannel(i)->GetHwname().c_str());
			LogIndenter li2;

			//Read the data block
			size_t nsamples;
			int8_t* samples = (int8_t*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", nsamples);
			if(samples == NULL)
			{
				LogWarning("Didn't get any samples (timeout?)\n");

				ResynchronizeSCPI();

				continue; // retry
			}

			if (nsamples != (size_t)preamble.nr_pt)
			{
				LogWarning("Didn't get the right number of points\n");

				ResynchronizeSCPI();

				delete[] samples;

				continue; // retry
			}

			//Set up the capture we're going to store our data into
			//(no TDC data or fine timestamping available on Tektronix scopes?)
			auto cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(i)->GetHwname());
			cap->m_timescale = timebase;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
			cap->Resize(nsamples);
			cap->PrepareForCpuAccess();

			Convert8BitSamples(
				cap->m_samples.GetCpuPointer(),
				samples,
				preamble.ymult,
				-preamble.yoff,
				nsamples);

			cap->MarkSamplesModifiedFromCpu();

			//Done, update the data
			pending_waveforms[i].push_back(cap);

			//Done
			delete[] samples;

			//Throw out garbage at the end of the message (why is this needed?)
			if (m_transport->ReadReply() != "")
				LogWarning("Tek has junk after CURV? reply\n");

			succeeded = true;
			break;
		}

		if (!succeeded)
		{
			LogError("Retried too many times acquiring channel\n");
			return false;
		}
	}

	//Get the spectrum stuff
	bool firstSpectrum = true;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		auto nchan = m_spectrumChannelBase + i;
		if(!IsChannelEnabled(nchan))
			continue;

		//If channel is enabled but was just turned on/off, skip this channel
		if(IsEnableStateDirty(i))
		{
			pending_waveforms[nchan].push_back(NULL);
			continue;
		}

		bool succeeded = false;
		for (int retry = 0; retry < 3; retry++)
		{
			// Set source (before setting format)
			m_transport->SendCommandImmediate(string("DAT:SOU ") + GetOscilloscopeChannel(i)->GetHwname() + "_SV_NORMAL");

			//Select mode
			if(firstSpectrum || retry) // set again on retry
			{
				m_transport->SendCommandImmediate("DAT:WID 8");					//double precision floating point data
				m_transport->SendCommandImmediate("DAT:ENC SFPB");				//IEEE754 float
				firstSpectrum = false;
			}

			//Ask for the waveform preamble
			string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
			mso56_preamble preamble;

			//LogDebug("Channel %zu (%s)\n", nchan, m_channels[nchan]->GetHwname().c_str());
			//LogIndenter li2;

			//Process it
			if (!ReadPreamble(preamble_str, preamble))
				continue; // retry

			m_channelOffsets[i] = -preamble.yoff;

			//Read the data block
			size_t msglen;
			double* samples = (double*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
			if(samples == NULL)
			{
				LogWarning("Didn't get any samples (timeout?)\n");

				ResynchronizeSCPI();

				continue; // retry
			}

			size_t nsamples = msglen/8;

			if (nsamples != (size_t)preamble.nr_pt)
			{
				LogWarning("Didn't get the right number of points\n");

				ResynchronizeSCPI();

				delete[] samples;

				continue; // retry
			}

			//Set up the capture we're going to store our data into
			//(no TDC data or fine timestamping available on Tektronix scopes?)
			auto cap = new UniformAnalogWaveform;
			cap->m_timescale = preamble.hzbase;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
			cap->Resize(nsamples);
			cap->PrepareForCpuAccess();

			//We get dBm from the instrument, so just have to convert double to single precision
			//TODO: are other units possible here?
			//int64_t ibase = preamble.hzoff / preamble.hzbase;
			for(size_t j=0; j<nsamples; j++)
				cap->m_samples[j] = preamble.ymult*samples[j] + preamble.yoff;

			//Done, update the data
			cap->MarkSamplesModifiedFromCpu();
			pending_waveforms[nchan].push_back(cap);

			//Done
			delete[] samples;

			//Throw out garbage at the end of the message (why is this needed?)
			m_transport->ReadReply();

			//Look for peaks
			//TODO: make this configurable, for now 1 MHz spacing and up to 10 peaks
			dynamic_cast<SpectrumChannel*>(m_channels[nchan])->FindPeaks(cap, 10, 1000000);

			succeeded = true;
			break;
		}

		if (!succeeded)
		{
			LogError("Retried too many times acquiring channel\n");
			return false;
		}
	}

	//Get the digital stuff
	bool firstDigital = true;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		//Skip anything without a digital probe connected
		if(m_probeTypes[i] != PROBE_TYPE_DIGITAL_8BIT)
		{
			for(size_t j=0; j<8; j++)
				pending_waveforms[m_digitalChannelBase + i*8 + j].push_back(NULL);
			continue;
		}

		//Only grab waveform if at least one channel is enabled
		bool enabled = false;
		for(size_t j=0; j<8; j++)
		{
			size_t nchan = m_digitalChannelBase + i*8 + j;
			if(IsChannelEnabled(nchan))
			{
				enabled = true;
				break;
			}
		}
		if(!enabled)
			continue;

		//If channel is enabled but was just turned on/off, skip this channel
		if(IsEnableStateDirty(i))
		{
			for(int j=0; j<8; j++)
				pending_waveforms[m_digitalChannelBase + i*8 + j].push_back(NULL);
			continue;
		}

		bool succeeded = false;
		for (int retry = 0; retry < 3; retry++)
		{
			//Set source (before setting format); Ask for all of the data
			m_transport->SendCommandImmediate(string("DAT:SOU CH") + to_string(i+1) + "_DALL");

			//Configuration
			if(firstDigital || retry) // set again on retry
			{
				m_transport->SendCommandImmediate("DAT:WID 1");					//8 data bits per channel
				m_transport->SendCommandImmediate("DAT:ENC SRI");				//signed, little endian binary
				firstDigital = false;
			}

			//Ask for the waveform preamble
			string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
			mso56_preamble preamble;

			if (!ReadPreamble(preamble_str, preamble))
				continue; // retry

			timebase = preamble.xincrement * FS_PER_SECOND;	//scope gives sec, not fs

			//And the acutal data
			size_t msglen;
			char* samples = (char*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
			if(samples == NULL)
			{
				LogWarning("Didn't get any samples (timeout?)\n");

				ResynchronizeSCPI();

				continue; // retry
			}

			if (msglen != (size_t)preamble.nr_pt)
			{
				LogWarning("Didn't get the right number of points\n");

				ResynchronizeSCPI();

				delete[] samples;

				continue; // retry
			}

			//Process the data for each channel
			for(int j=0; j<8; j++)
			{
				//Set up the capture we're going to store our data into
				//(no TDC data or fine timestamping available on Tektronix scopes?)
				auto cap = new SparseDigitalWaveform;
				cap->m_timescale = timebase;
				cap->m_triggerPhase = 0;
				cap->m_startTimestamp = time(NULL);
				double t = GetTime();
				cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
				cap->Resize(msglen);
				cap->PrepareForCpuAccess();

				//Extract sample data
				int mask = (1 << j);

				bool last = (samples[0] & mask) ? true : false;

				cap->m_offsets[0] = 0;
				cap->m_durations[0] = 1;
				cap->m_samples[0] = last;

				size_t k = 0;

				for(size_t m=1; m<msglen; m++)
				{
					bool sample = (samples[m] & mask) ? true : false;

					//Deduplicate consecutive samples with same value
					//FIXME: temporary workaround for rendering bugs
					//if(last == sample)
					if( (last == sample) && ((m+5) < msglen) && (m > 5))
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

				//Done, update the data
				pending_waveforms[m_digitalChannelBase + i*8 + j].push_back(cap);
			}

			//Done
			delete[] samples;

			//Throw out garbage at the end of the message (why is this needed?)
			m_transport->ReadReply();

			succeeded = true;
			break;
		}

		if (!succeeded)
		{
			LogError("Retried too many times acquiring channel\n");
			return false;
		}
	}
	return true;
}

/**
	@brief Checks if the channel enable state of a channel is up to date

	@param chan	Channel index

	@return	True if up to date, false if not
 */
bool TektronixOscilloscope::IsEnableStateDirty(size_t chan)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelEnableStatusDirty.find(chan) != m_channelEnableStatusDirty.end();
}

/**
	@brief Flushes pending channel enable/disable commands
 */
void TektronixOscilloscope::FlushChannelEnableStates()
{
	//Push all previous commands to the scope, then mark channel enable states as up to date
	m_transport->FlushCommandQueue();
	m_channelEnableStatusDirty.clear();
}

void TektronixOscilloscope::Start()
{
	//Flush enable states with the cache mutex locked.
	//This is necessary to ensure the scope's view of what's enabled is consistent with ours at trigger time.
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	FlushChannelEnableStates();

	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	FlushChannelEnableStates();

	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_transport->SendCommandQueued("ACQ:STATE STOP");
	m_triggerOneShot = true;
}

void TektronixOscilloscope::ForceTrigger()
{
	m_triggerArmed = true;
	m_transport->SendCommandQueued("TRIG FORC");
	m_triggerOneShot = true;
}

bool TektronixOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> TektronixOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	const int64_t k = 1000;
	const int64_t m = k*k;
	const int64_t g = k*m;

	uint64_t bases[] = { 1000, 1250, 2500, 3125, 5000, 6250 };
	vector<uint64_t> scales = {1, 10, 100, 1*k};

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				for(auto b : bases)
					ret.push_back(b / 10);

				for(auto scale : scales)
				{
					for(auto b : bases)
						ret.push_back(b * scale);
				}

				// MSO6 also supports these, or at least had them available in the picker before.
				// TODO: Are these actually supported?

				if (m_family == FAMILY_MSO6) {
					for(auto b : bases) {
						ret.push_back(b * 10 * k);
					}
				}

				// We break with the pattern on the upper end of the frequency range
				ret.push_back(12500 * k);
				ret.push_back(25 * m);
				ret.push_back(31250 * k);
				ret.push_back(62500 * k);
				ret.push_back(125 * m);
				ret.push_back(250 * m);
				ret.push_back(312500 * k);
				ret.push_back(625 * m);
				ret.push_back(1250 * m);
				ret.push_back(1562500 * k);
				ret.push_back(3125 * m);
				ret.push_back(6250 * m);
				ret.push_back(12500 * m);

				// Below are interpolated. 8 bits, not 12.
				//TODO: we can save bandwidth by using 8 bit waveform download for these

				ret.push_back(25 * g);

				// MSO5 supports these, TODO: Does MSO6?
				if (m_family == FAMILY_MSO5) {
					ret.push_back(25000 * m);
					ret.push_back(62500 * m);
					ret.push_back(125000 * m);
					ret.push_back(250000 * m);
					ret.push_back(500000 * m);
				}

			}
			break;

		default:
			break;
	}

	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleRatesInterleaved()
{
	//MSO5/6 have no interleaving
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> TektronixOscilloscope::GetInterleaveConflicts()
{
	set<Oscilloscope::InterleaveConflict> ret;

	switch(m_family)
	{
		//MSO5/6 have no interleaving.
		//Every channel conflicts with itself
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				for(size_t i=0; i<m_analogChannelCount; i++)
					ret.emplace(InterleaveConflict(GetOscilloscopeChannel(i), GetOscilloscopeChannel(i)));
			}
			break;

		default:
			break;
	}

	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

	const int64_t k = 1000;
	const int64_t m = k*k;

	switch(m_family)
	{
		//The scope allows extremely granular specification of memory depth.
		//For our purposes, only show a bunch of common step values.
		//No need for super fine granularity since record length isn't tied to the UI display width.
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				ret.push_back(500);
				ret.push_back(1 * k);
				ret.push_back(2 * k);
				ret.push_back(5 * k);
				ret.push_back(10 * k);
				ret.push_back(20 * k);
				ret.push_back(50 * k);
				ret.push_back(100 * k);
				ret.push_back(200 * k);
				ret.push_back(500 * k);

				ret.push_back(1 * m);
				ret.push_back(2 * m);
				ret.push_back(5 * m);
				ret.push_back(10 * m);
				ret.push_back(20 * m);
				ret.push_back(50 * m);
				ret.push_back(62500 * k);
				ret.push_back(100 * m);
				ret.push_back(125 * m);
			}
			break;

		default:
			break;
	}

	return ret;
}

vector<uint64_t> TektronixOscilloscope::GetSampleDepthsInterleaved()
{
	//MSO5/6 have no interleaving
	return GetSampleDepthsNonInterleaved();
}

uint64_t TektronixOscilloscope::GetSampleRate()
{
	//don't bother with mutexing here, worst case we return slightly stale data
	if(m_sampleRateValid)
		return m_sampleRate;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

			//stoull seems to not handle scientific notation
			m_sampleRate = stod(m_transport->SendCommandQueuedWithReply("HOR:MODE:SAMPLER?"));

			break;

		default:
			return 1;
	}

	m_sampleRateValid = true;
	return m_sampleRate;
}

uint64_t TektronixOscilloscope::GetSampleDepth()
{
	//don't bother with mutexing here, worst case we return slightly stale data
	if(m_sampleDepthValid)
		return m_sampleDepth;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_sampleDepth = stos(m_transport->SendCommandQueuedWithReply("HOR:MODE:RECO?"));
			m_transport->SendCommandQueued("DAT:START 0");
			m_transport->SendCommandQueued(string("DAT:STOP ") + to_string(m_sampleDepth));
			break;

		default:
			return 1;
	}

	m_sampleDepthValid = true;
	return m_sampleDepth;
}

void TektronixOscilloscope::SetSampleDepth(uint64_t depth)
{
	//Update the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_sampleDepth = depth;
		m_sampleDepthValid = true;

		for (size_t i = 0; i < m_analogChannelCount; i++)
		{
			if (IsChannelEnabled(m_spectrumChannelBase + i))
			{
				depth = min<size_t>(depth, 62500 * 1000);
				// Having a spectrum channel enabled silently caps the depth to 62.5Mpts.
				// Setting it higher via SCPI "works" but does really weird stuff to the
				// hdiv that breaks us, so cap.
			}
		}
	}

	//Send it
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued(string("HOR:MODE:RECO ") + to_string(depth));
			m_transport->SendCommandQueued("DAT:START 0");
			m_transport->SendCommandQueued(string("DAT:STOP ") + to_string(depth));
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::SetSampleRate(uint64_t rate)
{
	//Update the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_sampleRate = rate;
		m_sampleRateValid = true;
	}

	//Send it
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued(string("HOR:MODE:SAMPLER ") + to_string(rate));
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::SetTriggerOffset(int64_t offset)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
		{
			//Instrument reports position of trigger from the midpoint of the display
			//but we want to know position from the start of the capture
			double capture_len_sec = 1.0 * GetSampleDepth() / GetSampleRate();
			double offset_sec = offset * SECONDS_PER_FS;
			double center_offset_sec = capture_len_sec/2 - offset_sec;

			m_transport->SendCommandQueued(string("HOR:DELAY:TIME ") + to_string(center_offset_sec));

			//Don't update the cache because the scope is likely to round the offset we ask for.
			//If we query the instrument later, the cache will be updated then.
			m_triggerOffsetValid = false;
		}

		default:
			break;
	}
}

int64_t TektronixOscilloscope::GetTriggerOffset()
{
	if(m_triggerOffsetValid)
		return m_triggerOffset;

	//Shouldn't be necessary but the scope is derpy...
	m_transport->FlushCommandQueue();

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
		{
			//Instrument reports position of trigger from the midpoint of the display
			double center_offset_sec = stod(m_transport->SendCommandQueuedWithReply("HOR:DELAY:TIME?"));

			//but we want to know position from the start of the capture
			double capture_len_sec = 1.0 * GetSampleDepth() / GetSampleRate();
			double offset_sec = capture_len_sec/2 - center_offset_sec;

			//All good, convert to fs and we're done
			m_triggerOffset = round(offset_sec * FS_PER_SECOND);
			m_triggerOffsetValid = true;
			return m_triggerOffset;
		}

		default:
			return 0;
	}
}

void TektronixOscilloscope::SetDeskewForChannel(size_t channel, int64_t skew)
{
	//Don't update the cache because the scope is likely to round the offset we ask for.
	//If we query the instrument later, the cache will be updated then.
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		auto it = m_channelDeskew.find(channel);
		if(it != m_channelDeskew.end())
			m_channelDeskew.erase(it);
	}

	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			//Tek's skew convention has positive values move the channel EARLIER, so we need to flip sign
			m_transport->SendCommandQueued(GetOscilloscopeChannel(channel)->GetHwname() + ":DESK " + to_string(-skew) + "E-15");
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::EnableTriggerOutput()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
				m_transport->SendCommandQueued("AUX:SOU ATRIG");
				m_transport->SendCommandQueued("AUX:EDGE RIS");
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::SetUseExternalRefclk(bool external)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(external)
				m_transport->SendCommandQueued("ROSC:SOU EXT");
			else
				m_transport->SendCommandQueued("ROSC:SOU INTER");
			break;

		default:
			break;
	}
}

int64_t TektronixOscilloscope::GetDeskewForChannel(size_t channel)
{
	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return 0;

	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDeskew.find(channel) != m_channelDeskew.end())
			return m_channelDeskew[channel];
	}

	int64_t deskew = 0;
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				//Tek's skew convention has positive values move the channel EARLIER, so we need to flip sign
				deskew = -round(FS_PER_SECOND * stof(
					m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(channel)->GetHwname() + ":DESK?")));
				break;

			default:
				break;
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelDeskew[channel] = deskew;
	return deskew;
}

bool TektronixOscilloscope::IsInterleaving()
{
	//MSO5/6 have no interleaving
	return false;
}

bool TektronixOscilloscope::SetInterleaving(bool /*combine*/)
{
	//MSO5/6 have no interleaving
	return false;
}

vector<string> TektronixOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(DropoutTrigger::GetTriggerName());
	ret.push_back(EdgeTrigger::GetTriggerName());
	ret.push_back(PulseWidthTrigger::GetTriggerName());
	ret.push_back(RuntTrigger::GetTriggerName());
	ret.push_back(SlewRateTrigger::GetTriggerName());
	ret.push_back(WindowTrigger::GetTriggerName());
	return ret;
}

void TektronixOscilloscope::PullTrigger()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				string reply = m_transport->SendCommandQueuedWithReply("TRIG:A:TYP?");

				if(reply == "EDG")
					PullEdgeTrigger();
				else if(reply == "WID")
					PullPulseWidthTrigger();
				else if(reply == "TIMEO")
					PullDropoutTrigger();
				else if(reply == "RUN")
					PullRuntTrigger();
				else if(reply == "TRAN")
					PullSlewRateTrigger();
				else if(reply == "WIN")
					PullWindowTrigger();
				else
				{
					LogWarning("Unknown trigger type %s\n", reply.c_str());
					delete m_trigger;
					m_trigger = NULL;
				}
			}
			break;

		default:
			LogWarning("PullTrigger() not implemented for this scope family\n");
			break;
	}
}

/**
	@brief Determine the current trigger level

	@param chan	The channel selected as the trigger source

	@return	The current trigger level, in volts
 */
float TektronixOscilloscope::ReadTriggerLevelMSO56(OscilloscopeChannel* chan)
{
	string reply;

	if(chan == m_extTrigChannel)
		reply = m_transport->SendCommandQueuedWithReply("TRIG:AUXLEV?", false);
	else
		reply = m_transport->SendCommandQueuedWithReply("TRIG:A:LEV?", false);

	size_t off = reply.find(";");
	if(off != string::npos)
		reply = reply.substr(0, off);

	return stof(reply);
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void TektronixOscilloscope::PullEdgeTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new EdgeTrigger(this);
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				auto reply = m_transport->SendCommandQueuedWithReply("TRIG:A:EDGE:SOU?");
				et->SetInput(0, StreamDescriptor(GetOscilloscopeChannelByHwName(reply), 0), true);

				//Trigger level
				et->SetLevel(ReadTriggerLevelMSO56(GetOscilloscopeChannelByHwName(reply)));

				//Edge slope
				reply = m_transport->SendCommandQueuedWithReply("TRIG:A:EDGE:SLO?");
				if(reply == "RIS")
					et->SetType(EdgeTrigger::EDGE_RISING);
				else if(reply == "FALL")
					et->SetType(EdgeTrigger::EDGE_FALLING);
				else if(reply == "EIT")
					et->SetType(EdgeTrigger::EDGE_ANY);
			}
			break;

		default:
			/*
			//Check cache
			//No locking, worst case we return a result a few seconds old
			if(m_triggerChannelValid)
				return m_triggerChannel;

			lock_guard<recursive_mutex> lock(m_mutex);

			//Look it up
			m_transport->SendCommandImmediate("TRIG:SOUR?");
			string ret = m_transport->ReadReply();

			if(ret.find("CHAN") == 0)
			{
				m_triggerChannelValid = true;
				m_triggerChannel = atoi(ret.c_str()+4) - 1;
				return m_triggerChannel;
			}
			else if(ret == "EXT")
			{
				m_triggerChannelValid = true;
				m_triggerChannel = m_extTrigChannel->GetIndex();
				return m_triggerChannel;
			}
			else
			{
				m_triggerChannelValid = false;
				LogWarning("Unknown trigger source %s\n", ret.c_str());
				return 0;
			}

			//Check cache.
			//No locking, worst case we return a just-invalidated (but still fresh-ish) result.
			if(m_triggerLevelValid)
				return m_triggerLevel;

			lock_guard<recursive_mutex> lock(m_mutex);

			m_transport->SendCommandImmediate("TRIG:LEV?");
			string ret = m_transport->ReadReply();

			double level;
			sscanf(ret.c_str(), "%lf", &level);
			m_triggerLevel = level;
			m_triggerLevelValid = true;
			return level;
			*/
			break;
	}
}

/**
	@brief Reads settings for a pulse width trigger from the instrument
 */
void TektronixOscilloscope::PullPulseWidthTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<PulseWidthTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new PulseWidthTrigger(this);
	PulseWidthTrigger* et = dynamic_cast<PulseWidthTrigger*>(m_trigger);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				auto reply = m_transport->SendCommandQueuedWithReply("TRIG:A:PULSEW:SOU?");
				et->SetInput(0, StreamDescriptor(GetOscilloscopeChannelByHwName(reply), 0), true);

				//TODO: TRIG:A:PULSEW:LOGICQUAL?

				//Trigger level
				et->SetLevel(ReadTriggerLevelMSO56(GetOscilloscopeChannelByHwName(reply)));

				Unit fs(Unit::UNIT_FS);
				et->SetUpperBound(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:PULSEW:HIGHL?")));
				et->SetLowerBound(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:PULSEW:LOWL?")));

				//Edge slope
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:PULSEW:POL?"));
				if(reply == "POS")
					et->SetType(EdgeTrigger::EDGE_RISING);
				else if(reply == "NEG")
					et->SetType(EdgeTrigger::EDGE_FALLING);

				//Condition
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:PULSEW:WHE?"));
				if(reply == "LESS")
					et->SetCondition(Trigger::CONDITION_LESS);
				if(reply == "MORE")
					et->SetCondition(Trigger::CONDITION_GREATER);
				else if(reply == "EQ")
					et->SetCondition(Trigger::CONDITION_EQUAL);
				else if(reply == "UNEQ")
					et->SetCondition(Trigger::CONDITION_NOT_EQUAL);
				else if(reply == "WIT")
					et->SetCondition(Trigger::CONDITION_BETWEEN);
				else if(reply == "OUT")
					et->SetCondition(Trigger::CONDITION_NOT_BETWEEN);
			}
			break;

		default:
			break;
	}
}


/**
	@brief Reads settings for a dropout trigger from the instrument

	Note that Tek's UI calls it "timeout" not "dropout" but the functionality is the same.
 */
void TektronixOscilloscope::PullDropoutTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<DropoutTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new DropoutTrigger(this);
	DropoutTrigger* et = dynamic_cast<DropoutTrigger*>(m_trigger);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				auto reply = m_transport->SendCommandQueuedWithReply("TRIG:A:TIMEO:SOU?");
				et->SetInput(0, StreamDescriptor(GetOscilloscopeChannelByHwName(reply), 0), true);

				//Trigger level
				et->SetLevel(ReadTriggerLevelMSO56(GetOscilloscopeChannelByHwName(reply)));

				Unit fs(Unit::UNIT_FS);
				et->SetDropoutTime(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:TIMEO:TIM?")));

				//TODO: TRIG:A:TIMEO:LOGICQUAL?

				//Edge slope
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TIMEO:POL?"));
				if(reply == "STAYSH")
					et->SetType(DropoutTrigger::EDGE_RISING);
				else if(reply == "STAYSL")
					et->SetType(DropoutTrigger::EDGE_FALLING);
				else if(reply == "EIT")
					et->SetType(DropoutTrigger::EDGE_ANY);
			}
			break;

		default:
			break;
	}
}

/**
	@brief Reads settings for a runt trigger from the instrument
 */
void TektronixOscilloscope::PullRuntTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<RuntTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new RuntTrigger(this);
	RuntTrigger* et = dynamic_cast<RuntTrigger*>(m_trigger);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				auto reply = m_transport->SendCommandQueuedWithReply("TRIG:A:RUNT:SOU?");
				et->SetInput(0, StreamDescriptor(GetOscilloscopeChannelByHwName(reply), 0), true);

				//Trigger level
				auto chname = reply;
				et->SetLowerBound(stof(m_transport->SendCommandQueuedWithReply(
					string("TRIG:A:LOW:") + chname + "?")));
				et->SetUpperBound(stof(m_transport->SendCommandQueuedWithReply(
					string("TRIG:A:UPP:") + chname + "?")));

				//Match condition
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:RUNT:WHE?"));
				if(reply == "LESS")
					et->SetCondition(Trigger::CONDITION_LESS);
				else if(reply == "MORE")
					et->SetCondition(Trigger::CONDITION_GREATER);
				else if(reply == "EQ")
					et->SetCondition(Trigger::CONDITION_EQUAL);
				else if(reply == "UNEQ")
					et->SetCondition(Trigger::CONDITION_NOT_EQUAL);
				else if(reply == "OCCURS")
					et->SetCondition(Trigger::CONDITION_ANY);

				//Only lower interval supported, no upper
				Unit fs(Unit::UNIT_FS);
				et->SetLowerInterval(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:RUNT:WID?")));

				//TODO: TRIG:A:RUNT:LOGICQUAL?

				//Edge slope
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:RUNT:POL?"));
				if(reply == "POS")
					et->SetSlope(RuntTrigger::EDGE_RISING);
				else if(reply == "NEG")
					et->SetSlope(RuntTrigger::EDGE_FALLING);
				else if(reply == "EIT")
					et->SetSlope(RuntTrigger::EDGE_ANY);
			}
			break;

		default:
			break;
	}
}

/**
	@brief Reads settings for a slew rate trigger from the instrument
 */
void TektronixOscilloscope::PullSlewRateTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<SlewRateTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new SlewRateTrigger(this);
	SlewRateTrigger* et = dynamic_cast<SlewRateTrigger*>(m_trigger);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				auto reply = m_transport->SendCommandQueuedWithReply("TRIG:A:TRAN:SOU?");
				et->SetInput(0, StreamDescriptor(GetOscilloscopeChannelByHwName(reply), 0), true);

				//Trigger level
				auto chname = reply;
				et->SetLowerBound(stof(m_transport->SendCommandQueuedWithReply(
					string("TRIG:A:LOW:") + chname + "?")));
				et->SetUpperBound(stof(m_transport->SendCommandQueuedWithReply(
					string("TRIG:A:UPP:") + chname + "?")));

				//Match condition
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TRAN:WHE?"));
				if(reply == "FAST")
					et->SetCondition(Trigger::CONDITION_LESS);
				else if(reply == "SLOW")
					et->SetCondition(Trigger::CONDITION_GREATER);
				else if(reply == "EQ")
					et->SetCondition(Trigger::CONDITION_EQUAL);
				else if(reply == "UNEQ")
					et->SetCondition(Trigger::CONDITION_NOT_EQUAL);

				//Only lower interval supported, no upper
				Unit fs(Unit::UNIT_FS);
				et->SetLowerInterval(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:TRAN:DELT?")));

				//TODO: TRIG:A:TRAN:LOGICQUAL?

				//Edge slope
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:TRAN:POL?"));
				if(reply == "POS")
					et->SetSlope(SlewRateTrigger::EDGE_RISING);
				else if(reply == "NEG")
					et->SetSlope(SlewRateTrigger::EDGE_FALLING);
				else if(reply == "EIT")
					et->SetSlope(SlewRateTrigger::EDGE_ANY);
			}
			break;

		default:
			break;
	}
}

/**
	@brief Reads settings for a window trigger from the instrument
 */
void TektronixOscilloscope::PullWindowTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<WindowTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new WindowTrigger(this);
	WindowTrigger* et = dynamic_cast<WindowTrigger*>(m_trigger);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				auto reply = m_transport->SendCommandQueuedWithReply("TRIG:A:WIN:SOU?");
				et->SetInput(0, StreamDescriptor(GetOscilloscopeChannelByHwName(reply), 0), true);

				//Trigger level
				auto chname = reply;
				et->SetLowerBound(stof(m_transport->SendCommandQueuedWithReply(
					string("TRIG:A:LOW:") + chname + "?")));
				et->SetUpperBound(stof(m_transport->SendCommandQueuedWithReply(
					string("TRIG:A:UPP:") + chname + "?")));

				//TODO: TRIG:A:WIN:LOGICQUAL?

				//Crossing direction (only used for inside/outside greater)
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:WIN:CROSSI?"));
				if(reply == "UPP")
					et->SetCrossingDirection(WindowTrigger::CROSS_UPPER);
				else if(reply == "LOW")
					et->SetCrossingDirection(WindowTrigger::CROSS_LOWER);
				else if(reply == "EIT")
					et->SetCrossingDirection(WindowTrigger::CROSS_EITHER);
				else if(reply == "NON")
					et->SetCrossingDirection(WindowTrigger::CROSS_NONE);

				//Match condition
				reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:A:WIN:WHE?"));
				if(reply == "ENTERSW")
					et->SetWindowType(WindowTrigger::WINDOW_ENTER);
				else if(reply == "EXITSW")
					et->SetWindowType(WindowTrigger::WINDOW_EXIT);
				else if(reply == "INSIDEG")
					et->SetWindowType(WindowTrigger::WINDOW_EXIT_TIMED);
				else if(reply == "OUTSIDEG")
					et->SetWindowType(WindowTrigger::WINDOW_ENTER_TIMED);

				//Only lower interval supported, no upper
				Unit fs(Unit::UNIT_FS);
				et->SetWidth(fs.ParseString(m_transport->SendCommandQueuedWithReply("TRIG:A:WIN:WID?")));
			}
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto dt = dynamic_cast<DropoutTrigger*>(m_trigger);
	auto rt = dynamic_cast<RuntTrigger*>(m_trigger);
	auto st = dynamic_cast<SlewRateTrigger*>(m_trigger);
	auto wt = dynamic_cast<WindowTrigger*>(m_trigger);
	if(pt)
		PushPulseWidthTrigger(pt);
	else if(dt)
		PushDropoutTrigger(dt);
	else if(rt)
		PushRuntTrigger(rt);
	else if(st)
		PushSlewRateTrigger(st);
	else if(wt)
		PushWindowTrigger(wt);

	//needs to be last, since pulse width and other more specialized types should be checked first
	//but are also derived from EdgeTrigger
	else if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Push the current trigger voltage level to hardware

	@param trig	The trigger to push
 */
void TektronixOscilloscope::SetTriggerLevelMSO56(Trigger* trig)
{
	auto chan = trig->GetInput(0).m_channel;

	if(chan == m_extTrigChannel)
		m_transport->SendCommandQueued(string("TRIG:AUXLEVEL ") + to_string_sci(trig->GetLevel()));
	else
	{
		m_transport->SendCommandQueued(
			string("TRIG:A:LEV:") + chan->GetHwname() + " " + to_string_sci(trig->GetLevel()));
	}
}

/**
	@brief Pushes settings for an edge trigger to the instrument

	@param trig	The trigger to push
 */
void TektronixOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommandQueued("TRIG:A:TYP EDGE");

				m_transport->SendCommandQueued(string("TRIG:A:EDGE:SOU ") + trig->GetInput(0).m_channel->GetHwname());
				SetTriggerLevelMSO56(trig);

				switch(trig->GetType())
				{
					case EdgeTrigger::EDGE_RISING:
						m_transport->SendCommandQueued("TRIG:A:EDGE:SLO RIS");
						break;

					case EdgeTrigger::EDGE_FALLING:
						m_transport->SendCommandQueued("TRIG:A:EDGE:SLO FALL");
						break;

					case EdgeTrigger::EDGE_ANY:
						m_transport->SendCommandQueued("TRIG:A:EDGE:SLO ANY");
						break;

					default:
						break;
				}
			}
			break;

		default:
			{
				char tmp[32];
				snprintf(tmp, sizeof(tmp), "TRIG:LEV %.3f", trig->GetLevel());
				m_transport->SendCommandQueued(tmp);
			}
			break;
	}
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument

	@param trig	The trigger to push
 */
void TektronixOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommandQueued("TRIG:A:TYP WID");

				m_transport->SendCommandQueued(string("TRIG:A:PULSEW:SOU ") + trig->GetInput(0).m_channel->GetHwname());
				SetTriggerLevelMSO56(trig);

				m_transport->SendCommandQueued(string("TRIG:A:PULSEW:HIGHL ") +
					to_string_sci(trig->GetUpperBound() * SECONDS_PER_FS));
				m_transport->SendCommandQueued(string("TRIG:A:PULSEW:LOWL ") +
					to_string_sci(trig->GetLowerBound() * SECONDS_PER_FS));

				if(trig->GetType() == EdgeTrigger::EDGE_RISING)
					m_transport->SendCommandQueued("TRIG:A:PULSEW:POL POS");
				else
					m_transport->SendCommandQueued("TRIG:A:PULSEW:POL NEG");

				switch(trig->GetCondition())
				{
					case Trigger::CONDITION_LESS:
						m_transport->SendCommandQueued("TRIG:A:PULSEW:WHE LESS");
						break;

					case Trigger::CONDITION_GREATER:
						m_transport->SendCommandQueued("TRIG:A:PULSEW:WHE MORE");
						break;

					case Trigger::CONDITION_EQUAL:
						m_transport->SendCommandQueued("TRIG:A:PULSEW:WHE EQ");
						break;

					case Trigger::CONDITION_NOT_EQUAL:
						m_transport->SendCommandQueued("TRIG:A:PULSEW:WHE UNEQ");
						break;

					case Trigger::CONDITION_BETWEEN:
						m_transport->SendCommandQueued("TRIG:A:PULSEW:WHE WIT");
						break;

					case Trigger::CONDITION_NOT_BETWEEN:
						m_transport->SendCommandQueued("TRIG:A:PULSEW:WHE OUT");
						break;

					default:
						break;
				}
			}
			break;

		default:
			break;
	}
}

/**
	@brief Pushes settings for a dropout trigger to the instrument

	@param trig	The trigger to push
 */
void TektronixOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommandQueued("TRIG:A:TYP TIMEO");

				m_transport->SendCommandQueued(string("TRIG:A:TIMEO:SOU ") + trig->GetInput(0).m_channel->GetHwname());
				SetTriggerLevelMSO56(trig);

				switch(trig->GetType())
				{
					case DropoutTrigger::EDGE_RISING:
						m_transport->SendCommandQueued("TRIG:A:TIMEO:POL STAYSH");
						break;

					case DropoutTrigger::EDGE_FALLING:
						m_transport->SendCommandQueued("TRIG:A:TIMEO:POL STAYSL");
						break;

					case DropoutTrigger::EDGE_ANY:
						m_transport->SendCommandQueued("TRIG:A:TIMEO:POL EIT");
						break;

					default:
						break;
				}

				m_transport->SendCommandQueued(string("TRIG:A:TIMEO:TIM ") +
					to_string_sci(trig->GetDropoutTime() * SECONDS_PER_FS));
			}
			break;

		default:
			break;
	}
}

/**
	@brief Pushes settings for a runt trigger to the instrument

	@param trig	The trigger to push
 */
void TektronixOscilloscope::PushRuntTrigger(RuntTrigger* trig)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommandQueued("TRIG:A:TYP RUN");

				m_transport->SendCommandQueued(string("TRIG:A:RUNT:SOU ") + trig->GetInput(0).m_channel->GetHwname());

				m_transport->SendCommandQueued(
					string("TRIG:A:LOW:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetLowerBound()));
				m_transport->SendCommandQueued(
					string("TRIG:A:UPP:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetUpperBound()));

				switch(trig->GetSlope())
				{
					case RuntTrigger::EDGE_RISING:
						m_transport->SendCommandQueued("TRIG:A:RUNT:POL POS");
						break;

					case RuntTrigger::EDGE_FALLING:
						m_transport->SendCommandQueued("TRIG:A:RUNT:POL NEG");
						break;

					case RuntTrigger::EDGE_ANY:
						m_transport->SendCommandQueued("TRIG:A:RUNT:POL EIT");
						break;
				}

				switch(trig->GetCondition())
				{
					case Trigger::CONDITION_LESS:
						m_transport->SendCommandQueued("TRIG:A:RUNT:WHEN LESS");
						break;

					case Trigger::CONDITION_GREATER:
						m_transport->SendCommandQueued("TRIG:A:RUNT:WHEN MORE");
						break;

					case Trigger::CONDITION_EQUAL:
						m_transport->SendCommandQueued("TRIG:A:RUNT:WHEN EQ");
						break;

					case Trigger::CONDITION_NOT_EQUAL:
						m_transport->SendCommandQueued("TRIG:A:RUNT:WHEN UNEQ");
						break;

					case Trigger::CONDITION_ANY:
						m_transport->SendCommandQueued("TRIG:A:RUNT:WHEN OCCURS");
						break;

					default:
						break;
				}

				m_transport->SendCommandQueued(string("TRIG:A:RUNT:WID ") +
					to_string_sci(trig->GetLowerInterval() * SECONDS_PER_FS));
			}
			break;

		default:
			break;
	}
}

/**
	@brief Pushes settings for a slew rate trigger to the instrument

	@param trig	The trigger to push
 */
void TektronixOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommandQueued("TRIG:A:TYP TRAN");

				m_transport->SendCommandQueued(string("TRIG:A:TRAN:SOU ") + trig->GetInput(0).m_channel->GetHwname());

				m_transport->SendCommandQueued(
					string("TRIG:A:LOW:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetLowerBound()));
				m_transport->SendCommandQueued(
					string("TRIG:A:UPP:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetUpperBound()));

				switch(trig->GetSlope())
				{
					case SlewRateTrigger::EDGE_RISING:
						m_transport->SendCommandQueued("TRIG:A:TRAN:POL POS");
						break;

					case SlewRateTrigger::EDGE_FALLING:
						m_transport->SendCommandQueued("TRIG:A:TRAN:POL NEG");
						break;

					case SlewRateTrigger::EDGE_ANY:
						m_transport->SendCommandQueued("TRIG:A:TRAN:POL EIT");
						break;
				}

				switch(trig->GetCondition())
				{
					case Trigger::CONDITION_LESS:
						m_transport->SendCommandQueued("TRIG:A:TRAN:WHEN FAST");
						break;

					case Trigger::CONDITION_GREATER:
						m_transport->SendCommandQueued("TRIG:A:TRAN:WHEN SLOW");
						break;

					case Trigger::CONDITION_EQUAL:
						m_transport->SendCommandQueued("TRIG:A:TRAN:WHEN EQ");
						break;

					case Trigger::CONDITION_NOT_EQUAL:
						m_transport->SendCommandQueued("TRIG:A:TRAN:WHEN UNEQ");
						break;

					default:
						break;
				}

				m_transport->SendCommandQueued(string("TRIG:A:TRAN:DELT ") +
					to_string_sci(trig->GetLowerInterval() * SECONDS_PER_FS));
			}
			break;

		default:
			break;
	}
}

/**
	@brief Pushes settings for a window trigger to the instrument

	@param trig	The trigger to push
 */
void TektronixOscilloscope::PushWindowTrigger(WindowTrigger* trig)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommandQueued("TRIG:A:TYP WIN");

				m_transport->SendCommandQueued(string("TRIG:A:WIN:SOU ") + trig->GetInput(0).m_channel->GetHwname());

				m_transport->SendCommandQueued(
					string("TRIG:A:LOW:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetLowerBound()));
				m_transport->SendCommandQueued(
					string("TRIG:A:UPP:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetUpperBound()));

				switch(trig->GetCrossingDirection())
				{
					case WindowTrigger::CROSS_UPPER:
						m_transport->SendCommandQueued("TRIG:A:WIN:CROSSI UPP");
						break;

					case WindowTrigger::CROSS_LOWER:
						m_transport->SendCommandQueued("TRIG:A:WIN:CROSSI LOW");
						break;

					case WindowTrigger::CROSS_EITHER:
						m_transport->SendCommandQueued("TRIG:A:WIN:CROSSI EIT");
						break;

					case WindowTrigger::CROSS_NONE:
						m_transport->SendCommandQueued("TRIG:A:WIN:CROSSI NON");
						break;
				}

				switch(trig->GetWindowType())
				{
					case WindowTrigger::WINDOW_ENTER:
						m_transport->SendCommandQueued("TRIG:A:WIN:WHEN ENTERSW");
						break;

					case WindowTrigger::WINDOW_EXIT:
						m_transport->SendCommandQueued("TRIG:A:WIN:WHEN EXITSW");
						break;

					case WindowTrigger::WINDOW_ENTER_TIMED:
						m_transport->SendCommandQueued("TRIG:A:WIN:WHEN INSIDEG");
						break;

					case WindowTrigger::WINDOW_EXIT_TIMED:
						m_transport->SendCommandQueued("TRIG:A:WIN:WHEN OUTSIDEG");
						break;

					default:
						break;
				}

				m_transport->SendCommandQueued(string("TRIG:A:WIN:WID ") +
					to_string_sci(trig->GetWidth() * SECONDS_PER_FS));
			}
			break;

		default:
			break;
	}
}

vector<Oscilloscope::DigitalBank> TektronixOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> ret;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			for(auto it : m_flexChannelParents)
			{
				DigitalBank bank;
				bank.push_back(it.first);
				ret.push_back(bank);
			}
			break;

		default:
			break;
	}

	return ret;
}

Oscilloscope::DigitalBank TektronixOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank ret;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			ret.push_back(GetOscilloscopeChannel(channel));
			break;

		default:
			break;
	}

	return ret;
}

bool TektronixOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool TektronixOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float TektronixOscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0;
}

float TektronixOscilloscope::GetDigitalThreshold(size_t channel)
{
	if( (channel < m_digitalChannelBase) || (m_digitalChannelCount == 0) )
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelDigitalThresholds.find(channel) != m_channelDigitalThresholds.end())
			return m_channelDigitalThresholds[channel];
	}

	float result = -1;

	auto chan = GetOscilloscopeChannel(channel);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			//note, group IDs are one based but lane IDs are zero based!
			result = stof(m_transport->SendCommandQueuedWithReply(
				string("DIGGRP") + to_string(m_flexChannelParents[chan]+1) +
				":D" + to_string(m_flexChannelLanes[chan]) + ":THR?"));

		default:
			break;
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelDigitalThresholds[channel] = result;
	return result;
}

void TektronixOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
	//not configurable
}

void TektronixOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	auto chan = GetOscilloscopeChannel(channel);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			//note, group IDs are one based but lane IDs are zero based!
			m_transport->SendCommandQueued(string("DIGGRP") + to_string(m_flexChannelParents[chan]+1) +
				":D" + to_string(m_flexChannelLanes[chan]) + ":THR " + to_string(level));
			break;

		default:
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spectrum analyzer mode

bool TektronixOscilloscope::HasFrequencyControls()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			return true;

		default:
			return false;
	}
}

void TektronixOscilloscope::SetSpan(int64_t span)
{
	//Update the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_span = span;
		m_spanValid = true;
	}

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued(string("SV:SPAN ") + to_string(span));
			break;

		default:
			break;
	}
}

int64_t TektronixOscilloscope::GetSpan()
{
	if(m_spanValid)
		return m_span;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_span = round(stod(m_transport->SendCommandQueuedWithReply("SV:SPAN?")));
			break;
		default:
			m_span = 1;
	}

	m_spanValid = true;
	return m_span;
}

void TektronixOscilloscope::SetCenterFrequency(size_t channel, int64_t freq)
{
	//Update cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelCenterFrequencies[channel] = freq;
	}

	if(channel < m_spectrumChannelBase)
		return;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued(
				string("CH") + to_string(channel-m_spectrumChannelBase+1) + ":SV:CENTERFREQUENCY " + to_string(freq));
			break;

		default:
			break;
	}
}

int64_t TektronixOscilloscope::GetCenterFrequency(size_t channel)
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCenterFrequencies.find(channel) != m_channelCenterFrequencies.end())
			return m_channelCenterFrequencies[channel];
	}

	int64_t centerFrequency;
	if(channel < m_spectrumChannelBase)
		centerFrequency =  0;
	else
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				centerFrequency = round(stof(m_transport->SendCommandQueuedWithReply(
					string("CH") + to_string(channel-m_spectrumChannelBase+1) + ":SV:CENTERFREQUENCY?")));
				break;
			default:
				centerFrequency = 0;
				break;
		}
	}

	//Update cache
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCenterFrequencies[channel] = centerFrequency;
	return centerFrequency;
}

void TektronixOscilloscope::SetResolutionBandwidth(int64_t rbw)
{
	m_rbw = rbw;
	m_rbwValid = true;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued(string("SV:RBW ") + to_string(rbw));
			break;

		default:
			break;
	}
}

int64_t TektronixOscilloscope::GetResolutionBandwidth()
{
	if(m_rbwValid)
		return m_rbw;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_rbw = round(stod(m_transport->SendCommandQueuedWithReply("SV:RBW?")));
			m_rbwValid = true;
			return m_rbw;

		default:
			return 1;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multimeter mode

unsigned int TektronixOscilloscope::GetMeasurementTypes()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(m_hasDVM)
				return DC_VOLTAGE | DC_RMS_AMPLITUDE | AC_RMS_AMPLITUDE;
			else
				return 0;

		default:
			return 0;
	}
}

int TektronixOscilloscope::GetMeterChannelCount()
{
	return m_analogChannelCount;
}

int TektronixOscilloscope::GetCurrentMeterChannel()
{
	if(!m_dmmChannelValid)
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				m_dmmChannel = (int)GetOscilloscopeChannelByHwName(Trim(m_transport->SendCommandQueuedWithReply(
					"DVM:SOU?")))->GetIndex();
				break;

			default:
				break;
		}

		m_dmmChannelValid = true;
	}

	return m_dmmChannel;
}

void TektronixOscilloscope::SetCurrentMeterChannel(int chan)
{
	//Skip channels that aren't usable
	if(!CanEnableChannel(chan))
		return;

	m_dmmChannel = chan;
	m_dmmChannelValid = true;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued(string("DVM:SOU ") + m_channels[chan]->GetHwname());
			break;

		default:
			break;
	}
}

Multimeter::MeasurementTypes TektronixOscilloscope::GetMeterMode()
{
	if(m_dmmModeValid)
		return m_dmmMode;

	return DC_VOLTAGE;
}

void TektronixOscilloscope::SetMeterMode(MeasurementTypes type)
{
	m_dmmMode = type;
	m_dmmModeValid = true;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			switch(type)
			{
				case Multimeter::DC_VOLTAGE:
					m_transport->SendCommandQueued("DVM:MOD DC");
					break;

				case Multimeter::DC_RMS_AMPLITUDE:
					m_transport->SendCommandQueued("DVM:MOD ACDCRMS");
					break;

				case Multimeter::AC_RMS_AMPLITUDE:
					m_transport->SendCommandQueued("DVM:MOD ACRMS");
					break;

				//no other modes supported
				default:
					break;
			}
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::SetMeterAutoRange(bool enable)
{
	m_dmmAutorange = enable;
	m_dmmAutorangeValid = true;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(enable)
				m_transport->SendCommandQueued("DVM:AUTOR ON");
			else
				m_transport->SendCommandQueued("DVM:AUTOR OFF");
			break;

		default:
			break;
	}
}

bool TektronixOscilloscope::GetMeterAutoRange()
{
	if(m_dmmAutorangeValid)
		return m_dmmAutorange;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_dmmAutorange = (stoi(m_transport->SendCommandQueuedWithReply("DVM:AUTOR?")) == 1);
			break;

		default:
			break;
	}

	m_dmmAutorangeValid = true;
	return m_dmmAutorange;
}

void TektronixOscilloscope::StartMeter()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued("DVM:MOD DC");		//TODO: use saved operating mode
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::StopMeter()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommandQueued("DVM:MOD OFF");
			break;

		default:
			break;
	}
}

double TektronixOscilloscope::GetMeterValue()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			return stod(m_transport->SendCommandQueuedWithReply("DVM:MEASU:VAL?"));

		default:
			return 0;
	}
}

int TektronixOscilloscope::GetMeterDigits()
{
	return 4;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator

vector<FunctionGenerator::WaveShape> TektronixOscilloscope::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(FunctionGenerator::SHAPE_SINE);
	ret.push_back(FunctionGenerator::SHAPE_SQUARE);
	ret.push_back(FunctionGenerator::SHAPE_PULSE); // TODO: Support set width (default: 1us)
	ret.push_back(FunctionGenerator::SHAPE_TRIANGLE); // TODO: Support set symmetry (default: 50%)
	ret.push_back(FunctionGenerator::SHAPE_DC);
	ret.push_back(FunctionGenerator::SHAPE_NOISE);
	ret.push_back(FunctionGenerator::SHAPE_SINC);
	ret.push_back(FunctionGenerator::SHAPE_GAUSSIAN);
	ret.push_back(FunctionGenerator::SHAPE_LORENTZ);
	ret.push_back(FunctionGenerator::SHAPE_EXPONENTIAL_RISE);
	ret.push_back(FunctionGenerator::SHAPE_EXPONENTIAL_DECAY);
	ret.push_back(FunctionGenerator::SHAPE_HAVERSINE);
	ret.push_back(FunctionGenerator::SHAPE_CARDIAC);
	return ret;
}

bool TektronixOscilloscope::GetFunctionChannelActive(int /*chan*/)
{
	return m_afgEnabled;
}

void TektronixOscilloscope::SetFunctionChannelActive(int /*chan*/, bool on)
{
	m_afgEnabled = on;

	lock_guard<recursive_mutex> lock(m_mutex);
	if(on)
	{
		m_transport->SendCommandQueued("AFG:OUTPUT:STATE 1");
		m_transport->SendCommandQueued("AFG:OUTPUT:MODE CONTINUOUS");
	}
	else
	{
		m_transport->SendCommandQueued("AFG:OUTPUT:STATE 0");
	}
}

float TektronixOscilloscope::GetFunctionChannelDutyCycle(int /*chan*/)
{
	return m_afgShape == SHAPE_SQUARE ? m_afgDutyCycle : 0;
}

void TektronixOscilloscope::SetFunctionChannelDutyCycle(int /*chan*/, float duty)
{
	m_afgDutyCycle = duty;

	if (m_afgShape != SHAPE_SQUARE)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued(string("AFG:SQUARE:DUTY ") + to_string(duty * 100));
}

float TektronixOscilloscope::GetFunctionChannelAmplitude(int /*chan*/)
{
	return m_afgAmplitude;
}

void TektronixOscilloscope::SetFunctionChannelAmplitude(int /*chan*/, float amplitude)
{
	m_afgAmplitude = amplitude;

	//Rescale if load is not high-Z
	if(m_afgImpedance == IMPEDANCE_50_OHM)
		amplitude *= 2;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued(string("AFG:AMPLITUDE ") + to_string(amplitude));
}

float TektronixOscilloscope::GetFunctionChannelOffset(int /*chan*/)
{
	return m_afgOffset;
}

void TektronixOscilloscope::SetFunctionChannelOffset(int /*chan*/, float offset)
{
	m_afgOffset = offset;

	//Rescale if load is not high-Z
	if(m_afgImpedance == IMPEDANCE_50_OHM)
		offset *= 2;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued(string("AFG:OFFSET ") + to_string(offset));
}

float TektronixOscilloscope::GetFunctionChannelFrequency(int /*chan*/)
{
	return m_afgFrequency;
}

void TektronixOscilloscope::SetFunctionChannelFrequency(int /*chan*/, float hz)
{
	m_afgFrequency = hz;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued(string("AFG:FREQUENCY ") + to_string(hz));
}

FunctionGenerator::WaveShape TektronixOscilloscope::GetFunctionChannelShape(int /*chan*/)
{
	return m_afgShape;
}

void TektronixOscilloscope::SetFunctionChannelShape(int /*chan*/, WaveShape shape)
{
	m_afgShape = shape;

	lock_guard<recursive_mutex> lock(m_mutex);
	switch(shape)
	{
		case SHAPE_SINE:
			m_transport->SendCommandQueued(string("AFG:FUNCTION SINE"));
			break;

		case SHAPE_SQUARE:
			m_transport->SendCommandQueued(string("AFG:FUNCTION SQUARE"));
			break;

		case SHAPE_PULSE:
			m_transport->SendCommandQueued(string("AFG:FUNCTION PULSE"));
			break;

		case SHAPE_TRIANGLE:
			m_transport->SendCommandQueued(string("AFG:FUNCTION RAMP"));
			break;

		case SHAPE_DC:
			m_transport->SendCommandQueued(string("AFG:FUNCTION DC"));
			break;

		case SHAPE_NOISE:
			m_transport->SendCommandQueued(string("AFG:FUNCTION NOISE"));
			break;

		case SHAPE_SINC:
			m_transport->SendCommandQueued(string("AFG:FUNCTION SINC")); // Called Sin(x)/x in scope UI
			break;

		case SHAPE_GAUSSIAN:
			m_transport->SendCommandQueued(string("AFG:FUNCTION GAUSSIAN"));
			break;

		case SHAPE_LORENTZ:
			m_transport->SendCommandQueued(string("AFG:FUNCTION LORENTZ"));
			break;

		case SHAPE_EXPONENTIAL_RISE:
			m_transport->SendCommandQueued(string("AFG:FUNCTION ERISE"));
			break;

		case SHAPE_EXPONENTIAL_DECAY:
			m_transport->SendCommandQueued(string("AFG:FUNCTION EDECAY"));
			break;

		case SHAPE_HAVERSINE:
			m_transport->SendCommandQueued(string("AFG:FUNCTION HAVERSINE"));
			break;

		case SHAPE_CARDIAC:
			m_transport->SendCommandQueued(string("AFG:FUNCTION CARDIAC"));
			break;

		// TODO: ARB

		default:
			break;
	}
}

bool TektronixOscilloscope::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return false;
}

FunctionGenerator::OutputImpedance TektronixOscilloscope::GetFunctionChannelOutputImpedance(int /*chan*/)
{
	return m_afgImpedance;
}

void TektronixOscilloscope::SetFunctionChannelOutputImpedance(int chan, OutputImpedance z)
{
	//Save old offset/amplitude
	float off = GetFunctionChannelOffset(chan);
	float amp = GetFunctionChannelAmplitude(chan);

	m_afgImpedance = z;
	if (z == IMPEDANCE_50_OHM)
		m_transport->SendCommandQueued(string("AFG:OUTPUT:LOAD:IMPEDANCE FIFTY"));
	else
		m_transport->SendCommandQueued(string("AFG:OUTPUT:LOAD:IMPEDANCE HIGHZ"));

	//Restore with new impedance
	SetFunctionChannelAmplitude(chan, amp);
	SetFunctionChannelOffset(chan, off);
}

