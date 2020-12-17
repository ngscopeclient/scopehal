/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg	                                                                       *
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
#include "TektronixOscilloscope.h"
#include "EdgeTrigger.h"
#include "PulseWidthTrigger.h"
#include "DropoutTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "WindowTrigger.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OSCILLOSCOPE_INITPROC_CPP(TektronixOscilloscope)

TektronixOscilloscope::TektronixOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_sampleRateValid(false)
	, m_sampleRate(0)
	, m_sampleDepthValid(false)
	, m_sampleDepth(0)
	, m_triggerOffsetValid(false)
	, m_triggerOffset(0)
	, m_rbwValid(false)
	, m_rbw(0)
	, m_dmmAutorangeValid(false)
	, m_dmmAutorange(false)
	, m_dmmChannelValid(false)
	, m_dmmChannel(0)
	, m_dmmModeValid(false)
	, m_dmmMode(Multimeter::DC_VOLTAGE)
	, m_digitalChannelBase(0)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_maxBandwidth(1000)
	, m_hasDVM(false)
{
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
	const char* colors_mso56[4] = { "#ffff00", "#20d3d8", "#f23f59", "#f16727" };	//yellow-cyan-pink-orange

	for(int i=0; i<nchans; i++)
	{
		//Color the channels based on Tektronix's standard color sequence
		string color = "#ffffff";
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				color = colors_mso56[i % 4];
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
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			color,
			1,
			i,
			true));
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
					OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
					colors_mso56[i % 4],
					m_channels.size(),
					true));
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
						m_channels[i]->GetHwname() + "_D" + to_string(j),
						OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
						m_channels[i]->m_displaycolor,
						1,
						m_channels.size(),
						true);

					m_flexChannelParents[chan] = i;
					m_flexChannelLanes[chan] = j;
					m_channels.push_back(chan);
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
				OscilloscopeChannel::CHANNEL_TYPE_TRIGGER,
				"",
				1,
				m_channels.size(),
				true);
			m_channels.push_back(m_extTrigChannel);
			break;

		default:
			m_extTrigChannel = new OscilloscopeChannel(
				this,
				"EX",
				OscilloscopeChannel::CHANNEL_TYPE_TRIGGER,
				"",
				1,
				m_channels.size(),
				true);
			m_channels.push_back(m_extTrigChannel);
			break;
	}

	//See what options we have
	vector<string> options;
	string reply = m_transport->SendCommandImmediateWithReply("*OPT?", false) + ',';
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				/*
					Seems like we have blocks divided by commas
					Each block contains three semicolon delimited fields: code, text description, license type
					Option code has is further divided into code:type, e.g. BW6-1000:License
					For extra fun, there can be commas inside the internal fields!

					So ultimately what we expect is:
						Option code
						Colon
						Option type
						Semicolon
						Description
						Semicolon
						License type
						Comma
				*/

				size_t pos = 0;
				while(pos < reply.length())
				{

					//Read option code (the only part we care about)
					string optcode;
					for( ; pos < reply.length(); pos++)
					{
						if(reply[pos] == ':')
							break;
						optcode += reply[pos];
					}
					options.push_back(optcode);

					//Skip the colon
					pos++;

					//Read and discard option type
					string opttype;
					for( ; pos < reply.length(); pos++)
					{
						if(reply[pos] == ';')
							break;
						opttype += reply[pos];
					}

					//Skip the semicolon
					pos++;

					//Read and discard option description (commas are legal here... thanks Tek)
					string optdesc;
					for( ; pos < reply.length(); pos++)
					{
						if(reply[pos] == ';')
							break;
						optdesc += reply[pos];
					}

					//Skip the semicolon
					pos ++;

					//Read and discard license type
					string lictype;
					for( ; pos < reply.length(); pos++)
					{
						if(reply[pos] == ',')
							break;
						lictype += reply[pos];
					}

					//Skip the comma
					pos ++;
				}
			}
			break;

		default:
			{


				for (std::string::size_type prev_pos=0, pos=0;
					 (pos = reply.find(',', pos)) != std::string::npos;
					 prev_pos=++pos)
				{
					std::string opt( reply.substr(prev_pos, pos-prev_pos) );
					if (opt == "0")
						continue;
					if (opt.substr(opt.length()-3, 3) == "(d)")
						opt.erase(opt.length()-3);

					options.push_back(opt);
				}
			}
			break;
	}

	//Print out the option list and do processing for each
	LogDebug("Installed options:\n");
	if(options.empty())
		LogDebug("* None\n");
	for(auto opt : options)
	{
		if(opt == "BW6-1000")
		{
			LogDebug("* BW6-1000 (1 GHz bandwidth)\n");
			//Don't touch m_maxBandwidth, we already got it from CONFIG:ANALO:BANDWIDTH
		}
		else if(opt == "SUP6-DVM")
		{
			LogDebug("* SUP6-DVM (Digital voltmeter)\n");
			m_hasDVM = true;
		}
		else if(opt == "SUP6-DEMO")
		{
			LogDebug("* SUP6-DEMO (Arbitrary function generator)\n");
		}
		else if(opt == "LIC6-SREMBD")
		{
			LogDebug("* LIC6-SREMBD (I2C/SPI trigger/decode)\n");
		}
		else if(opt == "LIC6-DDU")
		{
			/*
				This is a bundle code that unlocks lots of stuff:
					* 8 GHZ bandwidth
					* Function generator
					* Multimeter
					* I3C (decode only, no trigger)
					* 100baseT1 (decode only, no trigger)
					* SpaceWire (decode only, no trigger)

				Trigger/decode types:
					* Parallel bus
					* I2C
					* SPI
					* RS232
					* CAN
					* LIN
					* FlexRay
					* SENT
					* USB
					* 10/100 Ethernet
					* SPMI
					* MIL-STD-1553
					* ARINC 429
					* I2S/LJ/RJ/TDM audio

				This is in addition to the probably-standard trigger types:
					* Edge
					* Pulse width
					* Timeout
					* Runt
					* Window
					* Logic pattern
					* Setup/hold
					* Slew rate
					* Sequence
			 */
			 LogDebug("* LIC6-DDU (6 series distribution demo)\n");
			 m_hasDVM = true;
		}

		else
			LogDebug("* %s (unknown)\n", opt.c_str());
	}

	//Figure out what probes we have connected
	DetectProbes();
}

TektronixOscilloscope::~TektronixOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TektronixOscilloscope::GetDriverNameInternal()
{
	return "tektronix";
}

unsigned int TektronixOscilloscope::GetInstrumentTypes()
{
	unsigned int mask = Instrument::INST_OSCILLOSCOPE;
	if(m_hasDVM)
		mask |= Instrument::INST_DMM;
	return mask;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void TektronixOscilloscope::DetectProbes()
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

			//Figure out what kind of probe is attached (analog or digital).
			//If a digital probe (TLP058), disable this channel and mark as not usable
			for(size_t i=0; i<m_analogChannelCount; i++)
			{
				string reply = m_transport->SendCommandImmediateWithReply(m_channels[i]->GetHwname() + ":PROBETYPE?");

				if(reply == "DIG")
					m_probeTypes[i] = PROBE_TYPE_DIGITAL_8BIT;

				//Treat anything else as analog. See what type
				else
				{
					string id = TrimQuotes(m_transport->SendCommandImmediateWithReply(
						m_channels[i]->GetHwname() + ":PROBE:ID:TYP?"));

					if(id == "TPP1000")
						m_probeTypes[i] = PROBE_TYPE_ANALOG_250K;
					else
						m_probeTypes[i] = PROBE_TYPE_ANALOG;
				}
			}
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	m_channelOffsets.clear();
	m_channelVoltageRanges.clear();
	m_channelCouplings.clear();
	m_channelsEnabled.clear();
	m_probeTypes.clear();
	m_channelDeskew.clear();
	m_channelDisplayNames.clear();

	m_sampleRateValid = false;
	m_sampleDepthValid = false;
	m_triggerOffsetValid = false;
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
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//Pre-checks based on type
	if(IsDigital(i))
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If the parent analog channel doesn't have a digital probe, we're disabled
		size_t parent = m_flexChannelParents[m_channels[i]];
		if(m_probeTypes[parent] != PROBE_TYPE_DIGITAL_8BIT)
			return false;
	}
	else if(IsAnalog(i))
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if(m_probeTypes[i] == PROBE_TYPE_DIGITAL_8BIT)
			return false;
	}
	else if(IsSpectrum(i))
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if(m_probeTypes[i - m_spectrumChannelBase] == PROBE_TYPE_DIGITAL_8BIT)
			return false;
	}

	//Check the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
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
				reply = m_transport->SendCommandImmediateWithReply(
					m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE?");
			}
			else
			{
				m_transport->SendCommandImmediateWithReply(
					string("DISP:WAVEV:") + m_channels[i]->GetHwname() + ":STATE?");
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
					size_t parent = m_flexChannelParents[m_channels[i]];
					if(m_probeTypes[parent] != PROBE_TYPE_DIGITAL_8BIT)
						return;
				}
				break;

			default:
				break;
		}
	}

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(IsSpectrum(i))
				m_transport->SendCommandQueued(m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE ON");
			else
			{
				//Make sure the digital group is on
				if(IsDigital(i))
				{
					size_t parent = m_flexChannelParents[m_channels[i]];
					m_transport->SendCommandQueued(
						string("DISP:WAVEV:") + m_channels[parent]->GetHwname() + "_DALL:STATE ON");
				}

				m_transport->SendCommandQueued(string("DISP:WAVEV:") + m_channels[i]->GetHwname() + ":STATE ON");
			}
			break;

		default:
			break;
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;
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
		size_t parent = m_flexChannelParents[m_channels[i]];
		if(m_probeTypes[parent] != PROBE_TYPE_DIGITAL_8BIT)
			return false;
	}

	return true;
}

void TektronixOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if( IsAnalog(i) && (m_probeTypes[i] == PROBE_TYPE_DIGITAL_8BIT) )
			return;
	}

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(IsSpectrum(i))
				m_transport->SendCommandQueued(m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE OFF");
			else
				m_transport->SendCommandQueued(string("DISP:WAVEV:") + m_channels[i]->GetHwname() + ":STATE OFF");
			break;

		default:
			break;
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = false;
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
				string coup = m_transport->SendCommandImmediateWithReply(
					m_channels[i]->GetHwname() + ":COUP?");
				float nterm = stof(m_transport->SendCommandImmediateWithReply(
					m_channels[i]->GetHwname() + ":TER?"));

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

			m_transport->SendCommandImmediate(m_channels[i]->GetHwname() + ":COUP?");
			string coup_reply = m_transport->ReadReply();
			m_transport->SendCommandImmediate(m_channels[i]->GetHwname() + ":IMP?");
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
	return coupling;
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
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP DC");
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":TERM 50");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					if(m_probeTypes[i] == PROBE_TYPE_ANALOG_250K)
						m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":TERM 250E3");
					else
						m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":TERM 1E+6");
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					if(m_probeTypes[i] == PROBE_TYPE_ANALOG_250K)
						m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":TERM 250E3");
					else
						m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP DC");
					break;

				default:
					LogError("Invalid coupling for channel\n");
			}
			break;

		default:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_DC_50:
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP DC");
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":IMP FIFT");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":IMP ONEM");
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":IMP ONEM");
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":COUP DC");
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
					m_transport->SendCommandImmediateWithReply(m_channels[i]->GetHwname() + ":PRO:GAIN?"));
				float extatten = stof(
					m_transport->SendCommandImmediateWithReply(m_channels[i]->GetHwname() + ":PROBEF:EXTA?"));

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

			m_transport->SendCommandImmediate(m_channels[i]->GetHwname() + ":PROB?");

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
					m_transport->SendCommandImmediateWithReply(m_channels[i]->GetHwname() + ":PRO:GAIN?"));

				m_transport->SendCommandQueued(
					m_channels[i]->GetHwname() + ":PROBEF:EXTA " + to_string(atten * probegain));
			}
			break;

		default:
			//FIXME
			break;
	}
}

int TektronixOscilloscope::GetChannelBandwidthLimit(size_t i)
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
					string reply = m_transport->SendCommandImmediateWithReply(m_channels[i]->GetHwname() + ":BAN?");
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
				m_transport->SendCommandImmediate(m_channels[i]->GetHwname() + ":BWL?");
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
		case FAMILY_MSO6:

			//Only show "unlimited" for 50 ohm channels
			if(!is_1m)
				ret.push_back(0);

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
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BAN FUL");
				else
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BAN " + to_string(limit_hz));
			}
			break;

		default:
			break;
	}
}

double TektronixOscilloscope::GetChannelVoltageRange(size_t i)
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
	double range = 1;
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				if(IsSpectrum(i))
				{
					range = 10 * stof(m_transport->SendCommandImmediateWithReply(
						string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) + ":VERT:SCA?"));
				}
				else
				{
					range = 10 * stof(m_transport->SendCommandImmediateWithReply(
						m_channels[i]->GetHwname() + ":SCA?"));
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

void TektronixOscilloscope::SetChannelVoltageRange(size_t i, double range)
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
				double divsize = range/10;
				double offset_div = (GetChannelOffset(i) / divsize) - 5;

				m_transport->SendCommandQueued(string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) +
					":VERT:SCA " + to_string(divsize));

				//This seems to also mess up vertical position, so update that too to keep us centered
				m_transport->SendCommandQueued(string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) +
					":VERT:POS " + to_string(offset_div));
			}
			else
				m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":SCA " + to_string(range/10));
			break;

		default:
			break;
	}
}

OscilloscopeChannel* TektronixOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

string TektronixOscilloscope::GetChannelDisplayName(size_t i)
{
	auto chan = m_channels[i];

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return m_extTrigChannel->GetHwname();

	//Check cache first
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelDisplayNames.find(chan) != m_channelDisplayNames.end())
			return m_channelDisplayNames[chan];
	}

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
				name = TrimQuotes(m_transport->SendCommandImmediateWithReply(chan->GetHwname() + ":LAB:NAM?"));
				break;

			default:
				break;
		}
	}

	//Default to using hwname if no alias defined
	if(name == "")
		name = chan->GetHwname();

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDisplayNames[chan] = name;

	return name;
}

void TektronixOscilloscope::SetChannelDisplayName(size_t i, string name)
{
	auto chan = m_channels[i];

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return;

	//Update cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelDisplayNames[m_channels[i]] = name;
	}

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

double TektronixOscilloscope::GetChannelOffset(size_t i)
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
					float pos = stof(m_transport->SendCommandImmediateWithReply(
						string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) + ":VERT:POS?"));
					offset = (pos+5) * (GetChannelVoltageRange(i)/10);
				}
				else
					offset = -stof(m_transport->SendCommandImmediateWithReply(m_channels[i]->GetHwname() + ":OFFS?"));
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

void TektronixOscilloscope::SetChannelOffset(size_t i, double offset)
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
				double divsize = GetChannelVoltageRange(i) / 10;
				double offset_div = (offset / divsize) - 5;

				m_transport->SendCommandQueued(string("DISP:SPECV:CH") + to_string(i-m_spectrumChannelBase+1) +
					":VERT:POS " + to_string(offset_div));
			}
			else
				m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":OFFS " + to_string(-offset));
			break;

		default:
			break;
	}
}

Oscilloscope::TriggerMode TektronixOscilloscope::PollTrigger()
{
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	// Based on example from 6000 Series Programmer's Guide
	// Section 10 'Synchronizing Acquisitions' -> 'Polling Synchronization With Timeout'
	string ter = m_transport->SendCommandImmediateWithReply("TRIG:STATE?");

	if(ter == "SAV")
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	if(ter == "REA")
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//TODO: AUTO, TRIGGER. For now consider that same as RUN
	return TRIGGER_MODE_RUN;
}

bool TektronixOscilloscope::AcquireData()
{
	//LogDebug("Acquiring data\n");

	map<int, vector<WaveformBase*> > pending_waveforms;

	//Critical to lock in this order to avoid deadlock!
	lock_guard<recursive_mutex> lock2(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock(m_mutex);

	LogIndenter li;

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			if(!AcquireDataMSO56(pending_waveforms))
				return false;
			break;

		default:
			//m_transport->SendCommandImmediate("WFMPRE:" + m_channels[i]->GetHwname() + "?");
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
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		m_transport->SendCommandImmediate("ACQ:STATE ON");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

/**
	@brief Converts raw ADC samples to floating point
 */
void TektronixOscilloscope::Convert16BitSamples(
		int64_t* offs, int64_t* durs, float* pout, int16_t* pin, float ymult, float yoff, size_t count)
{
	for(size_t j=0; j<count; j++)
	{
		offs[j] = j;
		durs[j] = 1;
		pout[j] = ymult*pin[j] + yoff;
	}
}

__attribute__((target("avx2")))
void TektronixOscilloscope::Convert16BitSamplesAVX2(
		int64_t* offs, int64_t* durs, float* pout, int16_t* pin, float ymult, float yoff, size_t count)
{
	size_t end = count - (count % 32);

	int64_t __attribute__ ((aligned(32))) count_x4[] = { 0, 1, 2, 3 };

	__m256i all_ones	= _mm256_set1_epi64x(1);
	__m256i all_fours	= _mm256_set1_epi64x(4);
	__m256i counts		= _mm256_load_si256(reinterpret_cast<__m256i*>(count_x4));

	__m256 gains = { ymult, ymult, ymult, ymult, ymult, ymult, ymult, ymult };
	__m256 offsets = { yoff, yoff, yoff, yoff, yoff, yoff, yoff, yoff };

	for(size_t k=0; k<end; k += 16)
	{
		//Load all 16 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m256i raw_samples = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k));

		//Fill duration
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 4), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 8), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 12), all_ones);

		//Fill offset
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 4), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 8), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 12), counts);
		counts = _mm256_add_epi64(counts, all_fours);

		//Extract the low and high halves (8 samples each) from the input block
		__m128i block0_i16 = _mm256_extracti128_si256(raw_samples, 0);
		__m128i block1_i16 = _mm256_extracti128_si256(raw_samples, 1);

		//Convert both blocks from 16 to 32 bit, giving us a pair of 8x int32 vectors
		__m256i block0_i32 = _mm256_cvtepi16_epi32(block0_i16);
		__m256i block1_i32 = _mm256_cvtepi16_epi32(block1_i16);

		//Convert the 32-bit int blocks to fp32
		//Sadly there's no direct epi32 to ps conversion instruction.
		__m256 block0_float = _mm256_cvtepi32_ps(block0_i32);
		__m256 block1_float = _mm256_cvtepi32_ps(block1_i32);

		//Woo! We've finally got floating point data. Now we can do the fun part.
		block0_float = _mm256_mul_ps(block0_float, gains);
		block1_float = _mm256_mul_ps(block1_float, gains);

		block0_float = _mm256_add_ps(block0_float, offsets);
		block1_float = _mm256_add_ps(block1_float, offsets);

		//All done, store back to the output buffer
		_mm256_store_ps(pout + k, 		block0_float);
		_mm256_store_ps(pout + k + 8,	block1_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(size_t k=end; k<count; k++)
	{
		offs[k] = k;
		durs[k] = 1;
		pout[k] = pin[k] * ymult + yoff;
	}
}

/**
	@brief Converts 8-bit ADC samples to floating point
 */
void TektronixOscilloscope::Convert8BitSamples(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float ymult, float yoff, size_t count)
{
	for(unsigned int k=0; k<count; k++)
	{
		offs[k] = k;
		durs[k] = 1;
		pout[k] = pin[k] * ymult + yoff;
	}
}

/**
	@brief Optimized version of Convert8BitSamples()
 */
__attribute__((target("avx2")))
void TektronixOscilloscope::Convert8BitSamplesAVX2(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float ymult, float yoff, size_t count)
{
	unsigned int end = count - (count % 32);

	int64_t __attribute__ ((aligned(32))) count_x4[] = { 0, 1, 2, 3 };

	__m256i all_ones	= _mm256_set1_epi64x(1);
	__m256i all_fours	= _mm256_set1_epi64x(4);
	__m256i counts		= _mm256_load_si256(reinterpret_cast<__m256i*>(count_x4));

	__m256 gains = { ymult, ymult, ymult, ymult, ymult, ymult, ymult, ymult };
	__m256 offsets = { yoff, yoff, yoff, yoff, yoff, yoff, yoff, yoff };

	for(unsigned int k=0; k<end; k += 32)
	{
		//Load all 32 raw ADC samples, without assuming alignment
		//(on most modern Intel processors, load and loadu have same latency/throughput)
		__m256i raw_samples = _mm256_loadu_si256(reinterpret_cast<__m256i*>(pin + k));

		//Fill duration
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 4), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 8), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 12), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 16), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 20), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 24), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 28), all_ones);

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

		//Fill offset
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 4), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 8), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 12), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 16), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 20), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 24), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 28), counts);
		counts = _mm256_add_epi64(counts, all_fours);

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

		block0_float = _mm256_add_ps(block0_float, offsets);
		block1_float = _mm256_add_ps(block1_float, offsets);
		block2_float = _mm256_add_ps(block2_float, offsets);
		block3_float = _mm256_add_ps(block3_float, offsets);

		//All done, store back to the output buffer
		_mm256_store_ps(pout + k, 		block0_float);
		_mm256_store_ps(pout + k + 8,	block1_float);
		_mm256_store_ps(pout + k + 16,	block2_float);
		_mm256_store_ps(pout + k + 24,	block3_float);
	}

	//Get any extras we didn't get in the SIMD loop
	for(unsigned int k=end; k<count; k++)
	{
		offs[k] = k;
		durs[k] = 1;
		pout[k] = pin[k] * ymult + yoff;
	}
}

bool TektronixOscilloscope::AcquireDataMSO56(map<int, vector<WaveformBase*> >& pending_waveforms)
{
	//Make sure record length is valid
	GetSampleDepth();

	//Preamble fields (not all are used)
	int byte_num;
	int bit_num;
	char encoding[32];
	char bin_format[32];
	char asc_format[32];
	char byte_order[32];
	char wfid[256];
	int nr_pt;
	char pt_fmt[32];
	char pt_order[32];
	char xunit[32];
	double xincrement;
	double xzero;
	int pt_off;
	char yunit[32];
	double ymult;
	double yoff;
	double yzero;
	char domain[32];
	char wfmtype[32];
	double centerfreq;
	double span;

	//Ask for the analog data
	m_transport->SendCommandImmediate("DAT:WID 1");					//8-bit data in NORMAL mode
	m_transport->SendCommandImmediate("DAT:ENC SRI");				//signed, little endian binary
	size_t timebase = 0;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		// Set source & get preamble+data
		m_transport->SendCommandImmediate(string("DAT:SOU ") + m_channels[i]->GetHwname());

		//Ask for the waveform preamble
		string preamble = m_transport->SendCommandImmediateWithReply("WFMO?", false);

		//Process it (grab the whole block, semicolons and all)
		sscanf(preamble.c_str(),
			"%d;%d;%31[^;];%31[^;];%31[^;];%31[^;];%255[^;];%d;%c;%31[^;];"
			"%31[^;];%lf;%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
			&byte_num, &bit_num, encoding, bin_format, asc_format, byte_order, wfid, &nr_pt, pt_fmt, pt_order,
			xunit, &xincrement, &xzero,	&pt_off, yunit, &ymult, &yoff, &yzero, domain, wfmtype, &centerfreq, &span);

		timebase = xincrement * FS_PER_SECOND;	//scope gives sec, not fs
		m_channelOffsets[i] = -yoff;

		//LogDebug("Channel %zu (%s)\n", i, m_channels[i]->GetHwname().c_str());
		LogIndenter li2;

		//Read the data block
		size_t nsamples;
		int8_t* samples = (int8_t*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", nsamples);
		if(samples == NULL)
		{
			pending_waveforms[i].push_back(NULL);
			continue;
		}

		//Set up the capture we're going to store our data into
		//(no TDC data or fine timestamping available on Tektronix scopes?)
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_densePacked = true;
		cap->m_timescale = timebase;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
		cap->Resize(nsamples);

		if(g_hasAvx2)
		{
			Convert8BitSamplesAVX2(
				(int64_t*)&cap->m_offsets[0],
				(int64_t*)&cap->m_durations[0],
				(float*)&cap->m_samples[0],
				samples,
				ymult,
				yoff,
				nsamples);
		}
		else
		{
			Convert8BitSamples(
				(int64_t*)&cap->m_offsets[0],
				(int64_t*)&cap->m_durations[0],
				(float*)&cap->m_samples[0],
				samples,
				ymult,
				yoff,
				nsamples);
		}

		//Done, update the data
		pending_waveforms[i].push_back(cap);

		//Done
		delete[] samples;

		//Throw out garbage at the end of the message (why is this needed?)
		m_transport->ReadReply();
	}

	//Get the spectrum stuff
	bool firstSpectrum = true;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		auto nchan = m_spectrumChannelBase + i;
		if(!IsChannelEnabled(nchan))
			continue;

		//Select mode
		if(firstSpectrum)
		{
			m_transport->SendCommandImmediate("DAT:WID 8");					//double precision floating point data
			m_transport->SendCommandImmediate("DAT:ENC SFPB");				//IEEE754 float
			firstSpectrum = false;
		}

		// Set source & get preamble+data
		m_transport->SendCommandImmediate(string("DAT:SOU ") + m_channels[i]->GetHwname() + "_SV_NORMAL");

		//Ask for the waveform preamble
		string preamble = m_transport->SendCommandImmediateWithReply("WFMO?", false);

		//LogDebug("Channel %zu (%s)\n", nchan, m_channels[nchan]->GetHwname().c_str());
		//LogIndenter li2;

		//Process it
		double hzbase = 0;
		double hzoff = 0;
		sscanf(preamble.c_str(),
			"%d;%d;%31[^;];%31[^;];%31[^;];%31[^;];%255[^;];%d;%c;%31[^;];"
			"%31[^;];%lf;%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
			&byte_num, &bit_num, encoding, bin_format, asc_format, byte_order, wfid, &nr_pt, pt_fmt, pt_order,
			xunit, &hzbase, &hzoff,	&pt_off, yunit, &ymult, &yoff, &yzero, domain, wfmtype, &centerfreq, &span);
		m_channelOffsets[i] = -yoff;

		//Read the data block
		size_t msglen;
		double* samples = (double*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
		if(samples == NULL)
		{
			pending_waveforms[nchan].push_back(NULL);
			continue;
		}
		size_t nsamples = msglen/8;

		//Set up the capture we're going to store our data into
		//(no TDC data or fine timestamping available on Tektronix scopes?)
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = hzbase;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		cap->m_densePacked = true;
		double t = GetTime();
		cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
		cap->Resize(nsamples);

		//We get dBm from the instrument, so just have to convert double to single precision
		//TODO: are other units possible here?
		int64_t ibase = hzoff / hzbase;
		for(size_t j=0; j<nsamples; j++)
		{
			cap->m_offsets[j] = j + ibase;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = ymult*samples[j] + yoff;
		}

		//Done, update the data
		pending_waveforms[nchan].push_back(cap);

		//Done
		delete[] samples;

		//Throw out garbage at the end of the message (why is this needed?)
		m_transport->ReadReply();

		//Look for peaks
		//TODO: make this configurable, for now 1 MHz spacing and up to 10 peaks
		dynamic_cast<SpectrumChannel*>(m_channels[nchan])->FindPeaks(cap, 10, 1000000);
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


		//Configuration
		if(firstDigital)
		{
			m_transport->SendCommandImmediate("DAT:WID 1");					//8 data bits per channel
			m_transport->SendCommandImmediate("DAT:ENC SRI");				//signed, little endian binary
			firstDigital = false;
		}

		//Ask for all of the data
		m_transport->SendCommandImmediate(string("DAT:SOU CH") + to_string(i+1) + "_DALL");

		//Ask for the waveform preamble
		string preamble = m_transport->SendCommandImmediateWithReply("WFMO?", false);
		sscanf(preamble.c_str(),
			"%d;%d;%31[^;];%31[^;];%31[^;];%31[^;];%255[^;];%d;%c;%31[^;];"
			"%31[^;];%lf;%lf;%d;%31[^;];%lf;%lf;%lf;%31[^;];%31[^;];%lf;%lf",
			&byte_num, &bit_num, encoding, bin_format, asc_format, byte_order, wfid, &nr_pt, pt_fmt, pt_order,
			xunit, &xincrement, &xzero,	&pt_off, yunit, &ymult, &yoff, &yzero, domain, wfmtype, &centerfreq, &span);
		timebase = xincrement * FS_PER_SECOND;	//scope gives sec, not fs

		//And the acutal data
		size_t msglen;
		char* samples = (char*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
		if(samples == NULL)
		{
			for(int j=0; j<8; j++)
				pending_waveforms[m_digitalChannelBase + i*8 + j].push_back(NULL);
			continue;
		}

		//Process the data for each channel
		for(int j=0; j<8; j++)
		{
			//Set up the capture we're going to store our data into
			//(no TDC data or fine timestamping available on Tektronix scopes?)
			DigitalWaveform* cap = new DigitalWaveform;
			cap->m_timescale = timebase;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			cap->m_densePacked = true;
			double t = GetTime();
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
			cap->Resize(msglen);

			//Extract sample data
			int mask = (1 << j);
			for(size_t k=0; k<msglen; k++)
			{
				cap->m_offsets[k] = k;
				cap->m_durations[k] = 1;
				cap->m_samples[k] = (samples[k] & mask) ? true : false;
			}

			//Done, update the data
			pending_waveforms[m_digitalChannelBase + i*8 + j].push_back(cap);
		}

		//Done
		delete[] samples;

		//Throw out garbage at the end of the message (why is this needed?)
		m_transport->ReadReply();
	}

	return true;
}

void TektronixOscilloscope::Start()
{
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixOscilloscope::StartSingleTrigger()
{
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixOscilloscope::Stop()
{
	m_transport->SendCommandQueued("ACQ:STATE STOP");
	m_triggerArmed = false;
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
	uint64_t scales_mso6[] = {1, 10, 100, 1*k, 10*k};

	switch(m_family)
	{
		case FAMILY_MSO5:
			break;

		case FAMILY_MSO6:
			{
				for(auto b : bases)
					ret.push_back(b / 10);

				for(auto scale : scales_mso6)
				{
					for(auto b : bases)
						ret.push_back(b * scale);
				}

				//We break with the pattern on the upper end of the frequency range
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
				ret.push_back(25 * g);		//8 bits, not 12.
											//TODO: we can save bandwidth by using 8 bit waveform download for this
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
					ret.emplace(InterleaveConflict(m_channels[i], m_channels[i]));
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
	//const int64_t m = k*k;

	switch(m_family)
	{
		case FAMILY_MSO5:
			break;

		//The scope allows extremely granular specification of memory depth.
		//For our purposes, only show a bunch of common step values.
		//No need for super fine granularity since record length isn't tied to the UI display width.
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

				//Deeper memory is disabled because using it seems to crash the scope firmware. Not sure why yet.

				/*
				ret.push_back(1 * m);
				ret.push_back(2 * m);
				ret.push_back(5 * m);
				ret.push_back(10 * m);
				ret.push_back(20 * m);
				ret.push_back(50 * m);
				ret.push_back(62500 * k);
				*/
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
			m_sampleRate = stod(m_transport->SendCommandImmediateWithReply("HOR:MODE:SAMPLER?"));

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
			m_sampleDepth = stos(m_transport->SendCommandImmediateWithReply("HOR:MODE:RECO?"));
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

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
		{
			//Instrument reports position of trigger from the midpoint of the display
			double center_offset_sec = stod(m_transport->SendCommandImmediateWithReply("HOR:DELAY:TIME?"));

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
			m_transport->SendCommandQueued(m_channels[channel]->GetHwname() + ":DESK " + to_string(-skew) + "E-15");
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
		lock_guard<recursive_mutex> lock(m_mutex);
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				//Tek's skew convention has positive values move the channel EARLIER, so we need to flip sign
				deskew = -round(FS_PER_SECOND * stof(
					m_transport->SendCommandImmediateWithReply(m_channels[channel]->GetHwname() + ":DESK?")));
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
				string reply = m_transport->SendCommandImmediateWithReply("TRIG:A:TYP?");

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
	@brief Parses a trigger level message
 */
float TektronixOscilloscope::ReadTriggerLevelMSO56()
{
	string reply = m_transport->SendCommandImmediateWithReply("TRIG:A:LEV?", false);

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
				auto reply = m_transport->SendCommandImmediateWithReply("TRIG:A:EDGE:SOU?");
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//Trigger level
				et->SetLevel(ReadTriggerLevelMSO56());

				//Edge slope
				reply = m_transport->SendCommandImmediateWithReply("TRIG:A:EDGE:SLO?");
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
				auto reply = m_transport->SendCommandImmediateWithReply("TRIG:A:PULSEW:SOU?");
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//TODO: TRIG:A:PULSEW:LOGICQUAL?

				//Trigger level
				et->SetLevel(ReadTriggerLevelMSO56());

				Unit fs(Unit::UNIT_FS);
				et->SetUpperBound(fs.ParseString(m_transport->SendCommandImmediateWithReply("TRIG:A:PULSEW:HIGHL?")));
				et->SetLowerBound(fs.ParseString(m_transport->SendCommandImmediateWithReply("TRIG:A:PULSEW:LOWL?")));

				//Edge slope
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:PULSEW:POL?"));
				if(reply == "POS")
					et->SetType(EdgeTrigger::EDGE_RISING);
				else if(reply == "NEG")
					et->SetType(EdgeTrigger::EDGE_FALLING);

				//Condition
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:PULSEW:WHE?"));
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
				auto reply = m_transport->SendCommandImmediateWithReply("TRIG:A:TIMEO:SOU?");
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//Trigger level
				et->SetLevel(ReadTriggerLevelMSO56());

				Unit fs(Unit::UNIT_FS);
				et->SetDropoutTime(fs.ParseString(m_transport->SendCommandImmediateWithReply("TRIG:A:TIMEO:TIM?")));

				//TODO: TRIG:A:TIMEO:LOGICQUAL?

				//Edge slope
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:TIMEO:POL?"));
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
				auto reply = m_transport->SendCommandImmediateWithReply("TRIG:A:RUNT:SOU?");
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//Trigger level
				auto chname = reply;
				et->SetLowerBound(stof(m_transport->SendCommandImmediateWithReply(
					string("TRIG:A:LOW:") + chname + "?")));
				et->SetUpperBound(stof(m_transport->SendCommandImmediateWithReply(
					string("TRIG:A:UPP:") + chname + "?")));

				//Match condition
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:RUNT:WHE?"));
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
				et->SetLowerInterval(fs.ParseString(m_transport->SendCommandImmediateWithReply("TRIG:A:RUNT:WID?")));

				//TODO: TRIG:A:RUNT:LOGICQUAL?

				//Edge slope
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:RUNT:POL?"));
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
	@brief Reads settings for a runt trigger from the instrument
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
				auto reply = m_transport->SendCommandImmediateWithReply("TRIG:A:TRAN:SOU?");
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//Trigger level
				auto chname = reply;
				et->SetLowerBound(stof(m_transport->SendCommandImmediateWithReply(
					string("TRIG:A:LOW:") + chname + "?")));
				et->SetUpperBound(stof(m_transport->SendCommandImmediateWithReply(
					string("TRIG:A:UPP:") + chname + "?")));

				//Match condition
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:TRAN:WHE?"));
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
				et->SetLowerInterval(fs.ParseString(m_transport->SendCommandImmediateWithReply("TRIG:A:TRAN:DELT?")));

				//TODO: TRIG:A:TRAN:LOGICQUAL?

				//Edge slope
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:TRAN:POL?"));
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
				auto reply = m_transport->SendCommandImmediateWithReply("TRIG:A:WIN:SOU?");
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//Trigger level
				auto chname = reply;
				et->SetLowerBound(stof(m_transport->SendCommandImmediateWithReply(
					string("TRIG:A:LOW:") + chname + "?")));
				et->SetUpperBound(stof(m_transport->SendCommandImmediateWithReply(
					string("TRIG:A:UPP:") + chname + "?")));

				//TODO: TRIG:A:WIN:LOGICQUAL?

				//Crossing direction (only used for inside/outside greater)
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:WIN:CROSSI?"));
				if(reply == "UPP")
					et->SetCrossingDirection(WindowTrigger::CROSS_UPPER);
				else if(reply == "LOW")
					et->SetCrossingDirection(WindowTrigger::CROSS_LOWER);
				else if(reply == "EIT")
					et->SetCrossingDirection(WindowTrigger::CROSS_EITHER);
				else if(reply == "NON")
					et->SetCrossingDirection(WindowTrigger::CROSS_NONE);

				//Match condition
				reply = Trim(m_transport->SendCommandImmediateWithReply("TRIG:A:WIN:WHE?"));
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
				et->SetWidth(fs.ParseString(m_transport->SendCommandImmediateWithReply("TRIG:A:WIN:WID?")));
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
	@brief Pushes settings for an edge trigger to the instrument
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
				m_transport->SendCommandQueued(
					string("TRIG:A:LEV:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string_sci(trig->GetLevel()));

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

void TektronixOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommandQueued("TRIG:A:TYP WID");

				m_transport->SendCommandQueued(string("TRIG:A:PULSEW:SOU ") + trig->GetInput(0).m_channel->GetHwname());
				m_transport->SendCommandQueued(
					string("TRIG:A:LEV:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetLevel()));

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
				m_transport->SendCommandQueued(
					string("TRIG:A:LEV:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetLevel()));

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
	@brief Pushes settings for a runt trigger to the instrument
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
	@brief Pushes settings for a runt trigger to the instrument
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
			ret.push_back(m_channels[channel]);
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
	//TODO: caching?

	auto chan = m_channels[channel];

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			//note, group IDs are one based but lane IDs are zero based!
			return stof(m_transport->SendCommandImmediateWithReply(
				string("DIGGRP") + to_string(m_flexChannelParents[chan]+1) +
				":D" + to_string(m_flexChannelLanes[chan]) + ":THR?"));

		default:
			break;
	}
	return -1;
}

void TektronixOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
	//not configurable
}

void TektronixOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	auto chan = m_channels[channel];

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
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			return round(stod(m_transport->SendCommandImmediateWithReply("SV:SPAN?")));

		default:
			return 1;
	}
}

void TektronixOscilloscope::SetCenterFrequency(size_t channel, int64_t freq)
{
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
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			return round(stof(m_transport->SendCommandImmediateWithReply(
				string("CH") + to_string(channel-m_spectrumChannelBase+1) + ":SV:CENTERFREQUENCY?")));

		default:
			return 0;
	}
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
			m_rbw = round(stod(m_transport->SendCommandImmediateWithReply("SV:RBW?")));
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

string TektronixOscilloscope::GetMeterChannelName(int chan)
{
	return GetChannel(chan)->GetDisplayName();
}

int TektronixOscilloscope::GetCurrentMeterChannel()
{
	if(!m_dmmChannelValid)
	{
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				m_dmmChannel = (int)GetChannelByHwName(Trim(m_transport->SendCommandImmediateWithReply(
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
			m_dmmAutorange = (stoi(m_transport->SendCommandImmediateWithReply("DVM:AUTOR?")) == 1);
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
			return stod(m_transport->SendCommandImmediateWithReply("DVM:MEASU:VAL?"));

		default:
			return 0;
	}
}

int TektronixOscilloscope::GetMeterDigits()
{
	return 4;
}
