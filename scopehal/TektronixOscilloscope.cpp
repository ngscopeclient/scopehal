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
	, m_digitalChannelBase(0)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_bandwidth(1000)
{
	//Figure out what device family we are
	if(m_model.find("MSO5") == 0)
		m_family = FAMILY_MSO5;
	else if(m_model.find("MSO6") == 0)
		m_family = FAMILY_MSO6;
	else
		m_family = FAMILY_UNKNOWN;

	//Last digit of the model number is the number of channels
	std::string model_number = m_model;
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
	m_transport->SendCommand("HEAD 0");

	//Device specific initialization
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand("ACQ:MOD SAM");				//actual sampled data, no averaging etc
			m_transport->SendCommand("VERB OFF");					//Disable verbose mode (send shorter commands)
			m_transport->SendCommand("ACQ:STOPA SEQ");				//Stop after acquiring a single waveform
			m_transport->SendCommand("CONFIG:ANALO:BANDW?");		//Figure out what bandwidth we have
			m_bandwidth = stof(m_transport->ReadReply()) * 1e-6;	//(so we know what probe bandwidth is)
			m_transport->SendCommand("HOR:MODE MAN");				//Enable manual sample rate and record length
			m_transport->SendCommand("HOR:DEL:MOD ON");				//Horizontal position is in time units
			break;

		default:
			// 8-bit signed data
			m_transport->SendCommand("DATA:ENC RIB;WID 1");
			m_transport->SendCommand("DATA:SOURCE CH1, CH2, CH3, CH4;START 0; STOP 100000");

			// FIXME: where to put this?
			m_transport->SendCommand("ACQ:STOPA SEQ;REPE 1");
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
					new OscilloscopeChannel(
					this,
					string("CH") + to_string(i+1) + "_SPECTRUM",
					OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
					colors_mso56[i % 4],
					Unit(Unit::UNIT_HZ),
					Unit(Unit::UNIT_DBM),
					1,
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
	/*
	m_transport->SendCommand("*OPT?");
	string reply = m_transport->ReadReply();
	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

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
	*/

	//Print out the option list and do processing for each
	LogDebug("Installed options:\n");
	if(options.empty())
		LogDebug("* None\n");
	for(auto opt : options)
	{
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
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void TektronixOscilloscope::DetectProbes()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

			//Figure out what kind of probe is attached (analog or digital).
			//If a digital probe (TLP058), disable this channel and mark as not usable
			for(size_t i=0; i<m_analogChannelCount; i++)
			{
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROBETYPE?");
				string reply = m_transport->ReadReply();

				if(reply == "DIG")
					m_probeTypes[i] = PROBE_TYPE_DIGITAL_8BIT;

				//Treat anything else as analog
				else
					m_probeTypes[i] = PROBE_TYPE_ANALOG;
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

	m_sampleRateValid = false;
	m_sampleDepthValid = false;
	m_triggerOffsetValid = false;

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
		if(m_probeTypes[i] != PROBE_TYPE_ANALOG)
			return false;
	}
	else if(IsSpectrum(i))
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if(m_probeTypes[i - m_spectrumChannelBase] != PROBE_TYPE_ANALOG)
			return false;
	}

	//Check the cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:

			//Undocumented command to toggle spectrum view state
			if(IsSpectrum(i))
				m_transport->SendCommand(m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE?");
			else
				m_transport->SendCommand(string("DISP:WAVEV:") + m_channels[i]->GetHwname() + ":STATE?");
			break;

		default:
			break;
	}

	string reply = m_transport->ReadReply();

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

void TektronixOscilloscope::EnableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if( IsAnalog(i) && (m_probeTypes[i] != PROBE_TYPE_ANALOG) )
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

	{
		lock_guard<recursive_mutex> lock(m_mutex);
		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				if(IsSpectrum(i))
					m_transport->SendCommand(m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE ON");
				else
					m_transport->SendCommand(string("DISP:WAVEV:") + m_channels[i]->GetHwname() + ":STATE ON");
				break;

			default:
				break;
		}
	}

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelsEnabled[i] = true;
}

void TektronixOscilloscope::DisableChannel(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		//If we're an analog channel with a digital probe connected, the analog channel is unusable
		if( IsAnalog(i) && (m_probeTypes[i] != PROBE_TYPE_ANALOG) )
			return;
	}

	{
		lock_guard<recursive_mutex> lock(m_mutex);

		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				if(IsSpectrum(i))
					m_transport->SendCommand(m_channels[i - m_spectrumChannelBase]->GetHwname() + ":SV:STATE OFF");
				else
					m_transport->SendCommand(string("DISP:WAVEV:") + m_channels[i]->GetHwname() + ":STATE OFF");
				break;

			default:
				break;
		}

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
	{
		lock_guard<recursive_mutex> lock2(m_mutex);

		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				{
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
					string coup = m_transport->ReadReply();
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TER?");
					float nterm = stof(m_transport->ReadReply());

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

				m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP?");
				string coup_reply = m_transport->ReadReply();
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP?");
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
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelCouplings[i] = coupling;
	return coupling;
}

void TektronixOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(!IsAnalog(i))
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_DC_50:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TERM 50");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TERM 1E+6");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":TERM 1E+6");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
					break;

				default:
					LogError("Invalid coupling for channel\n");
			}
			break;

		default:
			switch(type)
			{
				case OscilloscopeChannel::COUPLE_DC_50:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP FIFT");
					break;

				case OscilloscopeChannel::COUPLE_AC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP ONEM");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP AC");
					break;

				case OscilloscopeChannel::COUPLE_DC_1M:
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":IMP ONEM");
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUP DC");
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

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PRO:GAIN?");
				float probegain = stof(m_transport->ReadReply());
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROBEF:EXTA?");
				float extatten = stof(m_transport->ReadReply());

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

			m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROB?");

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

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//This function takes the overall system attenuation as an argument.
				//We need to scale this by the probe gain to figure out the necessary external attenuation.
				//At the moment, this isn't cached, but we probably should do this in the future.
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PRO:GAIN?");
				float probegain = stof(m_transport->ReadReply());

				float extatten = atten * probegain;
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":PROBEF:EXTA " + to_string(extatten));
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

	int bwl = 0;
	{
		lock_guard<recursive_mutex> lock(m_mutex);

		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				{
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":BAN?");
					string reply = m_transport->ReadReply();
					if(reply == "FUL")		//no limit
						bwl = 0;
					else
						bwl = stof(reply) * 1e-6;

					//If the returned bandwidth is the same as the instrument's upper bound, report "no limit"
					if(bwl == m_bandwidth)
						bwl = 0;
				}
				break;

			default:
				/*
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":BWL?");
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

void TektronixOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	if(!IsAnalog(i))
		return;

	//Update cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelBandwidthLimits[i] = limit_mhz;
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Instrument wants Hz, not MHz, or "FUL" for no limit
				size_t limit_hz = limit_mhz;
				limit_hz *= 1000 * 1000;

				if(limit_mhz == 0)
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":BAN FUL");
				else
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":BAN " + to_string(limit_hz));
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

	//We want total range, not per division
	double range;
	{
		lock_guard<recursive_mutex> lock2(m_mutex);

		switch(m_family)
		{
			case FAMILY_MSO5:
			case FAMILY_MSO6:
				if(IsSpectrum(i))
					return 70;	//FIXME
				else
					m_transport->SendCommand(m_channels[i]->GetHwname() + ":SCA?");
				break;

			default:
				break;
		}

		range = stof(m_transport->ReadReply()) * 10;
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
	if(!IsAnalog(i))
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":SCA " + to_string(range/10));
			break;

		default:
			break;
	}
}

OscilloscopeChannel* TektronixOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
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
	if(!IsAnalog(i))
		return 0;

	//Read offset
	double offset;
	{
		lock_guard<recursive_mutex> lock2(m_mutex);
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFS?");
		offset = -stof(m_transport->ReadReply());
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
	if(i > m_analogChannelCount)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFS " + to_string(-offset));
			break;

		default:
			break;
	}
}

Oscilloscope::TriggerMode TektronixOscilloscope::PollTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	// Based on example from 6000 Series Programmer's Guide
	// Section 10 'Synchronizing Acquisitions' -> 'Polling Synchronization With Timeout'
	m_transport->SendCommand("TRIG:STATE?");
	string ter = m_transport->ReadReply();

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
			//m_transport->SendCommand("WFMPRE:" + m_channels[i]->GetHwname() + "?");
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
			int64_t ps_per_sample = round(xincrement * 1e12f);
			//LogDebug("%ld ps/sample\n", ps_per_sample);

			//LogDebug("length = %d\n", length);

			//Set up the capture we're going to store our data into
			//(no TDC data available on Tektronix scopes?)
			AnalogWaveform* cap = new AnalogWaveform;
			cap->m_timescale = ps_per_sample;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startPicoseconds = (t - floor(t)) * 1e12f;

			//Ask for the data
			m_transport->SendCommand("CURV?");

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
		m_transport->SendCommand("ACQ:STATE ON");
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");
	return true;
}

bool TektronixOscilloscope::AcquireDataMSO56(map<int, vector<WaveformBase*> >& pending_waveforms)
{
	//Get record length
	m_transport->SendCommand("HOR:RECO?");
	size_t length = stos(m_transport->ReadReply());
	m_sampleDepth = length;
	m_sampleDepthValid = true;
	m_transport->SendCommand("DAT:START 0");
	m_transport->SendCommand(string("DAT:STOP ") + to_string(length));

	double ymult = 0;
	double yoff = 0;

	//Ask for the analog data
	m_transport->SendCommand("DAT:WID 2");					//16-bit data
	m_transport->SendCommand("DAT:ENC SRI");				//signed, little endian binary
	size_t timebase = 0;
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		// Set source & get preamble+data
		m_transport->SendCommand(string("DAT:SOU ") + m_channels[i]->GetHwname());

		//Ask for the waveform preamble
		m_transport->SendCommand("WFMO?");

		//Process it
		for(int j=0; j<22; j++)
		{
			string reply = m_transport->ReadReply();

			//LogDebug("preamble block %d = %s\n", j, reply.c_str());

			if(j == 11)
			{
				timebase = round(stof(reply) * 1e12);	//scope gives sec, not ps
				//LogDebug("xincrement = %s\n", Unit(Unit::UNIT_PS).PrettyPrint(xincrements[i]).c_str());
			}
			else if(j == 15)
			{
				ymult = stof(reply);
				//LogDebug("ymult = %s\n", Unit(Unit::UNIT_VOLTS).PrettyPrint(ymult).c_str());
			}
			else if(j == 17)
			{
				yoff = stof(reply);
				m_channelOffsets[i] = -yoff;
				//LogDebug("yoff = %s\n", Unit(Unit::UNIT_VOLTS).PrettyPrint(yoff).c_str());
			}

			//TODO: xzero is trigger time
		}

		//LogDebug("Channel %zu (%s)\n", i, m_channels[i]->GetHwname().c_str());
		LogIndenter li2;

		//Read the data blocks
		m_transport->SendCommand("CURV?");

		//Read length of the actual data
		char tmplen[3] = {0};
		m_transport->ReadRawData(2, (unsigned char*)tmplen);	//expect #n
		int ndigits = atoi(tmplen+1);

		char digits[10] = {0};
		m_transport->ReadRawData(ndigits, (unsigned char*)digits);
		int msglen = atoi(digits);

		//Read the actual data
		char* rxbuf = new char[msglen];
		m_transport->ReadRawData(msglen, (unsigned char*)rxbuf);

		//convert bytes to samples
		size_t nsamples = msglen/2;
		int16_t* samples = (int16_t*)rxbuf;

		//Set up the capture we're going to store our data into
		//(no TDC data or fine timestamping available on Tektronix scopes?)
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = timebase;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startPicoseconds = (t - floor(t)) * 1e12f;
		cap->Resize(nsamples);

		//Convert to volts
		for(size_t j=0; j<nsamples; j++)
		{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = ymult*samples[j] + yoff;
		}

		//Done, update the data
		pending_waveforms[i].push_back(cap);

		//Done
		delete[] rxbuf;

		//Throw out garbage at the end of the message (why is this needed?)
		m_transport->ReadReply();
	}

	//Get the spectrum stuff
	m_transport->SendCommand("DAT:WID 8");					//double precision floating point data
	m_transport->SendCommand("DAT:ENC SFPB");				//IEEE754 float
	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		auto nchan = m_spectrumChannelBase + i;
		if(!IsChannelEnabled(nchan))
			continue;

		// Set source & get preamble+data
		m_transport->SendCommand(string("DAT:SOU ") + m_channels[i]->GetHwname() + "_SV_NORMAL");

		//Ask for the waveform preamble
		m_transport->SendCommand("WFMO?");

		//LogDebug("Channel %zu (%s)\n", nchan, m_channels[nchan]->GetHwname().c_str());
		//LogIndenter li2;

		//Process it
		double hzbase = 0;
		for(int j=0; j<22; j++)
		{
			string reply = m_transport->ReadReply();

			//LogDebug("preamble block %d = %s\n", j, reply.c_str());

			if(j == 11)
			{
				hzbase = round(stof(reply));
				//LogDebug("xincrement = %s\n", Unit(Unit::UNIT_HZ).PrettyPrint(hzbase).c_str());
			}
			else if(j == 15)
			{
				ymult = stof(reply);
				//LogDebug("ymult = %s\n", Unit(Unit::UNIT_DBM).PrettyPrint(ymult).c_str());
			}
			else if(j == 17)
			{
				yoff = stof(reply);
				m_channelOffsets[i] = -yoff;
				//LogDebug("yoff = %s\n", Unit(Unit::UNIT_DBM).PrettyPrint(yoff).c_str());
			}

			//TODO: xzero is trigger time
		}

		//Read the data block
		m_transport->SendCommand("CURV?");

		//Read length of the actual data
		char tmplen[3] = {0};
		m_transport->ReadRawData(2, (unsigned char*)tmplen);	//expect #n
		int ndigits = atoi(tmplen+1);

		char digits[10] = {0};
		m_transport->ReadRawData(ndigits, (unsigned char*)digits);
		int msglen = atoi(digits);

		//Read the actual data
		char* rxbuf = new char[msglen];
		m_transport->ReadRawData(msglen, (unsigned char*)rxbuf);

		//convert bytes to samples
		size_t nsamples = msglen/8;
		double* samples = (double*)rxbuf;

		//Set up the capture we're going to store our data into
		//(no TDC data or fine timestamping available on Tektronix scopes?)
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = hzbase;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = time(NULL);
		double t = GetTime();
		cap->m_startPicoseconds = (t - floor(t)) * 1e12f;
		cap->Resize(nsamples);

		//We get dBm from the instrument, so just have to convert double to single precision
		for(size_t j=0; j<nsamples; j++)
		{
			cap->m_offsets[j] = j;
			cap->m_durations[j] = 1;
			cap->m_samples[j] = ymult*samples[j] + yoff;
		}

		//Done, update the data
		pending_waveforms[nchan].push_back(cap);

		//Done
		delete[] rxbuf;

		//Throw out garbage at the end of the message (why is this needed?)
		m_transport->ReadReply();
	}

	//Get the digital stuff
	m_transport->SendCommand("DAT:WID 1");					//8 data bits per channel
	m_transport->SendCommand("DAT:ENC SRI");				//signed, little endian binary
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

		//Ask for all of the data
		m_transport->SendCommand(string("DAT:SOU CH") + to_string(i+1) + "_DALL");

		//Ask for the waveform preamble
		m_transport->SendCommand("WFMO?");

		//Process it
		for(int j=0; j<22; j++)
		{
			string reply = m_transport->ReadReply();

			//LogDebug("preamble block %d = %s\n", j, reply.c_str());

			if(j == 11)
				timebase = round(stof(reply) * 1e12);	//scope gives sec, not ps
		}

		m_transport->SendCommand("CURV?");

		//Read length of the actual data
		char tmplen[3] = {0};
		m_transport->ReadRawData(2, (unsigned char*)tmplen);	//expect #n
		int ndigits = atoi(tmplen+1);

		char digits[10] = {0};
		m_transport->ReadRawData(ndigits, (unsigned char*)digits);
		int msglen = atoi(digits);

		//Read the actual data
		char* rxbuf = new char[msglen];
		m_transport->ReadRawData(msglen, (unsigned char*)rxbuf);

		//Process the data for each channel
		for(int j=0; j<8; j++)
		{
			//Set up the capture we're going to store our data into
			//(no TDC data or fine timestamping available on Tektronix scopes?)
			DigitalWaveform* cap = new DigitalWaveform;
			cap->m_timescale = timebase;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startPicoseconds = (t - floor(t)) * 1e12f;
			cap->Resize(msglen);

			//Extract sample data
			int mask = (1 << j);
			for(int k=0; k<msglen; k++)
			{
				cap->m_offsets[k] = k;
				cap->m_durations[k] = 1;
				cap->m_samples[k] = (rxbuf[k] & mask) ? true : false;
			}

			//Done, update the data
			pending_waveforms[m_digitalChannelBase + i*8 + j].push_back(cap);
		}

		//Done
		delete[] rxbuf;

		//Throw out garbage at the end of the message (why is this needed?)
		m_transport->ReadReply();
	}

	return true;
}

void TektronixOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixOscilloscope::Stop()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQ:STATE STOP");
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
	const int64_t m = k*k;

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
				ret.push_back(1 * m);
				ret.push_back(2 * m);
				ret.push_back(5 * m);
				ret.push_back(10 * m);
				ret.push_back(20 * m);
				ret.push_back(50 * m);
				ret.push_back(62500 * k);
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
	//don't bother with mutexing, worst case we return slightly stale data
	if(m_sampleRateValid)
		return m_sampleRate;

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand("HOR:MODE:SAMPLER?");
			m_sampleRate = stod(m_transport->ReadReply());	//stoull seems to not handle scientific notation
			break;

		default:
			return 1;
	}

	m_sampleRateValid = true;
	return m_sampleRate;
}

uint64_t TektronixOscilloscope::GetSampleDepth()
{
	if(m_sampleDepthValid)
		return m_sampleDepth;

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand("HOR:MODE:RECO?");
			m_sampleDepth = stos(m_transport->ReadReply());
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
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand(string("HOR:MODE:RECO ") + to_string(depth));
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
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			m_transport->SendCommand(string("HOR:MODE:SAMPLER ") + to_string(rate));
			break;

		default:
			break;
	}
}

void TektronixOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
		{
			//Instrument reports position of trigger from the midpoint of the display
			//but we want to know position from the start of the capture
			double capture_len_sec = 1.0 * GetSampleDepth() / GetSampleRate();
			double offset_sec = offset * 1e-12f;
			double center_offset_sec = capture_len_sec/2 - offset_sec;

			m_transport->SendCommand(string("HOR:DELAY:TIME ") + to_string(center_offset_sec));

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

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
		{
			//Instrument reports position of trigger from the midpoint of the display
			m_transport->SendCommand("HOR:DELAY:TIME?");
			double center_offset_sec = stod(m_transport->ReadReply());

			//but we want to know position from the start of the capture
			double capture_len_sec = 1.0 * GetSampleDepth() / GetSampleRate();
			double offset_sec = capture_len_sec/2 - center_offset_sec;

			//All good, convert to ps and we're done
			m_triggerOffset = round(offset_sec * 1e12);
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

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			//Tek's skew convention has positive values move the channel EARLIER, so we need to flip sign
			m_transport->SendCommand(m_channels[channel]->GetHwname() + ":DESK " + to_string(-skew) + "E-12");
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
				m_transport->SendCommand(m_channels[channel]->GetHwname() + ":DESK?");
				deskew = -round(1e12 * stof(m_transport->ReadReply()));
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

void TektronixOscilloscope::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommand("TRIG:A:TYP?");
				string reply = m_transport->ReadReply();

				if(reply == "EDG")
					PullEdgeTrigger();
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

	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				//Source channel
				m_transport->SendCommand("TRIG:A:EDGE:SOU?");
				auto reply = m_transport->ReadReply();
				et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

				//Trigger level
				m_transport->SendCommand("TRIG:A:LEV?");
				et->SetLevel(stof(m_transport->ReadReply()));

				//For some reason we get 3 more values after this. Discard them.
				for(int i=0; i<3; i++)
					m_transport->ReadReply();

				//Edge slope
				m_transport->SendCommand("TRIG:A:EDGE:SLO?");
				reply = m_transport->ReadReply();
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
			m_transport->SendCommand("TRIG:SOUR?");
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

			m_transport->SendCommand("TRIG:LEV?");
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

void TektronixOscilloscope::PushTrigger()
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
void TektronixOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			{
				m_transport->SendCommand(string("TRIG:A:EDGE:SOU ") + trig->GetInput(0).m_channel->GetHwname());
				m_transport->SendCommand(
					string("TRIG:A:LEV:") + trig->GetInput(0).m_channel->GetHwname() + " " +
					to_string(trig->GetLevel()));

				switch(trig->GetType())
				{
					case EdgeTrigger::EDGE_RISING:
						m_transport->SendCommand("TRIG:A:EDGE:SLO RIS");
						break;

					case EdgeTrigger::EDGE_FALLING:
						m_transport->SendCommand("TRIG:A:EDGE:SLO FALL");
						break;

					case EdgeTrigger::EDGE_ANY:
						m_transport->SendCommand("TRIG:A:EDGE:SLO ANY");
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
				m_transport->SendCommand(tmp);
			}
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

	lock_guard<recursive_mutex> lock(m_mutex);

	auto chan = m_channels[channel];

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			//note, group IDs are one based but lane IDs are zero based!
			m_transport->SendCommand(string("DIGGRP") + to_string(m_flexChannelParents[chan]+1) +
				":D" + to_string(m_flexChannelLanes[chan]) + ":THR?");
			return stof(m_transport->ReadReply());

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
	lock_guard<recursive_mutex> lock(m_mutex);
	auto chan = m_channels[channel];

	switch(m_family)
	{
		case FAMILY_MSO5:
		case FAMILY_MSO6:
			//note, group IDs are one based but lane IDs are zero based!
			m_transport->SendCommand(string("DIGGRP") + to_string(m_flexChannelParents[chan]+1) +
				":D" + to_string(m_flexChannelLanes[chan]) + ":THR " + to_string(level));
			break;

		default:
			break;
	}
}
