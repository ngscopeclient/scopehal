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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

RigolOscilloscope::RigolOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_triggerArmed(false)
	, m_triggerWasLive(false)
	, m_triggerOneShot(false)
	, m_liveMode(false)
{
	//Last digit of the model number is the number of channels
	if(1 == sscanf(m_model.c_str(), "DS%d", &m_modelNumber))
	{
		if(m_model.size() >= 7 && (m_model[6] == 'D' || m_model[6] == 'E'))
			m_protocol = DS_OLD;
		else
			m_protocol = DS;
	}
	else if(1 == sscanf(m_model.c_str(), "MSO%d", &m_modelNumber))
	{
		m_protocol = MSO5;
		// Hacky workaround since :SYST:OPT:STAT doesn't work properly on some scopes
		// Only enable chan 1
		m_transport->SendCommandQueued("CHAN1:DISP 1\n");
		m_transport->SendCommandQueued("CHAN2:DISP 0\n");
		if(m_modelNumber % 10 > 2)
		{
			m_transport->SendCommandQueued("CHAN3:DISP 0\n");
			m_transport->SendCommandQueued("CHAN4:DISP 0\n");
		}
		// Set in run mode to be able to set memory depth
		m_transport->SendCommandQueued("RUN\n");

		m_transport->SendCommandQueued("ACQ:MDEP 200M\n");
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("ACQ:MDEP?\n"));
		m_opt200M = reply == "2.0000E+08" ?
						true :
						false;	  // Yes, it actually returns a stringified float, manual says "scientific notation"

		// Reset memory depth
		m_transport->SendCommandQueued("ACQ:MDEP 1M\n");
		string originalBandwidthLimit = m_transport->SendCommandQueuedWithReply("CHAN1:BWL?");

		// Figure out its actual bandwidth since :SYST:OPT:STAT is practically useless
		m_transport->SendCommandQueued("CHAN1:BWL 200M\n");
		reply = Trim(m_transport->SendCommandQueuedWithReply("CHAN1:BWL?\n"));

		// A bit of a tree, maybe write more beautiful code
		if(reply == "200M")
			m_bandwidth = 350;
		else
		{
			m_transport->SendCommandQueued("CHAN1:BWL 100M\n");
			reply = Trim(m_transport->SendCommandQueuedWithReply("CHAN1:BWL?\n"));
			if(reply == "100M")
				m_bandwidth = 200;
			else
			{
				if(m_modelNumber % 1000 - m_modelNumber % 10 == 100)
					m_bandwidth = 100;
				else
					m_bandwidth = 70;
			}
		}

		m_transport->SendCommandQueued("CHAN1:BWL " + originalBandwidthLimit);
	}
	else if(1 == sscanf(m_model.c_str(), "DHO%d", &m_modelNumber) && (m_modelNumber < 5000))
	{	// Model numbers are :
		// - DHO802 (70MHz), DHO804 (70Mhz), DHO812 (100MHz),DHO814 (100MHz)
	    // - DHO914/DHO914S (125MHz), DHO924/DHO924S (250MHz)
		// - DHO1072 (70MHz), DHO1074 (70MHz), DHO1102 (100MHz), DHO1104 (100MHz), DHO1202 (200MHz), DHO1204 (200MHz)
		// - DHO4204 (200MHz), DHO4404 (400 MHz), DHO4804 (800MHz)
		m_protocol = DHO;

		int model_multiplicator = 100;
		int model_modulo = 100;
		if(m_modelNumber > 1000)
		{	// DHO1000 and 4000
			model_multiplicator = 10;
			model_modulo = 1000;
		}
		else if(m_modelNumber > 900)
		{	// special handling of DHO900 series
			model_multiplicator = 125;
		}
		m_bandwidth = m_modelNumber % model_modulo / 10 * model_multiplicator;
		if(m_bandwidth == 0) m_bandwidth = 70; // Fallback for DHO80x models

		m_opt200M = false;	  // does not exist in 800/900 series
		m_lowSrate = false;

		if (m_modelNumber > 4000 && m_modelNumber < 5000) {
			m_maxMdepth = 250*1000*1000;
			m_maxSrate  = 4*1000*1000*1000U;
			/* probe for bandwidth upgrades and memory upgrades on DHO4000 series */
			auto reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? RLU\n"));
			if (reply == "1")
				m_maxMdepth = 500*1000*1000;
			
			reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW2T4\n"));
			if (reply == "1")
				m_bandwidth = 400;

			reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW2T8\n"));
			if (reply == "1")
				m_bandwidth = 800;

			reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW4T8\n"));
			if (reply == "1")
				m_bandwidth = 800;
		}
		else if  (m_modelNumber > 1000 && m_modelNumber < 2000) {
			m_maxMdepth = 50*1000*1000;
			m_maxSrate  = 2*1000*1000*1000;
			/* probe for bandwidth upgrades and memory upgrades on DHO1000 series */
			auto reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? RLU\n"));
			if (reply == "1")
				m_maxMdepth = 100*1000*1000;
			
			reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW7T10\n"));
			if (reply == "1")
				m_bandwidth = 100;

			reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW7T20\n"));
			if (reply == "1")
				m_bandwidth = 200;

			reply = Trim(m_transport->SendCommandQueuedWithReply(":SYST:OPT:STAT? BW10T20\n"));
			if (reply == "1")
				m_bandwidth = 200;
		}
		else
		{	// DHO800/900 (DHO800 also have 50M memory since firmware v00.01.03.00.04  2024/07/11)
			m_maxMdepth = 50*1000*1000;
			m_maxSrate  = 1.25*1000*1000*1000;
			m_lowSrate = true;
		}
	}
	else
	{
		LogError("Bad model number\n");
		return;
	}

	// Maybe fix this in a similar manner to bandwidth
	int nchans = m_modelNumber % 10;

	if((m_protocol != MSO5) && (m_protocol != DHO))
		m_bandwidth = m_modelNumber % 1000 - nchans;

	for(int i = 0; i < nchans; i++)
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
	m_analogChannelCount = nchans;

	//Add the external trigger input
	m_extTrigChannel = new OscilloscopeChannel(
		this, "EX", "", Unit(Unit::UNIT_FS), Unit(Unit::UNIT_VOLTS), Stream::STREAM_TYPE_TRIGGER, m_channels.size());
	m_channels.push_back(m_extTrigChannel);
	m_extTrigChannel->SetDefaultDisplayName();

	//Configure acquisition modes
	if(m_protocol == DS_OLD)
		m_transport->SendCommandQueued(":WAV:POIN:MODE RAW");
	else
	{
		m_transport->SendCommandQueued(":WAV:FORM BYTE");
		m_transport->SendCommandQueued(":WAV:MODE RAW");
	}
	if(m_protocol == MSO5 || m_protocol == DS_OLD || m_protocol == DHO)
	{
		for(size_t i = 0; i < m_analogChannelCount; i++)
			m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":VERN ON");
	}
	if(m_protocol == MSO5 || m_protocol == DS || m_protocol == DHO)
		m_transport->SendCommandQueued(":TIM:VERN ON");
	FlushConfigCache();

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

	m_srateValid = false;
	m_mdepthValid = false;
	m_triggerOffsetValid = false;

	delete m_trigger;
	m_trigger = NULL;
}

bool RigolOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//TODO: handle digital channels, for now just claim they're off
	if(i >= m_analogChannelCount)
		return false;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":DISP?"));
	if(reply == "0")
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = false;
		return false;
	}
	else
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelsEnabled[i] = true;
		return true;
	}
}

void RigolOscilloscope::EnableChannel(size_t i)
{
	m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":DISP ON");
	// invalidate channel enable cache until confirmed on next IsChannelEnabled
	m_channelsEnabled.erase(i);
}

void RigolOscilloscope::DisableChannel(size_t i)
{
	m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":DISP OFF");
	// invalidate channel enable cache until confirmed on next IsChannelEnabled
	m_channelsEnabled.erase(i);
}

vector<OscilloscopeChannel::CouplingType> RigolOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	//TODO: some higher end models do have 50 ohm inputs... which ones?
	//ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);
	return ret;
}

OscilloscopeChannel::CouplingType RigolOscilloscope::GetChannelCoupling(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelCouplings.find(i) != m_channelCouplings.end())
			return m_channelCouplings[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":COUP?"));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(reply == "AC")
		m_channelCouplings[i] = OscilloscopeChannel::COUPLE_AC_1M;
	else if(reply == "DC")
		m_channelCouplings[i] = OscilloscopeChannel::COUPLE_DC_1M;
	else /* if(reply == "GND") */
		m_channelCouplings[i] = OscilloscopeChannel::COUPLE_GND;
	return m_channelCouplings[i];
}

void RigolOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
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
			LogError("Invalid coupling for channel\n");
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
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelAttenuations.find(i) != m_channelAttenuations.end())
			return m_channelAttenuations[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":PROB?"));

	double atten;
	sscanf(reply.c_str(), "%lf", &atten);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelAttenuations[i] = atten;
	return atten;
}

void RigolOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	bool valid = true;
	switch((
		int)(atten * 10000 +
			 0.1))	  //+ 0.1 in case atten is for example 0.049999 or so, to round it to 0.05 which turns to an int of 500
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
			LogError("Invalid attenuation for channel\n");
			valid = false;
	}

	if(valid)
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelAttenuations[i] = (int)(atten * 10000 + 0.1) * 0.0001;
	}
}

vector<unsigned int> RigolOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;

	if(m_protocol == MSO5)
	{
		switch(m_bandwidth)
		{
			case 70:
			case 100:
				ret = {20, 0};
				break;
			case 200:
				ret = {20, 100, 0};
				break;
			case 350:
				ret = {20, 100, 200, 0};
				break;
			default:
				LogError("Invalid model bandwidth\n");
		}
	}

	//For now, all known DS series models only support 20 MHz or off
	else if(m_protocol == DS || m_protocol == DHO)
	{
		ret = {20, 0};
	}
	return ret;
}

unsigned int RigolOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelBandwidthLimits.find(i) != m_channelBandwidthLimits.end())
			return m_channelBandwidthLimits[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":BWL?"));

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	if(reply == "20M")
		m_channelBandwidthLimits[i] = 20;
	if(reply == "100M")
		m_channelBandwidthLimits[i] = 100;
	if(reply == "200M")
		m_channelBandwidthLimits[i] = 200;
	else
		m_channelBandwidthLimits[i] = m_bandwidth;
	return m_channelBandwidthLimits[i];
}

void RigolOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	bool valid = true;

	if(m_protocol == MSO5 || m_protocol == DHO)
	{
		switch(m_bandwidth)
		{
			case 70:
			case 100:
			case 125:
				if((limit_mhz <= 20) & (limit_mhz != 0))
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL 20M");
				else
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL OFF");
				break;
			case 200:
			case 250:
				if((limit_mhz <= 20) & (limit_mhz != 0))
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL 20M");
				else if((limit_mhz <= 100) & (limit_mhz != 0))
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL 100M");
				else
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL OFF");
				break;
			case 350:
			case 400:
			case 800:
				if((limit_mhz <= 20) & (limit_mhz != 0))
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL 20M");
				else if((limit_mhz <= 100) & (limit_mhz != 0))
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL 100M");
				else if((limit_mhz <= 200) & (limit_mhz != 0))
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL 200M");
				else
					m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL OFF");
				break;
			default:
				LogError("Invalid model number\n");
				valid = false;
		}
	}
	else if(m_protocol == DS)
	{
		if((limit_mhz <= 20) & (limit_mhz != 0))
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL 20M");
		else
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":BWL OFF");
	}
	else
		LogError("unimplemented SetChannelBandwidth for this model\n");

	if(valid)
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		if(limit_mhz == 0)
			m_channelBandwidthLimits[i] = m_bandwidth;	  // max
		else if(limit_mhz <= 20)
			m_channelBandwidthLimits[i] = 20;
		else if(m_bandwidth == 70)
			m_channelBandwidthLimits[i] = 70;
		else if((limit_mhz <= 100) | (m_bandwidth == 100))
			m_channelBandwidthLimits[i] = 100;
		else if((limit_mhz <= 200) | (m_bandwidth == 200))
			m_channelBandwidthLimits[i] = 200;
		else
			m_channelBandwidthLimits[i] = m_bandwidth;	  // 350 MHz
	}
}

float RigolOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	string reply;
	if(m_protocol == DS)
		reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":RANGE?"));
	else if(m_protocol == MSO5 || m_protocol == DS_OLD || m_protocol == DHO)
		reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":SCALE?"));

	float range;
	sscanf(reply.c_str(), "%f", &range);
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(m_protocol == MSO5 || m_protocol == DHO)
		range = 8 * range;
	if(m_protocol == DS_OLD)
		range = 10 * range;
	m_channelVoltageRanges[i] = range;

	return range;
}

void RigolOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_channelVoltageRanges[i] = range;
	}

	if(m_protocol == DS)
		m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":RANGE " + to_string(range));
	else if(m_protocol == MSO5 || m_protocol == DS_OLD || m_protocol == DHO)
		m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":SCALE " + to_string(range / 8));
}

OscilloscopeChannel* RigolOscilloscope::GetExternalTrigger()
{
	//FIXME
	return nullptr;
}

float RigolOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":OFFS?"));

	float offset;
	sscanf(reply.c_str(), "%f", &offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void RigolOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	m_transport->SendCommandQueued(":" + m_channels[i]->GetHwname() + ":OFFS " + to_string(offset));

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

Oscilloscope::TriggerMode RigolOscilloscope::PollTrigger()
{
	if(m_liveMode)
		return TRIGGER_MODE_TRIGGERED;

	auto stat = Trim(m_transport->SendCommandQueuedWithReply(":TRIG:STAT?"));

	if(stat != "STOP")
		m_triggerWasLive = true;

	if(stat == "TD")
		return TRIGGER_MODE_TRIGGERED;
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
		if(m_triggerArmed && (m_protocol != DS_OLD || m_triggerWasLive))
		{
			m_triggerArmed = false;
			m_triggerWasLive = false;
			return TRIGGER_MODE_TRIGGERED;
		}

		//Nope, we're actually stopped
		return TRIGGER_MODE_STOP;
	}
}

bool RigolOscilloscope::AcquireData()
{
	//LogDebug("Acquiring data\n");

	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	LogIndenter li;

	// Rigol scopes do not have a capture time so we fake it
	double now = GetTime();

	//Grab the analog waveform data
	int unused1;
	int unused2;
	size_t npoints;
	int unused3;
	double sec_per_sample;
	double xorigin;
	double xreference;
	double yincrement;
	double yorigin;
	double yreference;
	size_t maxpoints = 250 * 1000;
	if(m_protocol == DS)
		maxpoints = 250 * 1000;
	else if(m_protocol == DS_OLD)
		maxpoints = 8192;	 // FIXME
	else if(m_protocol == MSO5)
		maxpoints = GetSampleDepth();	 //You can use 250E6 points too, but it is very slow

	unsigned char* temp_buf = new unsigned char[maxpoints + 1];
	map<int, vector<UniformAnalogWaveform*>> pending_waveforms;
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		//LogDebug("Channel %zu\n", i);

		int64_t fs_per_sample = 0;

		if(m_protocol == DS_OLD)
		{
			yreference = 0;
			npoints = maxpoints;

			yincrement = GetChannelVoltageRange(i, 0) / 256.0f;
			yorigin = GetChannelOffset(i, 0);

			auto reply = Trim(m_transport->SendCommandQueuedWithReply(":" + m_channels[i]->GetHwname() + ":OFFS?"));
			sscanf(reply.c_str(), "%lf", &yorigin);

			/* for these scopes, this is seconds per div */
			reply = Trim(m_transport->SendCommandQueuedWithReply(":TIM:SCAL?"));
			sscanf(reply.c_str(), "%lf", &sec_per_sample);
			fs_per_sample = (sec_per_sample * 12 * FS_PER_SECOND) / npoints;
		}
		else
		{
			m_transport->SendCommandQueued(string("WAV:SOUR ") + m_channels[i]->GetHwname());

			//This is basically the same function as a LeCroy WAVEDESC, but much less detailed
			auto reply = Trim(m_transport->SendCommandQueuedWithReply("WAV:PRE?"));
			//LogDebug("Preamble = %s\n", reply.c_str());
			sscanf(reply.c_str(),
				"%d,%d,%zu,%d,%lf,%lf,%lf,%lf,%lf,%lf",
				&unused1,
				&unused2,
				&npoints,
				&unused3,
				&sec_per_sample,
				&xorigin,
				&xreference,
				&yincrement,
				&yorigin,
				&yreference);
			if(sec_per_sample == 0)
			{	// Sometimes the scope might return a null value for xincrement => ignore waveform to prenvent an Arithmetic exception in WaveformArea::RasterizeAnalogOrDigitalWaveform 
				LogWarning("Got null sec_per_sample value from the scope, ignoring this waveform.\n");
				continue;
			}
			fs_per_sample = round(sec_per_sample * FS_PER_SECOND);
			if(m_protocol == DHO)
			{	// DHO models return page size instead of memory depth when paginating
				npoints = GetSampleDepth();
			}
			//LogDebug("X: %d points, %f origin, ref %f fs/sample %ld\n", (int) npoints, xorigin, xreference, (long int) fs_per_sample);
			//LogDebug("Y: %f inc, %f origin, %f ref\n", yincrement, yorigin, yreference);
		}

		//If we have zero points in the reply, skip reading data from this channel
		if(npoints == 0)
			continue;

		//Set up the capture we're going to store our data into
		auto cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(i)->GetHwname());
		cap->Resize(0);
		cap->m_timescale = fs_per_sample;
		cap->m_triggerPhase = 0;
		cap->m_startTimestamp = floor(now);
		cap->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;
		
		//Downloading the waveform is a pain in the butt, because we can only pull 250K points at a time! (Unless you have a MSO5)
		for(size_t npoint = 0; npoint < npoints;)
		{
			if(m_protocol == MSO5)
			{
				//Ask for the data block
				m_transport->SendCommandQueued("*WAI");
				m_transport->SendCommandQueued("WAV:DATA?");
			}
			else if(m_protocol == DS_OLD)
			{
				m_transport->SendCommandQueued(string(":WAV:DATA? ") + m_channels[i]->GetHwname());
			}
			else
			{
				//Ask for the data
				m_transport->SendCommandQueued(string("WAV:STAR ") + to_string(npoint + 1));	//ONE based indexing WTF
				size_t end = npoint + maxpoints;
				if(end > npoints)
					end = npoints;
				m_transport->SendCommandQueued(
					string("WAV:STOP ") + to_string(end));	  //Here it is zero based, so it gets from 1-1000

				//Ask for the data block
				m_transport->SendCommandQueued("WAV:DATA?");
			}
			m_transport->FlushCommandQueue();

			//Read block header, should be maximally 11 long on MSO5 scope with >= 100 MPoints
			unsigned char header[12] = {0};

			unsigned char header_size;
			m_transport->ReadRawData(2, header);
			//LogWarning("Time %f\n", (GetTime() - start));

			sscanf((char*)header, "#%c", &header_size);
			header_size = header_size - '0';

			if(header_size > 12)
			{
				header_size = 12;
			}

			m_transport->ReadRawData(header_size, header);

			//Look up the block size
			//size_t blocksize = end - npoints;
			//LogDebug("Block size = %zu\n", blocksize);
			size_t header_blocksize;
			sscanf((char*)header, "%zu", &header_blocksize);
			//LogDebug("Header block size = %zu\n", header_blocksize);

			if(header_blocksize == 0)
			{
				LogWarning("Ran out of data after %zu points\n", npoint);
				m_transport->ReadRawData(1, temp_buf);	  //discard the trailing newline

				//If this happened after zero samples, free the waveform so it doesn't leak
				if(npoint == 0)
				{
					AddWaveformToAnalogPool(cap);
					cap = nullptr;
				}
				break;
			}

			if(header_blocksize > maxpoints)
			{
				header_blocksize = maxpoints;
			}

			//Read actual block content and decode it
			//Scale: (value - Yorigin - Yref) * Yinc
			m_transport->ReadRawData(header_blocksize + 1, temp_buf);	 //trailing newline after data block

			double ydelta = yorigin + yreference;
			cap->Resize(cap->m_samples.size() + header_blocksize);
			cap->PrepareForCpuAccess();
			for(size_t j = 0; j < header_blocksize; j++)
			{
				float v = (static_cast<float>(temp_buf[j]) - ydelta) * yincrement;
				if(m_protocol == DS_OLD)
					v = (128 - static_cast<float>(temp_buf[j])) * yincrement - ydelta;
				//LogDebug("V = %.3f, temp=%d, delta=%f, inc=%f\n", v, temp_buf[j], ydelta, yincrement);
				cap->m_samples[npoint + j] = v;
			}
			cap->MarkSamplesModifiedFromCpu();

			npoint += header_blocksize;
		}

		//Done, update the data
		if(cap)
			pending_waveforms[i].push_back(cap);
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	   //TODO: segmented capture support
	for(size_t i = 0; i < num_pending; i++)
	{
		SequenceSet s;
		for(size_t j = 0; j < m_analogChannelCount; j++)
		{
			if(pending_waveforms.count(j) > 0)
				s[GetOscilloscopeChannel(j)] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//Clean up
	delete[] temp_buf;

	//TODO: support digital channels

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		if(m_protocol == DS_OLD)
		{
			m_transport->SendCommandQueued(":STOP");
			m_transport->SendCommandQueued(":TRIG:EDGE:SWE SING");
			m_transport->SendCommandQueued(":RUN");
		}
		else
		{
			if(!m_liveMode)
			{
				m_transport->SendCommandQueued(":SING");
				m_transport->SendCommandQueued("*WAI");
			}
		}
		m_triggerArmed = true;
	}

	//LogDebug("Acquisition done\n");

	return true;
}

void RigolOscilloscope::PrepareStart()
{
	if(m_protocol == DHO)
	{
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
	}
}

void RigolOscilloscope::Start()
{
	//LogDebug("Start single trigger\n");
	if(m_protocol == DS_OLD)
	{
		m_transport->SendCommandQueued(":TRIG:EDGE:SWE SING");
		m_transport->SendCommandQueued(":RUN");
	}
	else if(m_protocol == DHO)
	{	// Check for memory depth : if it is 1k, switch to live mode for better performance
		// Limit live mode to one channel setup to prevent grabbing waveforms from to different triggers on seperate channels
		if(GetEnabledChannelCount()==1)
		{
			m_mdepthValid = false;
			GetSampleDepth();
			m_liveMode = (m_mdepth == 1000);
		}
		else
		{
			m_liveMode = false;
		}
		PrepareStart();
		m_transport->SendCommandQueued(m_liveMode ? ":RUN" : ":SING");
		m_transport->SendCommandQueued("*WAI");
	}
	else
	{
		m_transport->SendCommandQueued(":SING");
		m_transport->SendCommandQueued("*WAI");
	}
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void RigolOscilloscope::StartSingleTrigger()
{
	m_liveMode = false;
	m_mdepthValid = false; // Memory depth might have been changed on scope
	PrepareStart();
	if(m_protocol == DS_OLD)
	{
		m_transport->SendCommandQueued(":TRIG:EDGE:SWE SING");
		m_transport->SendCommandQueued(":RUN");
	}
	else
	{
		m_transport->SendCommandQueued(":SING");
		m_transport->SendCommandQueued("*WAI");
	}
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void RigolOscilloscope::Stop()
{
	m_transport->SendCommandQueued(":STOP");
	m_liveMode = false;
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

void RigolOscilloscope::ForceTrigger()
{
	m_liveMode = false;
	m_mdepthValid = false; // Memory depth might have been changed on scope
	PrepareStart();
	if(m_protocol == DS || m_protocol == DHO)
		m_transport->SendCommandQueued(":TFOR");
	else
		LogError("RigolOscilloscope::ForceTrigger not implemented for this model\n");
}

bool RigolOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

static std::map<uint64_t, double> dhoLowSampleRates {
	// Available sample rates for DHO 800/900 models, regardless of activated channels
	// Map each sample rate to its horizontal scale ratio (this ratio changes based on srate on DHO models)
	{ 200, 10},
	{ 500, 10},
	{ 1000, 10},
	{ 2000, 10},
	{ 5000, 10},
	{ 10 * 1000, 10},
	{ 20 * 1000, 10},
	{ 50 * 1000, 10},
	{ 100 * 1000, 10},
	{ 125 * 1000, 16},
	{ 250 * 1000, 20},
	{ 500 * 1000, 20},
	{ 1250   * 1000, 16},
	{ 2500   * 1000, 20},
	{ 6250   * 1000, 16},
	{ 12500  * 1000, 16},
	{ 31250  * 1000, 16},
	{ 62500  * 1000, 16},
	{ 156250 * 1000, 12.8},
	{ 312500 * 1000, 16},
	{ 625000 * 1000, 16},
	{ 1250   * 1000 * 1000, 16},
};

static std::map<uint64_t, double> dhoHighSampleRates {
	// Available sample rates for DHO 1000/4000 models, regardless of activated channels
	// Map each sample rate to its horizontal scale ratio (this ratio changes based on srate on DHO models)
	{ 100, 10},
	{ 200, 10},
	{ 500, 10},
	{ 1000, 10},
	{ 2000, 10},
	{ 5000, 10},
	{ 10 * 1000, 10},
	{ 20 * 1000, 10},
	{ 50 * 1000, 10},
	{ 100 * 1000, 10},
	{ 200 * 1000, 10},
	{ 500 * 1000, 10},
	{ 1 * 1000 * 1000, 10},
	{ 2 * 1000 * 1000, 10},
	{ 5 * 1000 * 1000, 10},
	{ 10 * 1000 * 1000, 10},
	{ 20 * 1000 * 1000, 10},
	{ 50 * 1000 * 1000, 10},
	{ 100 * 1000 * 1000, 10},
	{ 200 * 1000 * 1000, 10},
	{ 500 * 1000 * 1000, 10},
	{ 1 * 1000 * 1000 * 1000, 10},
	{ 2 * 1000 * 1000 * 1000, 10},
	{ 4 * 1000 * 1000 * 1000U, 10},
};

vector<uint64_t> RigolOscilloscope::GetSampleRatesNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	if(m_protocol == MSO5)
	{
		ret = {
			100,
			200,
			500,
			1000,
			2000,
			5000,
			10 * 1000,
			20 * 1000,
			50 * 1000,
			100 * 1000,
			200 * 1000,
			500 * 1000,
			1 * 1000 * 1000,
			2 * 1000 * 1000,
			5 * 1000 * 1000,
			10 * 1000 * 1000,
			20 * 1000 * 1000,
			50 * 1000 * 1000,
			100 * 1000 * 1000,
			200 * 1000 * 1000,
			500 * 1000 * 1000,
			1 * 1000 * 1000 * 1000,
			2 * 1000 * 1000 * 1000,
		};
	}
	else if(m_protocol == DHO)
	{	// For DHO model, srates depend on high/low sample rate models, max srate and number of enabled channels
		uint64_t maxSrate = m_maxSrate / GetEnabledChannelCount();
		uint64_t curSrate;
		for (auto curSrateItem : (m_lowSrate ? dhoLowSampleRates : dhoHighSampleRates))
		{
			curSrate = curSrateItem.first;
			if(curSrate<=maxSrate)
			{
				ret.push_back(curSrate);
			}
			else break;
		}
	}
	else
		LogError("RigolOscilloscope::GetSampleRatesNonInterleaved not implemented for this model\n");
	return ret;
}

vector<uint64_t> RigolOscilloscope::GetSampleRatesInterleaved()
{
	//FIXME
	vector<uint64_t> ret = {};
	LogError("RigolOscilloscope::GetSampleRatesInterleaved not implemented for this model\n");
	return ret;
}

set<Oscilloscope::InterleaveConflict> RigolOscilloscope::GetInterleaveConflicts()
{
	if(m_protocol == DHO)
	{	// No interleave conflicts possible on DHO models
		return {};
	}
	//FIXME
	set<Oscilloscope::InterleaveConflict> ret;
	LogError("RigolOscilloscope::GetInterleaveConflicts not implemented for this model\n");
	return ret;
}

static std::vector<uint64_t> dhoSampleDepths {
	{ 	// Available sample depths for DHO models, regardless of model type and activated channels
		1000,
		10*1000,
		100*1000,
		1*1000*1000,
		10*1000*1000,
		25*1000*1000,
		50*1000*1000,
		100*1000*1000,
		250*1000*1000,
		500*1000*1000,
	}
};

vector<uint64_t> RigolOscilloscope::GetSampleDepthsNonInterleaved()
{
	//FIXME
	vector<uint64_t> ret;
	if(m_protocol == MSO5)
	{
		ret = {
			1000,
			10 * 1000,
			100 * 1000,
			1000 * 1000,
			10 * 1000 * 1000,
			25 * 1000 * 1000,
		};
	}
	else if(m_protocol == DHO)
	{	// Mdepth depends on model (maxMemDepth) and number of enabled channels
		uint64_t maxMemDepth = m_maxMdepth / GetEnabledChannelCount();
		for (auto curMemDepth : dhoSampleDepths)
		{
			if(curMemDepth<=maxMemDepth)
			{
				ret.push_back(curMemDepth);
			}
			else break;
		}
	}
	else
		LogError("RigolOscilloscope::GetSampleDepthsNonInterleaved not implemented for this model\n");
	return ret;
}

vector<uint64_t> RigolOscilloscope::GetSampleDepthsInterleaved()
{
	if(m_protocol == DHO)
	{	// Sample Depths are dynamical (depending on the number of active channels) in DHO models
		return GetSampleDepthsNonInterleaved();
	}
	else
	{
		//FIXME
		vector<uint64_t> ret;
		LogError("RigolOscilloscope::GetSampleDepthsInterleaved not implemented for this model\n");
		return ret;
	}
}

uint64_t RigolOscilloscope::GetSampleRate()
{
	if(m_srateValid)
		return m_srate;

	auto ret = Trim(m_transport->SendCommandQueuedWithReply(":ACQ:SRAT?"));

	double rate;
	sscanf(ret.c_str(), "%lf", &rate);
	m_srate = (uint64_t)rate;
	m_srateValid = true;
	return rate;
}

uint64_t RigolOscilloscope::GetSampleDepth()
{
	if(m_mdepthValid)
		return m_mdepth;

	auto ret = Trim(m_transport->SendCommandQueuedWithReply(":ACQ:MDEP?"));

	double depth;
	sscanf(ret.c_str(), "%lf", &depth);
	m_mdepth = (uint64_t)depth;
	m_mdepthValid = true;
	return m_mdepth;
}

void RigolOscilloscope::SetSampleDepth(uint64_t depth)
{
	if(m_protocol == MSO5)
	{
		// The MSO5 series will only process a sample depth setting if the oscilloscope is in auto or normal mode.
		// It's frustrating, but to accommodate, we'll grab the current mode and status for restoration later, then stick the
		// scope into auto mode
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
	}
	else if(m_protocol == DHO)
	{	// DHO models
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
				m_transport->SendCommandQueued("ACQ:MDEP 50M");
				break;
			case 100000000:
				m_transport->SendCommandQueued("ACQ:MDEP 100M");
				break;
			case 250000000:
				m_transport->SendCommandQueued("ACQ:MDEP 250M");
				break;
			case 500000000:
				m_transport->SendCommandQueued("ACQ:MDEP 500M");
				break;
			default:
				LogError("Invalid memory depth for channel: %" PRIu64 "\n", depth);
		}
	}
	else
	{
		LogError("Memory depth setting not implemented for this series");
	}
	m_mdepthValid = false;
}

void RigolOscilloscope::SetSampleRate(uint64_t rate)
{
	//FIXME, you can set :TIMebase:SCALe
	m_mdepthValid = false;
	double sampletime = GetSampleDepth() / (double)rate;
	double timeScaleFactor = 10;
	if(m_protocol == DHO)
	{	// Scale factor is not constant across all sample rates for DHO models
		std::map<uint64_t, double> *srates = (m_lowSrate ? &dhoLowSampleRates : &dhoHighSampleRates);
		auto d = srates->find(rate);
		if (d != srates->end())
		{
			timeScaleFactor = d->second;
		}
	}

	m_transport->SendCommandQueued(string(":TIM:SCAL ") + to_string(sampletime / timeScaleFactor));

	m_srateValid = false;
	m_mdepthValid = false;
}

void RigolOscilloscope::SetTriggerOffset(int64_t offset)
{
	//Rigol standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));
	m_transport->SendCommandQueued(string(":TIM:MAIN:OFFS ") + to_string((halfwidth - offset) * SECONDS_PER_FS));
}

int64_t RigolOscilloscope::GetTriggerOffset()
{
	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_triggerOffsetValid)
			return m_triggerOffset;
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(":TIM:MAIN:OFFS?"));

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Result comes back in scientific notation
	double offsetval;
	sscanf(reply.c_str(), "%lf", &offsetval);
	m_triggerOffset = static_cast<int64_t>(round(offsetval * FS_PER_SECOND));

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));
	m_triggerOffset = halfwidth - m_triggerOffset;

	m_triggerOffsetValid = true;

	return m_triggerOffset;	
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
	//Clear out any triggers of the wrong type
	if((m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL))
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new EdgeTrigger(this);
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

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
