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

#include "scopehal.h"
#include "LeCroyOscilloscope.h"
#include "base64.h"
#include <locale>
#include <immintrin.h>
#include <omp.h>

#include "DropoutTrigger.h"
#include "EdgeTrigger.h"
#include "GlitchTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyOscilloscope::LeCroyOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_hasFastSampleRate(false)
	, m_memoryDepthOption(0)
	, m_hasI2cTrigger(false)
	, m_hasSpiTrigger(false)
	, m_hasUartTrigger(false)
	, m_maxBandwidth(10000)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleRateValid(false)
	, m_sampleRate(1)
	, m_memoryDepthValid(false)
	, m_memoryDepth(1)
	, m_triggerOffsetValid(false)
	, m_triggerOffset(0)
	, m_interleaving(false)
	, m_interleavingValid(false)
	, m_meterMode(Multimeter::DC_VOLTAGE)
	, m_meterModeValid(false)
	, m_highDefinition(false)
{
	//standard initialization
	FlushConfigCache();
	IdentifyHardware();
	DetectAnalogChannels();
	SharedCtorInit();
	DetectOptions();
}

void LeCroyOscilloscope::SharedCtorInit()
{
	m_digitalChannelCount = 0;

	//Add the external trigger input
	m_extTrigChannel = new OscilloscopeChannel(
		this,
		"Ext",
		OscilloscopeChannel::CHANNEL_TYPE_TRIGGER,
		"",
		1,
		m_channels.size(),
		true);
	m_channels.push_back(m_extTrigChannel);

	//Desired format for waveform data
	//Only use increased bit depth if the scope actually puts content there!
	if(m_highDefinition)
		m_transport->SendCommand("COMM_FORMAT DEF9,WORD,BIN");
	else
		m_transport->SendCommand("COMM_FORMAT DEF9,BYTE,BIN");

	//Always use "max memory" config for setting sample depth
	m_transport->SendCommand("VBS 'app.Acquisition.Horizontal.Maximize=\"SetMaximumMemory\"'");

	//If interleaving, disable the extra channels
	if(IsInterleaving())
	{
		m_channelsEnabled[0] = false;
		m_channelsEnabled[3] = false;
	}

	//Clear the state-change register to we get rid of any history we don't care about
	PollTrigger();
}

void LeCroyOscilloscope::IdentifyHardware()
{
	//Turn off headers (complicate parsing and add fluff to the packets)
	m_transport->SendCommand("CHDR OFF");

	//Ask for the ID
	m_transport->SendCommand("*IDN?");
	string reply = m_transport->ReadReply();
	char vendor[128] = "";
	char model[128] = "";
	char serial[128] = "";
	char version[128] = "";
	if(4 != sscanf(reply.c_str(), "%127[^,],%127[^,],%127[^,],%127s", vendor, model, serial, version))
	{
		LogError("Bad IDN response %s\n", reply.c_str());
		return;
	}
	m_vendor = vendor;
	m_model = model;
	m_serial = serial;
	m_fwVersion = version;

	//Look up model info
	m_modelid = MODEL_UNKNOWN;
	m_maxBandwidth = 0;

	if(m_model.find("DDA5") == 0)
	{
		m_modelid = MODEL_DDA_5K;
		m_maxBandwidth = 5000;
	}
	else if( (m_model.find("HDO4") == 0) && (m_model.find("A") != string::npos) )
	{
		m_modelid = MODEL_HDO_4KA;
		m_maxBandwidth = stoi(m_model.substr(4, 2)) * 100;
	}
	else if( (m_model.find("HDO6") == 0) && (m_model.find("A") != string::npos) )
	{
		m_modelid = MODEL_HDO_6KA;
		m_maxBandwidth = stoi(m_model.substr(4, 2)) * 100;
	}
	else if(m_model.find("HDO9") == 0)
	{
		m_modelid = MODEL_HDO_9K;
		m_maxBandwidth = stoi(m_model.substr(4, 1)) * 1000;
	}
	else if(m_model == "MCM-ZI-A")
	{
		m_modelid = MODEL_LABMASTER_ZI_A;

		//For now assume 100 GHz bandwidth.
		//TODO: ID acquisition modules
		m_maxBandwidth = 100000;
	}
	else if(m_model.find("MDA8") == 0)
	{
		m_modelid = MODEL_MDA_800;
		m_highDefinition = true;	//Doesn't have "HD" in the name but is still 12 bit resolution
		m_maxBandwidth = stoi(m_model.substr(4, 2)) * 100;
	}
	else if(m_model.find("SDA3") == 0)
	{
		m_modelid = MODEL_SDA_3K;
		m_maxBandwidth = 3000;
	}
	else if(m_model.find("WM8") == 0)
	{
		if(m_model.find("ZI-B") != string::npos)
			m_modelid = MODEL_WAVEMASTER_8ZI_B;

		m_maxBandwidth = stoi(m_model.substr(3, 2)) * 1000;
	}
	else if(m_model.find("WAVERUNNER8") == 0)
	{
		m_modelid = MODEL_WAVERUNNER_8K;

		m_maxBandwidth = stoi(m_model.substr(11, 2)) * 100;

		if(m_model.find("HD") != string::npos)
			m_modelid = MODEL_WAVERUNNER_8K_HD;
	}
	else if(m_model.find("WP") == 0)
	{
		if(m_model.find("HD") != string::npos)
			m_modelid = MODEL_WAVEPRO_HD;
	}
	else if(m_model.find("WAVERUNNER9") == 0)
	{
		m_modelid = MODEL_WAVERUNNER_9K;
		m_maxBandwidth = stoi(m_model.substr(11, 2)) * 100;
	}
	else if(m_model.find("WS3") == 0)
	{
		m_modelid = MODEL_WAVESURFER_3K;
		m_maxBandwidth = stoi(m_model.substr(3, 2)) * 100;
	}
	else if (m_vendor.compare("SIGLENT") == 0)
	{
		// TODO: if LeCroy and Siglent classes get split, then this should obviously
		// move to the Siglent class.
		if (m_model.compare(0, 4, "SDS2") == 0 && m_model.back() == 'X')
			m_modelid = MODEL_SIGLENT_SDS2000X;

		//FIXME
		m_maxBandwidth = 200;
	}

	else
	{
		LogWarning("Model \"%s\" is unknown, available sample rates/memory depths may not be properly detected\n",
			m_model.c_str());
	}

	//Enable HD mode by default if model name contains "HD" at any point
	if(m_model.find("HD") != string::npos)
		m_highDefinition = true;

	//300 MHz bandwidth doesn't exist on any known scope.
	//It's always 350, but is normally coded in the model ID as if it were 300.
	if(m_maxBandwidth == 300)
		m_maxBandwidth = 350;
}

void LeCroyOscilloscope::DetectOptions()
{
	LogDebug("\n");

	m_transport->SendCommand("*OPT?");
	string reply = m_transport->ReadReply();
	if(reply.length() > 3)
	{
		//Read options until we hit a null
		vector<string> options;
		string opt;
		for(unsigned int i=0; i<reply.length(); i++)
		{
			if(reply[i] == 0)
			{
				options.push_back(opt);
				break;
			}

			else if(reply[i] == ',')
			{
				options.push_back(opt);
				opt = "";
			}

			//skip newlines
			else if(reply[i] == '\n')
				continue;

			else
				opt += reply[i];
		}
		if(opt != "")
			options.push_back(opt);

		//Print out the option list and do processing for each
		LogDebug("Installed options:\n");
		LogDebug("  %-20s %-25s %-35s %-20s\n", "Code", "Type", "Description", "Action");
		if(options.empty())
			LogDebug("* None\n");
		for(auto o : options)
		{
			string type = "Unknown";
			string desc = "Unknown";
			string action = "Ignoring";

			//Default types
			if(o.find("_TDME") != string::npos)
				type = "Trig/decode/measure/eye";
			else if(o.find("_TDG") != string::npos)
				type = "Trig/decode/graph";
			else if(o.find("_TD") != string::npos)
				type = "Trig/decode";
			else if( (o.find("_D") != string::npos) || (o.find("-DECODE") != string::npos) )
				type = "Protocol decode";

			//If we have an LA module installed, add the digital channels
			if( (o == "MSXX") && !m_hasLA)
			{
				type = "Hardware";
				desc = "16-channel MSO probe";
				action = "Enabled";

				AddDigitalChannels(16);
			}

			//If we have the voltmeter installed, make a note of that
			else if(o == "DVM")
			{
				m_hasDVM = true;

				type = "Hardware";
				desc = "Digital multimeter";
				action = "Enabled";

				SetMeterAutoRange(false);
			}

			//If we have the function generator installed, remember that
			else if(o == "AFG")
			{
				m_hasFunctionGen = true;
				type = "Hardware";
				desc = "Function generator";
				action = "Enabled";
			}

			//Extra sample rate and memory for WaveRunner 8000
			else if(o == "-M")
			{
				m_hasFastSampleRate = true;
				m_memoryDepthOption = 128;
				type = "Hardware";
				desc = "Extra sample rate and memory";
				action = "Enabled";
			}

			//Extra memory depth for WaveRunner 8000HD and WavePro HD
			else if(o == "100MS")
			{
				m_memoryDepthOption = 100;
				type = "Hardware";
				desc = "100M point memory";
				action = "Enabled";
			}
			else if(o == "200MS")
			{
				m_memoryDepthOption = 200;
				type = "Hardware";
				desc = "200M point memory";
				action = "Enabled";
			}
			else if(o == "500MS")
			{
				m_memoryDepthOption = 500;
				type = "Hardware";
				desc = "500M point memory";
				action = "Enabled";
			}
			else if(o == "1000MS")
			{
				m_memoryDepthOption = 1000;
				type = "Hardware";
				desc = "1000M point memory";
				action = "Enabled";
			}
			else if(o == "2000MS")
			{
				m_memoryDepthOption = 2000;
				type = "Hardware";
				desc = "2000M point memory";
				action = "Enabled";
			}
			else if(o == "5000MS")
			{
				m_memoryDepthOption = 5000;
				type = "Hardware";
				desc = "5000M point memory";
				action = "Enabled";
			}

			//Print out full names for protocol trigger options and enable trigger mode.
			//Note that many of these options don't have _TD in the base (non-TDME) option code!
			else if(o.find("I2C") == 0)
			{
				m_hasI2cTrigger = true;
				desc = "I2C";	//seems like UTF-8 characters mess up printf width specifiers
				action = "Enabling trigger";

				if(o == "I2C")
					type = "Trig/decode";
			}
			else if(o.find("SPI") == 0)
			{
				m_hasSpiTrigger = true;
				desc = "SPI";
				action = "Enabling trigger";

				if(o == "SPI")
					type = "Trig/decode";
			}
			else if(o.find("UART") == 0)
			{
				m_hasUartTrigger = true;
				desc = "UART";
				action = "Enabling trigger";

				if(o == "UART")
					type = "Trig/decode";
			}
			else if(o.find("SMBUS") == 0)
			{
				m_hasI2cTrigger = true;
				desc = "SMBus";
				//TODO: enable any SMBus specific stuff

				if(o == "SMBUS")
					type = "Trig/decode";
			}

			//Currently unsupported protocol decode with trigger capability, but no _TD in the option code
			//Print out names but ignore for now
			else if(o.find("FLX") == 0)
			{
				type = "Trig/decode";
				desc = "FlexRay";
			}
			else if(o.find("LIN") == 0)
			{
				type = "Trig/decode";
				desc = "LIN";
			}
			else if(o.find("MIL1553") == 0)
			{
				type = "Trig/decode";
				desc = "MIL-STD-1553";
			}

			//Decode only, not a trigger.
			//Has to be before USB2 to match properly.
			else if(o == "USB2-HSIC-BUS")
			{
				type = "Protocol decode";
				desc = "USB2 HSIC";
			}

			//Currently unsupported trigger/decodes, to be added in the future
			else if(o.find("CAN_FD") == 0)
				desc = "CAN FD";
			else if(o.find("FIBER_CH") == 0)
				desc = "Fibre Channel";
			else if(o.find("I2S") == 0)
				desc = "I2S";
			else if(o.find("I3C") == 0)
				desc = "I3C";
			else if(o.find("SENT") == 0)
				desc = "SENT";
			else if(o.find("SPMI") == 0)
				desc = "SPMI";
			else if(o.find("USB2") == 0)
				desc = "USB2";
			else if(o.find("USB3") == 0)
				desc = "USB3";
			else if(o.find("SATA") == 0)
				desc = "Serial ATA";
			else if(o.find("SAS") == 0)
				desc = "Serial Attached SCSI";
			else if(o == "HDTV")
			{
				type = "Trigger";		//FIXME: Is this just 1080p analog trigger support?
				desc = "HD analog TV";
			}

			//Protocol decodes without trigger capability
			//Print out name but otherwise ignore
			else if(o == "10-100M-ENET-BUS")
			{
				type = "Protocol decode";
				desc = "10/100 Ethernet";
			}
			else if(o == "10G-ENET-BUS")
			{
				type = "Protocol decode";
				desc = "10G Ethernet";
			}
			else if(o == "8B10B-BUS")
			{
				type = "Protocol decode";
				desc = "8B/10B";
			}
			else if(o == "64B66B-BUS")
			{
				type = "Protocol decode";
				desc = "64B/66B";
			}
			else if(
				(o == "ARINC429") ||
				(o == "ARINC429_DME_SYMB") )
			{
				type = "Protocol decode";
				desc = "ARINC 429";
			}
			else if(o == "AUTOENETDEBUG")
			{
				type = "Protocol decode";
				desc = "Automotive Ethernet";
			}
			else if(o == "DIGRF_3G_D")
				desc = "DigRF (3G)";
			else if(o == "DIGRF_V4_D")
				desc = "DigRF (V4)";
			else if(o == "DPHY-DECODE")
				desc = "MIPI D-PHY";
			else if(o == "ET")
			{
				type = "Protocol decode";
				desc = "Electrical Telecom";
			}
			else if(o == "MANCHESTER-BUS")
			{
				type = "Protocol decode";
				desc = "Manchester";
			}
			else if(o == "MDIO")
			{
				type = "Protocol decode";
				desc = "Ethernet MDIO";
			}
			else if(o == "MPHY-DECODE")
				desc = "MIPI M-PHY";
			else if(o == "PCIE_D")
				desc = "PCIe gen 1";
			else if(o == "SPACEWIRE")
			{
				type = "Protocol decode";
				desc = "SpaceWire";
			}
			else if(o == "NRZ-BUS")
			{
				desc = "NRZ";
				type = "Protocol decode";
			}
			else if(o == "UNIPRO-DECODE")
				desc = "UniPro";

			//Miscellaneous software option
			//Print out name but otherwise ignore
			else if(o == "CBL_DBED")
			{
				type = "Math";
				desc = "Cable De-Embedding";
			}
			else if(o == "DDM2")
			{
				type = "Math";
				desc = "Disk Drive Measurement";
			}
			else if(o == "DDR2DEBUG")
			{
				type = "Signal Integrity";
				desc = "DDR2 Debug";
			}
			else if(o == "DDR3DEBUG")
			{
				type = "Signal Integrity";
				desc = "DDR3 Debug";
			}
			else if(o == "DDR4DEBUG")
			{
				type = "Signal Integrity";
				desc = "DDR4 Debug";
			}
			else if(o == "DPHY-PHY")
			{
				type = "Signal Integrity";
				desc = "MIPI D-PHY";
			}
			else if(o == "MPHY-PHY")
			{
				type = "Signal Integrity";
				desc = "MIPI M-PHY";
			}
			else if(o == "EYEDR2")
			{
				type = "Signal Integrity";
				desc = "Eye Doctor";
			}
			else if(o == "EYEDR_EQ")
			{
				type = "Signal Integrity";
				desc = "Eye Doctor Equalization";
			}
			else if(o == "EYEDR_VP")
			{
				type = "Signal Integrity";
				desc = "Eye Doctor Virtual Probe";
			}
			else if(o == "VPROBE")
			{
				type = "Signal Integrity";
				desc = "Virtual Probe";
			}
			else if(o == "XTALK")
			{
				type = "Signal Integrity";
				desc = "Crosstalk Analysis";
			}
			else if(o == "DFP2")
			{
				type = "Math";
				desc = "DSP Filter";
			}
			else if(o == "DIGPWRMGMT")
			{
				type = "Miscellaneous";
				desc = "Power Management";
			}
			else if(o == "EMC")
			{
				type = "Miscellaneous";
				desc = "EMC Pulse Analysis";
			}
			else if( (o == "JITKIT") || (o == "JTA2") )
			{
				type = "Miscellaneous";
				desc = "Jitter/Timing Analysis";
			}
			else if(o == "PWR_ANALYSIS")
			{
				type = "Miscellaneous";
				desc = "Power Analysis";
			}
			else if( (o == "SDA2") || (o == "SDA3") || (o == "SDA3-LINQ") )
			{
				type = "Signal Integrity";
				desc = "Serial Data Analysis";
			}
			else if( (o == "THREEPHASEHARMONICS") || (o == "THREEPHASEPOWER") )
			{
				type = "Miscellaneous";
				desc = "3-Phase Power Analysis";
			}

			//UI etc options
			else if(o == "SPECTRUM")
			{
				type = "Math";
				desc = "Spectrum analyzer";
			}
			else if(o == "XWEB")
			{
				type = "UI";
				desc = "Processing web";
			}
			else if(o == "QSCAPE")
			{
				type = "UI";
				desc = "Tabbed display";
			}
			else if(o == "XDEV")
			{
				type = "SDK";
				desc = "Software development kit";
			}

			//Ignore meta-options
			else if(o == "DEMO-BUNDLE")
			{
				type = "Informational";
				desc = "Software licenses are demo/trial";
			}
			else if(o == "SIM")
			{
				type = "Informational";
				desc = "Instrument is a simulation";
			}

			LogDebug("* %-20s %-25s %-35s %-20s\n", o.c_str(), type.c_str(), desc.c_str(), action.c_str());
		}
	}

	//If we don't have a code for the LA software option, but are a -MS scope, add the LA
	if(!m_hasLA && (m_model.find("-MS") != string::npos))
		AddDigitalChannels(16);

	LogDebug("\n");
}

/**
	@brief Creates digital channels for the oscilloscope
 */
void LeCroyOscilloscope::AddDigitalChannels(unsigned int count)
{
	m_hasLA = true;
	LogIndenter li;

	m_digitalChannelCount = count;
	m_digitalChannelBase = m_channels.size();

	char chn[32];
	for(unsigned int i=0; i<count; i++)
	{
		snprintf(chn, sizeof(chn), "D%u", i);
		auto chan = new OscilloscopeChannel(
			this,
			chn,
			OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
			GetDefaultChannelColor(m_channels.size()),
			1,
			m_channels.size(),
			true);
		m_channels.push_back(chan);
		m_digitalChannels.push_back(chan);
	}

	//Set the threshold to "user defined" vs using a canned family
	m_transport->SendCommand("VBS? 'app.LogicAnalyzer.MSxxLogicFamily0 = \"USERDEFINED\" '");
	m_transport->SendCommand("VBS? 'app.LogicAnalyzer.MSxxLogicFamily1 = \"USERDEFINED\" '");

	//Select display to be "CUSTOM" so we can assign nicknames to the bits
	m_transport->SendCommand("VBS 'app.LogicAnalyzer.Digital1.Labels=\"CUSTOM\"'");
}

/**
	@brief Figures out how many analog channels we have, and add them to the device

	If you're lucky, the last digit of the model number will be the number of channels (HDO9204)

	But, since we can't have nice things, theres are plenty of exceptions. Known formats so far:
	* WAVERUNNER8104-MS has 4 channels (plus 16 digital)
	* DDA5005 / DDA5005A have 4 channels
	* SDA3010 have 4 channels
	* LabMaster just calls itself "MCM-Zi-A" and there's no information on the number of modules!
 */
void LeCroyOscilloscope::DetectAnalogChannels()
{
	int nchans = 1;

	switch(m_modelid)
	{
		//DDA5005 and similar have 4 channels despite a model number ending in 5
		//SDA3010 have 4 channels despite a model number ending in 0
		case MODEL_DDA_5K:
		case MODEL_SDA_3K:
			nchans = 4;
			break;

		//MDA800 models all have 8 channels
		case MODEL_MDA_800:
			nchans = 8;
			break;

		//LabMaster MCM could have any number of channels.
		//This is ugly and produces errors in the remote log each time we start up, but does work.
		case MODEL_LABMASTER_ZI_A:
			{
				char tmp[128];
				for(int i=1; i<80; i++)
				{
					snprintf(tmp, sizeof(tmp), "VBS? 'return=IsObject(app.Acquisition.C%d)'", i);

					m_transport->SendCommand(tmp);
					string reply = m_transport->ReadReply();

					//All good
					if(Trim(reply) == "-1")
						nchans = i;

					//Anything else is probably an error:
					//Object doesn't support this property or method: 'app.Acquisition.C5'
					else
						break;
				}
			}
			break;

		//General model format is family, number, suffix. Not all are always present.
		default:
			{
				//Trim off alphabetic characters from the start of the model number
				size_t pos;
				for(pos=0; pos < m_model.length(); pos++)
				{
					if(isalpha(m_model[pos]))
						continue;
					else if(isdigit(m_model[pos]))
						break;
					else
					{
						LogError("Unrecognized character (not alphanumeric) in model number %s\n", m_model.c_str());
						return;
					}
				}

				//Now we should be able to read the model number
				int modelNum = atoi(m_model.c_str() + pos);

				//Last digit of the model number is normally the number of channels (WAVESURFER3022, HDO8108)
				nchans = modelNum % 10;
			}
			break;
	}

	for(int i=0; i<nchans; i++)
	{
		//Hardware name of the channel
		string chname = string("C1");
		chname[1] += i;

		//Color the channels based on LeCroy's standard color sequence
		//yellow-pink-cyan-green-lightgreen-purple-red-brown
		//After that, for LabMaster, repeat the same colors
		string color = "#ffffff";
		switch(i % 8)
		{
			case 0:
				color = "#ffff00";
				break;

			case 1:
				color = "#ff6abc";
				break;

			case 2:
				color = "#00ffff";
				break;

			case 3:
				color = "#00c100";
				break;

			case 4:
				color = "#d7ffd7";
				break;

			case 5:
				color = "#8482ff";
				break;

			case 6:
				color = "#ff0000";
				break;

			case 7:
				color = "#ff8000";
				break;
		}

		//Create the channel
		m_channels.push_back(
			new OscilloscopeChannel(
			this,
			chname,
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			color,
			1,
			i,
			true));
	}
	m_analogChannelCount = nchans;
}

LeCroyOscilloscope::~LeCroyOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device information

string LeCroyOscilloscope::GetDriverNameInternal()
{
	return "lecroy";
}

OscilloscopeChannel* LeCroyOscilloscope::GetExternalTrigger()
{
	return m_extTrigChannel;
}

void LeCroyOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	if(m_trigger)
		delete m_trigger;
	m_trigger = NULL;

	m_channelVoltageRanges.clear();
	m_channelOffsets.clear();
	m_channelsEnabled.clear();
	m_channelDeskew.clear();
	m_channelDisplayNames.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;
	m_interleavingValid = false;
	m_meterModeValid = false;
}

/**
	@brief See what measurement capabilities we have
 */
unsigned int LeCroyOscilloscope::GetMeasurementTypes()
{
	unsigned int type = 0;
	if(m_hasDVM)
	{
		type |= DC_VOLTAGE;
		type |= DC_RMS_AMPLITUDE;
		type |= AC_RMS_AMPLITUDE;
		type |= FREQUENCY;
	}
	return type;
}

/**
	@brief See what features we have
 */
unsigned int LeCroyOscilloscope::GetInstrumentTypes()
{
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasDVM)
		type |= INST_DMM;
	if(m_hasFunctionGen)
		type |= INST_FUNCTION;
	return type;
}

string LeCroyOscilloscope::GetName()
{
	return m_model;
}

string LeCroyOscilloscope::GetVendor()
{
	return m_vendor;
}

string LeCroyOscilloscope::GetSerial()
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool LeCroyOscilloscope::IsChannelEnabled(size_t i)
{
	//ext trigger should never be displayed
	if(i == m_extTrigChannel->GetIndex())
		return false;

	//Disable end channels if interleaving
	if(m_interleaving)
	{
		if( (i == 0) || (i == 3) )
			return false;
	}

	//Early-out if status is in cache
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		if(m_channelsEnabled.find(i) != m_channelsEnabled.end())
			return m_channelsEnabled[i];
	}

	//Need to lock the main mutex first to prevent deadlocks
	lock_guard<recursive_mutex> lock(m_mutex);
	lock_guard<recursive_mutex> lock2(m_cacheMutex);

	//Analog
	if(i < m_analogChannelCount)
	{
		//See if the channel is enabled, hide it if not
		string cmd = m_channels[i]->GetHwname() + ":TRACE?";
		m_transport->SendCommand(cmd);
		string reply = m_transport->ReadReply();
		if(reply.find("OFF") == 0)	//may have a trailing newline, ignore that
			m_channelsEnabled[i] = false;
		else
			m_channelsEnabled[i] = true;
	}

	//Digital
	else
	{
		//See if the channel is on
		//Note that GetHwname() returns Dn, as used by triggers, not Digitaln, as used here
		size_t nchan = i - (m_analogChannelCount+1);
		m_transport->SendCommand(string("VBS? 'return = app.LogicAnalyzer.Digital1.Digital") + to_string(nchan) + "'");
		string str = m_transport->ReadReply();
		if(str == "0")
			m_channelsEnabled[i] = false;
		else
			m_channelsEnabled[i] = true;
	}

	return m_channelsEnabled[i];
}

void LeCroyOscilloscope::EnableChannel(size_t i)
{
	//LogDebug("enable channel %d\n", i);
	lock_guard<recursive_mutex> lock(m_mutex);
	//LogDebug("got mutex\n");

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
	{
		//Disable interleaving if we created a conflict
		auto chan = m_channels[i];
		if(IsInterleaving())
		{
			auto conflicts = GetInterleaveConflicts();
			for(auto c : conflicts)
			{
				if( (c.first->IsEnabled() || (c.first == chan) ) &&
					(c.second->IsEnabled() || (c.second == chan) ) )
				{
					SetInterleaving(false);
					break;
				}
			}
		}

		m_transport->SendCommand(chan->GetHwname() + ":TRACE ON");
	}

	//Trigger can't be enabled
	else if(i == m_extTrigChannel->GetIndex())
	{
	}

	//Digital channel
	else
	{
		//If we have NO digital channels enabled, enable the first digital bus
		bool anyDigitalEnabled = false;
		for(auto c : m_digitalChannels)
		{
			if(m_channelsEnabled[c->GetIndex()])
			{
				anyDigitalEnabled = true;
				break;
			}
		}

		if(!anyDigitalEnabled)
			m_transport->SendCommand("VBS 'app.LogicAnalyzer.Digital1.UseGrid=\"YT1\"'");

		//Enable this channel on the hardware
		//Note that GetHwname() returns Dn, as used by triggers, not Digitaln, as used here
		size_t nchan = i - (m_analogChannelCount+1);
		m_transport->SendCommand(string("VBS 'app.LogicAnalyzer.Digital1.Digital") + to_string(nchan) + " = 1'");
		char tmp[128];
		size_t nbit = (i - m_digitalChannels[0]->GetIndex());
		snprintf(tmp, sizeof(tmp), "VBS 'app.LogicAnalyzer.Digital1.BitIndex%zu = %zu'", nbit, nbit);
		m_transport->SendCommand(tmp);
	}

	m_channelsEnabled[i] = true;
}

bool LeCroyOscilloscope::CanEnableChannel(size_t i)
{
	//All channels are always legal if we're not interleaving
	if(!m_interleaving)
		return true;

	//We are interleaving. Disable channels we're not allowed to use.
	switch(m_modelid)
	{
		case MODEL_DDA_5K:
		case MODEL_HDO_9K:
		case MODEL_SDA_3K:
		case MODEL_HDO_4KA:
		case MODEL_WAVERUNNER_8K:
		case MODEL_WAVERUNNER_8K_HD:		//TODO: seems like multiple levels of interleaving possible
		case MODEL_WAVEMASTER_8ZI_B:
		case MODEL_WAVEPRO_HD:
		case MODEL_WAVERUNNER_9K:
		case MODEL_SIGLENT_SDS2000X:
			return (i == 1) || (i == 2) || (i > m_analogChannelCount);

		case MODEL_WAVESURFER_3K:			//TODO: can use ch1 if not 2, and ch3 if not 4
			return (i == 1) || (i == 2) || (i > m_analogChannelCount);

		//No interleaving possible, ignore
		case MODEL_HDO_6KA:
		case MODEL_LABMASTER_ZI_A:
		case MODEL_MDA_800:
		default:
			return true;
	}
}

void LeCroyOscilloscope::DisableChannel(size_t i)
{
	//LogDebug("enable channel %d\n", i);
	lock_guard<recursive_mutex> lock(m_mutex);
	//LogDebug("got mutex\n");

	m_channelsEnabled[i] = false;

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
		m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE OFF");

	//Trigger can't be enabled
	else if(i == m_extTrigChannel->GetIndex())
	{
	}

	//Digital channel
	else
	{
		//If we have NO digital channels enabled, disable the first digital bus
		bool anyDigitalEnabled = false;
		for(auto c : m_digitalChannels)
		{
			if(m_channelsEnabled[c->GetIndex()])
			{
				anyDigitalEnabled = true;
				break;
			}
		}

		if(!anyDigitalEnabled)
			m_transport->SendCommand("VBS 'app.LogicAnalyzer.Digital1.UseGrid=\"NotOnGrid\"'");

		//Disable this channel
		size_t nchan = i - (m_analogChannelCount+1);
		m_transport->SendCommand(string("VBS 'app.LogicAnalyzer.Digital1.Digital") + to_string(nchan) + " = 0'");
	}
}

OscilloscopeChannel::CouplingType LeCroyOscilloscope::GetChannelCoupling(size_t i)
{
	if(i >= m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING?");
	string reply = m_transport->ReadReply().substr(0,3);	//trim off trailing newline, all coupling codes are 3 chars

	if(reply == "A1M")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if(reply == "D1M")
		return OscilloscopeChannel::COUPLE_DC_1M;
	else if(reply == "D50")
		return OscilloscopeChannel::COUPLE_DC_50;
	else if(reply == "GND")
		return OscilloscopeChannel::COUPLE_GND;

	//invalid
	LogWarning("LeCroyOscilloscope::GetChannelCoupling got invalid coupling %s\n", reply.c_str());
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void LeCroyOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(i >= m_analogChannelCount)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);
	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING A1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING D1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING D50");
			break;

		//treat unrecognized as ground
		case OscilloscopeChannel::COUPLE_GND:
		default:
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":COUPLING GND");
			break;
	}
}

double LeCroyOscilloscope::GetChannelAttenuation(size_t i)
{
	if(i > m_analogChannelCount)
		return 1;

	//TODO: support ext/10
	if(i == m_extTrigChannel->GetIndex())
		return 1;

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":ATTENUATION?");
	string reply = m_transport->ReadReply();

	double d;
	sscanf(reply.c_str(), "%lf", &d);
	return d;
}

void LeCroyOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if(i >= m_analogChannelCount)
		return;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:ATTENUATION %f", m_channels[i]->GetHwname().c_str(), atten);

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(cmd);
}

vector<unsigned int> LeCroyOscilloscope::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;

	//"no limit"
	ret.push_back(0);

	//Supported by almost all known models
	ret.push_back(20);
	ret.push_back(200);

	switch(m_modelid)
	{
		//Only one DDA5 model is known to exist, no need for bandwidth check
		case MODEL_DDA_5K:
			ret.push_back(1000);
			ret.push_back(3000);
			ret.push_back(4000);
			break;

		case MODEL_HDO_9K:
			ret.push_back(500);
			if(m_maxBandwidth >= 2000)
				ret.push_back(1000);
			if(m_maxBandwidth >= 3000)
				ret.push_back(2000);
			if(m_maxBandwidth >= 4000)
				ret.push_back(3000);
			break;

		//TODO: this probably depends on which acquisition module is selected?
		case MODEL_LABMASTER_ZI_A:
			ret.clear();
			ret.push_back(0);
			ret.push_back(1000);
			ret.push_back(3000);
			ret.push_back(4000);
			ret.push_back(6000);
			ret.push_back(8000);
			ret.push_back(13000);
			ret.push_back(16000);
			ret.push_back(20000);
			ret.push_back(25000);
			ret.push_back(30000);
			ret.push_back(33000);
			ret.push_back(36000);
			break;

		case MODEL_MDA_800:
		case MODEL_WAVERUNNER_8K_HD:
			if(m_maxBandwidth >= 500)
				ret.push_back(350);
			if(m_maxBandwidth >= 1000)
				ret.push_back(500);
			if(m_maxBandwidth >= 2000)
				ret.push_back(1000);
			break;

		//Seems like the SDA 3010 is part of a family of different scopes with prefix indicating bandwidth.
		//We should probably change this to SDA_FIRSTGEN or something?
		case MODEL_SDA_3K:
			ret.push_back(1000);
			break;

		case MODEL_WAVEMASTER_8ZI_B:
			ret.push_back(1000);
			if(m_maxBandwidth >= 4000)
				ret.push_back(3000);
			if(m_maxBandwidth >= 6000)
				ret.push_back(4000);
			if(m_maxBandwidth >= 8000)
				ret.push_back(6000);
			if(m_maxBandwidth >= 13000)
				ret.push_back(8000);
			break;

		case MODEL_WAVEPRO_HD:
			ret.push_back(500);
			ret.push_back(1000);
			if(m_maxBandwidth >= 4000)
				ret.push_back(2500);
			if(m_maxBandwidth >= 6000)
				ret.push_back(4000);
			if(m_maxBandwidth >= 8000)
				ret.push_back(6000);
			break;

		case MODEL_WAVERUNNER_8K:
		case MODEL_WAVERUNNER_9K:
			if(m_maxBandwidth >= 2500)
				ret.push_back(1000);
			break;

		case MODEL_WAVESURFER_3K:
			ret.clear();
			if(m_maxBandwidth >= 350)
				ret.push_back(200);
			break;

		//Only the default 20/200
		case MODEL_HDO_4KA:
		case MODEL_HDO_6KA:
		case MODEL_SIGLENT_SDS2000X:
		default:
			break;
	}

	return ret;
}

int LeCroyOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i > m_analogChannelCount)
		return 0;

	lock_guard<recursive_mutex> lock(m_mutex);

	string cmd = "BANDWIDTH_LIMIT?";
	m_transport->SendCommand(cmd);
	string reply = m_transport->ReadReply();

	size_t index = reply.find(m_channels[i]->GetHwname());
	if(index == string::npos)
		return 0;

	char chbw[16];
	sscanf(reply.c_str() + index + 3, "%15[^,\n]", chbw);	//offset 3 for "Cn,"
	string sbw(chbw);

	if(sbw == "OFF")
		return 0;
	else if(sbw == "ON")		//apparently "on" means lowest possible B/W?
		return 20;				//this isn't documented anywhere in the MAUI remote control manual
	else if(sbw == "20MHZ")
		return 20;
	else if(sbw == "200MHZ")
		return 200;
	else if(sbw == "500MHZ")
		return 500;
	else if(sbw == "1GHZ")
		return 1000;
	else if(sbw == "2GHZ")
		return 2000;
	else if(sbw == "3GHZ")
		return 3000;
	else if(sbw == "4GHZ")
		return 4000;
	else if(sbw == "6GHZ")
		return 6000;

	LogWarning("LeCroyOscilloscope::GetChannelCoupling got invalid coupling %s\n", reply.c_str());
	return 0;
}

void LeCroyOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char cmd[128];
	if(limit_mhz == 0)
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,OFF", m_channels[i]->GetHwname().c_str());
	else if(limit_mhz >= 1000)
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,%uGHZ", m_channels[i]->GetHwname().c_str(), limit_mhz/1000);
	else
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,%uMHZ", m_channels[i]->GetHwname().c_str(), limit_mhz);

	m_transport->SendCommand(cmd);
}

void LeCroyOscilloscope::SetChannelDisplayName(size_t i, string name)
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

	//Update in hardware
	lock_guard<recursive_mutex> lock(m_mutex);
	if(i < m_analogChannelCount)
		m_transport->SendCommand(string("VBS 'app.Acquisition.") + chan->GetHwname() + ".Alias = \"" + name + "\"");

	else
	{
		m_transport->SendCommand(string("VBS 'app.LogicAnalyzer.Digital1.CustomBitName") +
			to_string(i - m_digitalChannelBase) + " = \"" + name + "\"");
	}
}

string LeCroyOscilloscope::GetChannelDisplayName(size_t i)
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

	lock_guard<recursive_mutex> lock(m_mutex);

	//Analog and digital channels use completely different namespaces, as usual.
	//Because clean, orthogonal APIs are apparently for losers?
	string name;
	if(i < m_analogChannelCount)
		name = GetPossiblyEmptyString(string("app.Acquisition.") + chan->GetHwname() + ".Alias");
	else
	{
		auto prop = string("app.LogicAnalyzer.Digital1.CustomBitName") + to_string(i - m_digitalChannelBase);
		name = GetPossiblyEmptyString(prop);

		//Default name, change it to the hwname for now
		if(name.find("Custom.") == 0)
		{
			m_transport->SendCommand(string("VBS '") + prop + " = \"" + chan->GetHwname() + "\"'");
			name = "";
		}
	}

	//Default to using hwname if no alias defined
	if(name == "")
		name = chan->GetHwname();

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDisplayNames[chan] = name;

	return name;
}

/**
	@brief Get an
 */
string LeCroyOscilloscope::GetPossiblyEmptyString(const string& property)
{
	//Get string length first since reading empty strings is problematic over SCPI
	m_transport->SendCommand(string("VBS? 'return = Len(") + property + ")'");
	string slen = Trim(m_transport->ReadReply());
	if(slen == "0")
		return "";

	m_transport->SendCommand(string("VBS? 'return = ") + property + "'");
	return Trim(m_transport->ReadReply());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

int LeCroyOscilloscope::GetMeterDigits()
{
	return 5;
}

bool LeCroyOscilloscope::GetMeterAutoRange()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.AutoRange'");
	string str = m_transport->ReadReply();
	int ret;
	sscanf(str.c_str(), "%d", &ret);
	return ret ? true : false;
}

void LeCroyOscilloscope::SetMeterAutoRange(bool enable)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if(enable)
		m_transport->SendCommand("VBS 'app.acquisition.DVM.AutoRange = 1'");
	else
		m_transport->SendCommand("VBS 'app.acquisition.DVM.AutoRange = 0'");
}

void LeCroyOscilloscope::StartMeter()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 1'");
}

void LeCroyOscilloscope::StopMeter()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS 'app.acquisition.DVM.DvmEnable = 0'");
}

double LeCroyOscilloscope::GetMeterValue()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	switch(GetMeterMode())
	{
		case Multimeter::DC_VOLTAGE:
			m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.Voltage'");
			break;

		case Multimeter::DC_RMS_AMPLITUDE:
		case Multimeter::AC_RMS_AMPLITUDE:
			m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.Amplitude'");
			break;

		case Multimeter::FREQUENCY:
			m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.Frequency'");
			break;

		default:
			return 0;
	}

	return stod(m_transport->ReadReply());
}

int LeCroyOscilloscope::GetMeterChannelCount()
{
	return m_analogChannelCount;
}

string LeCroyOscilloscope::GetMeterChannelName(int chan)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	return m_channels[chan]->GetDisplayName();
}

int LeCroyOscilloscope::GetCurrentMeterChannel()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.DvmSource'");
	string str = m_transport->ReadReply();
	int i;
	sscanf(str.c_str(), "C%d", &i);
	return i - 1;	//scope channels are 1 based
}

void LeCroyOscilloscope::SetCurrentMeterChannel(int chan)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char cmd[128];
	snprintf(
		cmd,
		sizeof(cmd),
		"VBS 'app.acquisition.DVM.DvmSource = \"C%d\"",
		chan + 1);	//scope channels are 1 based
	m_transport->SendCommand(cmd);
}

Multimeter::MeasurementTypes LeCroyOscilloscope::GetMeterMode()
{
	if(m_meterModeValid)
		return m_meterMode;

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("VBS? 'return = app.acquisition.DVM.DvmMode'");
	string str = m_transport->ReadReply();

	//trim off trailing whitespace
	while(isspace(str[str.length()-1]))
		str.resize(str.length() - 1);

	if(str == "DC")
		m_meterMode = Multimeter::DC_VOLTAGE;
	else if(str == "DC RMS")
		m_meterMode = Multimeter::DC_RMS_AMPLITUDE;
	else if(str == "ACRMS")
		m_meterMode = Multimeter::AC_RMS_AMPLITUDE;
	else if(str == "Frequency")
		m_meterMode = Multimeter::FREQUENCY;
	else
	{
		LogError("Invalid meter mode \"%s\"\n", str.c_str());
		m_meterMode = Multimeter::DC_VOLTAGE;
	}

	m_meterModeValid = true;
	return m_meterMode;
}

void LeCroyOscilloscope::SetMeterMode(Multimeter::MeasurementTypes type)
{
	m_meterMode = type;
	m_meterModeValid = true;

	string stype;
	switch(type)
	{
		case Multimeter::DC_VOLTAGE:
			stype = "DC";
			break;

		case Multimeter::DC_RMS_AMPLITUDE:
			stype = "DC RMS";
			break;

		case Multimeter::AC_RMS_AMPLITUDE:
			stype = "ACRMS";
			break;

		case Multimeter::FREQUENCY:
			stype = "Frequency";
			break;

		//not implemented, disable
		case Multimeter::AC_CURRENT:
		case Multimeter::DC_CURRENT:
		case Multimeter::TEMPERATURE:
			LogWarning("unsupported multimeter mode\n");
			return;

	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(string("VBS 'app.acquisition.DVM.DvmMode = \"") + stype + "\"");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator mode

int LeCroyOscilloscope::GetFunctionChannelCount()
{
	if(m_hasFunctionGen)
		return 1;
	else
		return 0;
}

string LeCroyOscilloscope::GetFunctionChannelName(int /*chan*/)
{
	return "FUNC";
}

bool LeCroyOscilloscope::GetFunctionChannelActive(int /*chan*/)
{
	LogWarning("LeCroyOscilloscope::GetFunctionChannelActive unimplemented\n");
	return false;
}

void LeCroyOscilloscope::SetFunctionChannelActive(int /*chan*/, bool on)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	if(on)
		m_transport->SendCommand("VBS 'app.wavesource.enable=True'");
	else
		m_transport->SendCommand("VBS 'app.wavesource.enable=False'");
}

float LeCroyOscilloscope::GetFunctionChannelDutyCycle(int /*chan*/)
{
	//app.wavesource.dutycycle
	LogWarning("LeCroyOscilloscope::GetFunctionChannelDutyCycle unimplemented\n");
	return false;
}

void LeCroyOscilloscope::SetFunctionChannelDutyCycle(int /*chan*/, float /*duty*/)
{
	//app.wavesource.dutycycle
}

float LeCroyOscilloscope::GetFunctionChannelAmplitude(int /*chan*/)
{
	//app.wavesource.amplitude
	LogWarning("LeCroyOscilloscope::GetFunctionChannelAmplitude unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelAmplitude(int /*chan*/, float /*amplitude*/)
{
	//app.wavesource.amplitude
}

float LeCroyOscilloscope::GetFunctionChannelOffset(int /*chan*/)
{
	//app.wavesource.offset
	LogWarning("LeCroyOscilloscope::GetFunctionChannelOffset unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelOffset(int /*chan*/, float /*offset*/)
{
	//app.wavesource.offset
}

float LeCroyOscilloscope::GetFunctionChannelFrequency(int /*chan*/)
{
	//app.wavesource.frequency
	LogWarning("LeCroyOscilloscope::GetFunctionChannelFrequency unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelFrequency(int /*chan*/, float hz)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.frequency = %f'", hz);
	m_transport->SendCommand(tmp);
}

FunctionGenerator::WaveShape LeCroyOscilloscope::GetFunctionChannelShape(int /*chan*/)
{
	//app.wavesource.shape

	LogWarning("LeCroyOscilloscope::GetFunctionChannelShape unimplemented\n");
	return FunctionGenerator::SHAPE_SINE;
}

void LeCroyOscilloscope::SetFunctionChannelShape(int /*chan*/, WaveShape /*shape*/)
{
	//app.wavesource.shape
}

float LeCroyOscilloscope::GetFunctionChannelRiseTime(int /*chan*/)
{
	//app.wavesource.risetime
	LogWarning("LeCroyOscilloscope::GetFunctionChannelRiseTime unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelRiseTime(int /*chan*/, float sec)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.risetime = %f'", sec);
	m_transport->SendCommand(tmp);
}

float LeCroyOscilloscope::GetFunctionChannelFallTime(int /*chan*/)
{
	//app.wavesource.falltime
	LogWarning("LeCroyOscilloscope::GetFunctionChannelFallTime unimplemented\n");
	return 0;
}

void LeCroyOscilloscope::SetFunctionChannelFallTime(int /*chan*/, float sec)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS 'app.wavesource.falltime = %f'", sec);
	m_transport->SendCommand(tmp);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

bool LeCroyOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

Oscilloscope::TriggerMode LeCroyOscilloscope::PollTrigger()
{
	//LogDebug("Polling trigger\n");

	//Read the Internal State Change Register
	string sinr;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("INR?");
		sinr = m_transport->ReadReply();
	}
	//LogDebug("Got trigger state\n");
	int inr = atoi(sinr.c_str());

	//See if we got a waveform
	if(inr & 0x0001)
	{
		m_triggerArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	//No waveform, but ready for one?
	if(inr & 0x2000)
	{
		m_triggerArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//Stopped, no data available
	//TODO: how to handle auto / normal trigger mode?
	return TRIGGER_MODE_RUN;
}

bool LeCroyOscilloscope::ReadWaveformBlock(string& data)
{
	//Prefix "DESC,\n" or "DAT1,\n". Always seems to be 6 chars and start with a D.
	//Next is the length header. Looks like #9000000346. #9 followed by nine ASCII length digits.
	//Ignore that too.
	string tmp = m_transport->ReadReply();
	size_t offset = tmp.find("D");

	//Copy the rest of the block
	data = tmp.substr(offset + 16);

	return true;
}

/**
	@brief Optimized function for checking channel enable status en masse with less round trips to the scope
 */
void LeCroyOscilloscope::BulkCheckChannelEnableState()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Check enable state in the cache.
	vector<int> uncached;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(m_channelsEnabled.find(i) == m_channelsEnabled.end())
			uncached.push_back(i);
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	//Batched implementation
	if(m_transport->IsCommandBatchingSupported())
	{
		for(auto i : uncached)
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE?");
		for(auto i : uncached)
		{
			string reply = m_transport->ReadReply();
			if(reply == "OFF")
				m_channelsEnabled[i] = false;
			else
				m_channelsEnabled[i] = true;
		}
	}

	//Unoptimized fallback for use with transports that can't handle batching
	else
	{
		for(auto i : uncached)
		{
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":TRACE?");

			string reply = m_transport->ReadReply();
			if(reply == "OFF")
				m_channelsEnabled[i] = false;
			else
				m_channelsEnabled[i] = true;
		}
	}

	/*
	//Check digital status
	//TODO: better per-lane queries
	m_transport->SendCommand("Digital1:TRACE?");

	string reply = m_transport->ReadReply();
	if(reply == "OFF")
	{
		for(size_t i=0; i<m_digitalChannelCount; i++)
			m_channelsEnabled[m_digitalChannels[i]->GetIndex()] = false;
	}
	else
	{
		for(size_t i=0; i<m_digitalChannelCount; i++)
			m_channelsEnabled[m_digitalChannels[i]->GetIndex()] = true;
	}*/
}

bool LeCroyOscilloscope::ReadWavedescs(
	vector<string>& wavedescs,
	bool* enabled,
	unsigned int& firstEnabledChannel,
	bool& any_enabled)
{
	//(Note: with VICP framing we cannot use semicolons to separate commands)
	BulkCheckChannelEnableState();
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		enabled[i] = IsChannelEnabled(i);
		if(enabled[i])
			any_enabled = true;
	}
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		wavedescs.push_back("");

		//If NO channels are enabled, query channel 1's WAVEDESC.
		//Per phone conversation w/ Honam @ LeCroy apps, this will be updated even if channel is turned off
		if(enabled[i] || (!any_enabled && i==0))
		{
			if(firstEnabledChannel == UINT_MAX)
				firstEnabledChannel = i;
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? DESC");
		}
	}
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i] || (!any_enabled && i==0))
		{
			if(!ReadWaveformBlock(wavedescs[i]))
				LogError("ReadWaveformBlock for wavedesc %u failed\n", i);
		}
	}

	//Check length, complain if a wavedesc comes back too short
	size_t expected_wavedesc_size = 346;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(!enabled[i] && !(!any_enabled && i==0))
			continue;

		if(wavedescs[i].size() < expected_wavedesc_size)
		{
			LogError("Got wavedesc of %zu bytes (expected %zu)\n", wavedescs[i].size(), expected_wavedesc_size);
			return false;
		}
	}
	return true;
}

void LeCroyOscilloscope::RequestWaveforms(bool* enabled, uint32_t num_sequences, bool denabled)
{
	//Ask for all analog waveforms
	bool sent_wavetime = false;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			//If a multi-segment capture, ask for the trigger time data
			if( (num_sequences > 1) && !sent_wavetime)
			{
				m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? TIME");
				sent_wavetime = true;
			}

			//Ask for the data
			m_transport->SendCommand(m_channels[i]->GetHwname() + ":WF? DAT1");
		}
	}

	//Ask for the digital waveforms
	if(denabled)
		m_transport->SendCommand("Digital1:WF?");
}

time_t LeCroyOscilloscope::ExtractTimestamp(unsigned char* wavedesc, double& basetime)
{
	/*
		Timestamp is a somewhat complex format that needs some shuffling around.
		Timestamp starts at offset 296 bytes in the wavedesc
		(296-303)	double seconds
		(304)		byte minutes
		(305)		byte hours
		(306)		byte days
		(307)		byte months
		(308-309)	uint16 year

		TODO: during startup, query instrument for its current time zone
		since the wavedesc reports instment local time
	 */
	//Yes, this cast is intentional.
	//It assumes you're on a little endian system using IEEE754 64-bit float, but that applies to everything we support.
	//cppcheck-suppress invalidPointerCast
	double fseconds = *reinterpret_cast<const double*>(wavedesc + 296);
	uint8_t seconds = floor(fseconds);
	basetime = fseconds - seconds;
	time_t tnow = time(NULL);
	struct tm tstruc;

#ifdef _WIN32
	localtime_s(&tstruc, &tnow);
#else
	localtime_r(&tnow, &tstruc);
#endif

	//Convert the instrument time to a string, then back to a tm
	//Is there a better way to do this???
	//Naively poking "struct tm" fields gives incorrect results (scopehal-apps:#52)
	//Maybe because tm_yday is inconsistent?
	char tblock[64] = {0};
	snprintf(tblock, sizeof(tblock), "%d-%d-%d %d:%02d:%02d",
		*reinterpret_cast<uint16_t*>(wavedesc + 308),
		wavedesc[307],
		wavedesc[306],
		wavedesc[305],
		wavedesc[304],
		seconds);
	locale cur_locale;
	auto& tget = use_facet< time_get<char> >(cur_locale);
	istringstream stream(tblock);
	ios::iostate state;
	char format[] = "%F %T";
	tget.get(stream, time_get<char>::iter_type(), stream, state, &tstruc, format, format+strlen(format));
	return mktime(&tstruc);
}

vector<WaveformBase*> LeCroyOscilloscope::ProcessAnalogWaveform(
	const char* data,
	size_t datalen,
	string& wavedesc,
	uint32_t num_sequences,
	time_t ttime,
	double basetime,
	double* wavetime)
{
	vector<WaveformBase*> ret;

	//Parse the wavedesc headers
	auto pdesc = (unsigned char*)(&wavedesc[0]);
	//uint32_t wavedesc_len = *reinterpret_cast<uint32_t*>(pdesc + 36);
	//uint32_t usertext_len = *reinterpret_cast<uint32_t*>(pdesc + 40);

	//cppcheck-suppress invalidPointerCast
	float v_gain = *reinterpret_cast<float*>(pdesc + 156);

	//cppcheck-suppress invalidPointerCast
	float v_off = *reinterpret_cast<float*>(pdesc + 160);

	//cppcheck-suppress invalidPointerCast
	float interval = *reinterpret_cast<float*>(pdesc + 176) * 1e12f;

	//cppcheck-suppress invalidPointerCast
	double h_off = *reinterpret_cast<double*>(pdesc + 180) * 1e12f;	//ps from start of waveform to trigger

	double h_off_frac = fmodf(h_off, interval);						//fractional sample position, in ps
	if(h_off_frac < 0)
		h_off_frac = interval + h_off_frac;		//double h_unit = *reinterpret_cast<double*>(pdesc + 244);

	//Raw waveform data
	size_t num_samples;
	if(m_highDefinition)
		num_samples = datalen/2;
	else
		num_samples = datalen;
	size_t num_per_segment = num_samples / num_sequences;
	int16_t* wdata = (int16_t*)&data[0];
	int8_t* bdata = (int8_t*)&data[0];

	//Update cache with settings from this trigger
	m_memoryDepth = num_per_segment;
	m_memoryDepthValid = true;

	for(size_t j=0; j<num_sequences; j++)
	{
		//Set up the capture we're going to store our data into
		AnalogWaveform* cap = new AnalogWaveform;
		cap->m_timescale = round(interval);

		cap->m_triggerPhase = h_off_frac;
		cap->m_startTimestamp = ttime;

		//Parse the time
		if(num_sequences > 1)
			cap->m_startPicoseconds = static_cast<int64_t>( (basetime + wavetime[j*2]) * 1e12f );
		else
			cap->m_startPicoseconds = static_cast<int64_t>(basetime * 1e12f);

		cap->Resize(num_per_segment);

		//Convert raw ADC samples to volts
		//TODO: Optimized AVX conversion for 16-bit samples
		float* samps = reinterpret_cast<float*>(&cap->m_samples[0]);
		if(m_highDefinition)
		{
			int16_t* base = wdata + j*num_per_segment;

			for(unsigned int k=0; k<num_per_segment; k++)
			{
				cap->m_offsets[k] = k;
				cap->m_durations[k] = 1;
				samps[k] = base[k] * v_gain - v_off;
			}
		}
		else
		{
			if(g_hasAvx2)
			{
				//Divide large waveforms (>1M points) into blocks and multithread them
				//TODO: tune split
				if(num_per_segment > 1000000)
				{
					//Round blocks to multiples of 32 samples for clean vectorization
					size_t numblocks = omp_get_max_threads();
					size_t lastblock = numblocks - 1;
					size_t blocksize = num_per_segment / numblocks;
					blocksize = blocksize - (blocksize % 32);

					#pragma omp parallel for
					for(size_t i=0; i<numblocks; i++)
					{
						//Last block gets any extra that didn't divide evenly
						size_t nsamp = blocksize;
						if(i == lastblock)
							nsamp = num_per_segment - i*blocksize;

						Convert8BitSamplesAVX2(
							(int64_t*)&cap->m_offsets[i*blocksize],
							(int64_t*)&cap->m_durations[i*blocksize],
							samps + i*blocksize,
							bdata + j*num_per_segment + i*blocksize,
							v_gain,
							v_off,
							nsamp,
							i*blocksize);
					}
				}

				//Small waveforms get done single threaded to avoid overhead
				else
				{
					Convert8BitSamplesAVX2(
						(int64_t*)&cap->m_offsets[0],
						(int64_t*)&cap->m_durations[0],
						samps,
						bdata + j*num_per_segment,
						v_gain,
						v_off,
						num_per_segment,
						0);
				}
			}
			else
			{
				Convert8BitSamples(
					(int64_t*)&cap->m_offsets[0],
					(int64_t*)&cap->m_durations[0],
					samps,
					bdata + j*num_per_segment,
					v_gain,
					v_off,
					num_per_segment,
					0);
			}
		}

		ret.push_back(cap);
	}

	return ret;
}

/**
	@brief Converts 8-bit ADC samples to floating point
 */
void LeCroyOscilloscope::Convert8BitSamples(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase)
{
	for(unsigned int k=0; k<count; k++)
	{
		offs[k] = ibase + k;
		durs[k] = 1;
		pout[k] = pin[k] * gain - offset;
	}
}

/**
	@brief Optimized version of Convert8BitSamples()
 */
__attribute__((target("avx2")))
void LeCroyOscilloscope::Convert8BitSamplesAVX2(
	int64_t* offs, int64_t* durs, float* pout, int8_t* pin, float gain, float offset, size_t count, int64_t ibase)
{
	unsigned int end = count - (count % 32);

	int64_t __attribute__ ((aligned(32))) ones_x4[] = {1, 1, 1, 1};
	int64_t __attribute__ ((aligned(32))) fours_x4[] = {4, 4, 4, 4};
	int64_t __attribute__ ((aligned(32))) count_x4[] =
	{
		ibase + 0,
		ibase + 1,
		ibase + 2,
		ibase + 3
	};

	__m256i all_ones = _mm256_load_si256(reinterpret_cast<__m256i*>(ones_x4));
	__m256i all_fours = _mm256_load_si256(reinterpret_cast<__m256i*>(fours_x4));
	__m256i counts = _mm256_load_si256(reinterpret_cast<__m256i*>(count_x4));

	__m256 gains = { gain, gain, gain, gain, gain, gain, gain, gain };
	__m256 offsets = { offset, offset, offset, offset, offset, offset, offset, offset };

	for(unsigned int k=0; k<end; k += 32)
	{
		//This is likely a lot faster, but assumes we have 64 byte alignment on pin which is not guaranteed.
		//TODO: fix alignment
		//__m256i raw_samples = _mm256_load_si256(reinterpret_cast<__m256i*>(pin + k));

		//Load all 32 raw ADC samples, without assuming alignment
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
	{
		offs[k] = ibase + k;
		durs[k] = 1;
		pout[k] = pin[k] * gain - offset;
	}
}

map<int, DigitalWaveform*> LeCroyOscilloscope::ProcessDigitalWaveform(string& data)
{
	map<int, DigitalWaveform*> ret;

	//See what channels are enabled
	string tmp = data.substr(data.find("SelectedLines=") + 14);
	tmp = tmp.substr(0, 16);
	bool enabledChannels[16];
	for(int i=0; i<16; i++)
		enabledChannels[i] = (tmp[i] == '1');

	//Quick and dirty string searching. We only care about a small fraction of the XML
	//so no sense bringing in a full parser.
	tmp = data.substr(data.find("<HorPerStep>") + 12);
	tmp = tmp.substr(0, tmp.find("</HorPerStep>"));
	float interval = atof(tmp.c_str()) * 1e12f;
	//LogDebug("Sample interval: %.2f ps\n", interval);

	tmp = data.substr(data.find("<NumSamples>") + 12);
	tmp = tmp.substr(0, tmp.find("</NumSamples>"));
	size_t num_samples = atoi(tmp.c_str());
	//LogDebug("Expecting %d samples\n", num_samples);

	//Extract the raw trigger timestamp (nanoseconds since Jan 1 2000)
	tmp = data.substr(data.find("<FirstEventTime>") + 16);
	tmp = tmp.substr(0, tmp.find("</FirstEventTime>"));
	int64_t timestamp;
	if(1 != sscanf(tmp.c_str(), "%ld", &timestamp))
		return ret;

	//Get the client's local time.
	//All we need from this is to know whether DST is active
	tm now;
	time_t tnow;
	time(&tnow);
	localtime_r(&tnow, &now);

	//Convert Jan 1 2000 in the client's local time zone (assuming this is the same as instrument time) to Unix time.
	//Note that the instrument time zone conversion seems to be broken and not handle DST offsets right.
	//Move the epoch by an hour if we're currently in DST to compensate.
	tm epoch;
	epoch.tm_sec = 0;
	epoch.tm_min = 0;
	epoch.tm_hour = 0;
	epoch.tm_mday = 1;
	epoch.tm_mon = 0;
	epoch.tm_year = 100;
	epoch.tm_wday = 6;				//Jan 1 2000 was a Saturday
	epoch.tm_yday = 0;
	epoch.tm_isdst = now.tm_isdst;
	time_t epoch_stamp = mktime(&epoch);

	//Pull out nanoseconds from the timestamp and convert to picoseconds since that's the scopehal fine time unit
	const int64_t ns_per_sec = 1000000000;
	int64_t start_ns = timestamp % ns_per_sec;
	int64_t start_ps = 1000 * start_ns;
	int64_t start_sec = (timestamp - start_ns) / ns_per_sec;
	time_t start_time = epoch_stamp + start_sec;

	//Pull out the actual binary data (Base64 coded)
	tmp = data.substr(data.find("<BinaryData>") + 12);
	tmp = tmp.substr(0, tmp.find("</BinaryData>"));

	//Decode the base64
	base64_decodestate bstate;
	base64_init_decodestate(&bstate);
	unsigned char* block = new unsigned char[tmp.length()];	//base64 is smaller than plaintext, leave room
	base64_decode_block(tmp.c_str(), tmp.length(), (char*)block, &bstate);

	//We have each channel's data from start to finish before the next (no interleaving).
	//TODO: Multithread across waveforms
	unsigned int icapchan = 0;
	for(unsigned int i=0; i<m_digitalChannelCount; i++)
	{
		if(enabledChannels[i])
		{
			DigitalWaveform* cap = new DigitalWaveform;
			cap->m_timescale = interval;

			//Capture timestamp
			cap->m_startTimestamp = start_time;
			cap->m_startPicoseconds = start_ps;

			//Preallocate memory assuming no deduplication possible
			cap->Resize(num_samples);

			//Save the first sample (can't merge with sample -1 because that doesn't exist)
			size_t base = icapchan*num_samples;
			size_t k = 0;
			cap->m_offsets[0] = 0;
			cap->m_durations[0] = 1;
			cap->m_samples[0] = block[base];

			//Read and de-duplicate the other samples
			//TODO: can we vectorize this somehow?
			bool last = block[base];
			for(size_t j=1; j<num_samples; j++)
			{
				bool sample = block[base + j];

				//Deduplicate consecutive samples with same value
				//FIXME: temporary workaround for rendering bugs
				//if(last == sample)
				if( (last == sample) && ((j+3) < num_samples) )
					cap->m_durations[k] ++;

				//Nope, it toggled - store the new value
				else
				{
					k++;
					cap->m_offsets[k] = j;
					cap->m_durations[k] = 1;
					cap->m_samples[k] = sample;
					last = sample;
				}

			}

			//Done, shrink any unused space
			cap->Resize(k);
			cap->m_offsets.shrink_to_fit();
			cap->m_durations.shrink_to_fit();
			cap->m_samples.shrink_to_fit();

			//See how much space we saved
			/*
			LogDebug("%s: %zu samples deduplicated to %zu (%.1f %%)\n",
				m_digitalChannels[i]->GetDisplayName().c_str(),
				num_samples,
				k,
				(k * 100.0f) / num_samples);
			*/

			//Done, save data and go on to next
			ret[m_digitalChannels[i]->GetIndex()] = cap;
			icapchan ++;
		}

		//No data here for us!
		else
			ret[m_digitalChannels[i]->GetIndex()] = NULL;
	}
	delete[] block;
	return ret;
}

bool LeCroyOscilloscope::AcquireData()
{
	//State for this acquisition (may be more than one waveform)
	uint32_t num_sequences = 1;
	map<int, vector<WaveformBase*> > pending_waveforms;
	double start = GetTime();
	time_t ttime = 0;
	double basetime = 0;
	bool denabled = false;
	map<int, string> analogWaveformData;
	string wavetime;
	bool enabled[8] = {false};
	vector<string> wavedescs;
	double* pwtime = NULL;
	string digitalWaveformData;

	//Acquire the data (but don't parse it)
	{
		lock_guard<recursive_mutex> lock(m_mutex);

		//Get the wavedescs for all channels
		unsigned int firstEnabledChannel = UINT_MAX;
		bool any_enabled = true;
		if(!ReadWavedescs(wavedescs, enabled, firstEnabledChannel, any_enabled))
			return false;

		//Grab the WAVEDESC from the first enabled channel
		unsigned char* pdesc = NULL;
		for(unsigned int i=0; i<m_analogChannelCount; i++)
		{
			if(enabled[i] || (!any_enabled && i==0))
			{
				pdesc = (unsigned char*)(&wavedescs[i][0]);
				break;
			}
		}

		//See if any digital channels are enabled
		if(m_digitalChannelCount > 0)
		{
			m_cacheMutex.lock();
			for(size_t i=0; i<m_digitalChannels.size(); i++)
			{
				if(m_channelsEnabled[m_digitalChannels[i]->GetIndex()])
				{
					denabled = true;
					break;
				}
			}
			m_cacheMutex.unlock();
		}

		//Pull sequence count out of the WAVEDESC if we have analog channels active
		if(pdesc)
		{
			uint32_t trigtime_len = *reinterpret_cast<uint32_t*>(pdesc + 48);
			if(trigtime_len > 0)
				num_sequences = trigtime_len / 16;
		}

		//No WAVEDESCs, look at digital channels
		else
		{
			//TODO: support sequence capture of digital channels if the instrument supports this
			//(need to look into it)
			if(denabled)
				num_sequences = 1;

			//no enabled channels. abort
			else
				return false;
		}

		//Ask for every enabled channel up front, so the scope can send us the next while we parse the first
		RequestWaveforms(enabled, num_sequences, denabled);

		if(pdesc)
		{
			//Figure out when the first trigger happened.
			//Read the timestamps if we're doing segmented capture
			ttime = ExtractTimestamp(pdesc, basetime);
			if(num_sequences > 1)
				wavetime = m_transport->ReadReply();
			pwtime = reinterpret_cast<double*>(&wavetime[16]);	//skip 16-byte SCPI header

			//Read the data from each analog waveform
			for(unsigned int i=0; i<m_analogChannelCount; i++)
			{
				if(enabled[i])
					analogWaveformData[i] = m_transport->ReadReply();
			}
		}

		//Read the data from the digital waveforms, if enabled
		if(denabled)
		{
			if(!ReadWaveformBlock(digitalWaveformData))
			{
				LogDebug("failed to download digital waveform\n");
				return false;
			}
		}
	}

	//At this point all data has been read so the scope is free to go do its thing while we crunch the results.
	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("TRIG_MODE SINGLE");
		m_triggerArmed = true;
	}

	//Process analog waveforms
	vector< vector<WaveformBase*> > waveforms;
	waveforms.resize(m_analogChannelCount);
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			waveforms[i] = ProcessAnalogWaveform(
				&analogWaveformData[i][16],			//skip 16-byte SCPI header DATA,\n#9xxxxxxxx
				analogWaveformData[i].size() - 16,
				wavedescs[i],
				num_sequences,
				ttime,
				basetime,
				pwtime);
		}
	}

	//Save analog waveform data
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(!enabled[i])
			continue;

		//Done, update the data
		for(size_t j=0; j<num_sequences; j++)
			pending_waveforms[i].push_back(waveforms[i][j]);
	}

	//TODO: proper support for sequenced capture when digital channels are active
	//(seems like this doesn't work right on at least wavesurfer 3000 series)
	if(denabled)
	{
		//This is a weird XML-y format but I can't find any other way to get it :(
		map<int, DigitalWaveform*> digwaves = ProcessDigitalWaveform(digitalWaveformData);

		//Done, update the data
		for(auto it : digwaves)
			pending_waveforms[it.first].push_back(it.second);
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	for(size_t i=0; i<num_sequences; i++)
	{
		SequenceSet s;
		for(size_t j=0; j<m_channels.size(); j++)
		{
			if(pending_waveforms.find(j) != pending_waveforms.end())
				s[m_channels[j]] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	double dt = GetTime() - start;
	LogTrace("Waveform download and processing took %.3f ms\n", dt * 1000);

	return true;
}

void LeCroyOscilloscope::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//m_transport->SendCommand("TRIG_MODE NORM");
	m_transport->SendCommand("TRIG_MODE SINGLE");	//always do single captures, just re-trigger
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void LeCroyOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	//LogDebug("Start single trigger\n");
	m_transport->SendCommand("TRIG_MODE SINGLE");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void LeCroyOscilloscope::Stop()
{
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("TRIG_MODE STOP");
	}

	m_triggerArmed = false;
	m_triggerOneShot = true;

	//Clear out any pending data (the user doesn't want it, and we don't want stale stuff hanging around)
	ClearPendingWaveforms();
}

double LeCroyOscilloscope::GetChannelOffset(size_t i)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":OFFSET?");

	string reply = m_transport->ReadReply();
	double offset;
	sscanf(reply.c_str(), "%lf", &offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void LeCroyOscilloscope::SetChannelOffset(size_t i, double offset)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return;

	{
		lock_guard<recursive_mutex> lock2(m_mutex);
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "%s:OFFSET %f", m_channels[i]->GetHwname().c_str(), offset);
		m_transport->SendCommand(tmp);
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

double LeCroyOscilloscope::GetChannelVoltageRange(size_t i)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	lock_guard<recursive_mutex> lock2(m_mutex);

	m_transport->SendCommand(m_channels[i]->GetHwname() + ":VOLT_DIV?");

	string reply = m_transport->ReadReply();
	double volts_per_div;
	sscanf(reply.c_str(), "%lf", &volts_per_div);

	double v = volts_per_div * 8;	//plot is 8 divisions high on all MAUI scopes
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = v;
	return v;
}

void LeCroyOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	double vdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV %.4f", m_channels[i]->GetHwname().c_str(), vdiv);
	m_transport->SendCommand(cmd);
}

vector<uint64_t> LeCroyOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	//Not all scopes can go this slow
	//TODO: complete list
	if(m_modelid == MODEL_WAVERUNNER_8K)
		ret.push_back(1000);

	const int64_t k = 1000;
	const int64_t m = k*k;
	const int64_t g = k*m;

	//These rates are supported by all known scopes
	ret.push_back(2 * k);
	ret.push_back(5 * k);
	ret.push_back(10 * k);
	ret.push_back(20 * k);
	ret.push_back(50 * k);
	ret.push_back(100 * k);
	ret.push_back(200 * k);
	ret.push_back(500 * k);

	ret.push_back(1 * m);
	if(m_modelid == MODEL_HDO_9K)		//... with one exception
		ret.push_back(2500 * k);
	else
		ret.push_back(2 * m);
	ret.push_back(5 * m);
	ret.push_back(10 * m);
	ret.push_back(20 * m);
	ret.push_back(50 * m);
	ret.push_back(100 * m);

	//Some scopes can go faster
	switch(m_modelid)
	{
		case MODEL_DDA_5K:
			ret.push_back(200 * m);
			ret.push_back(500 * m);
			ret.push_back(1 * g);
			ret.push_back(2 * g);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			break;

		case MODEL_HDO_4KA:
			ret.push_back(250 * m);
			ret.push_back(500 * m);
			//no 1 Gsps mode, we go straight from 2.5 Gsps to 500 Msps
			ret.push_back(2500 * m);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			break;

		case MODEL_HDO_6KA:
			ret.push_back(250 * m);
			ret.push_back(500 * m);
			ret.push_back(1250 * m);
			ret.push_back(2500 * m);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			break;

		case MODEL_HDO_9K:
			ret.push_back(200 * m);
			ret.push_back(500 * m);
			ret.push_back(1 * g);
			ret.push_back(2 * g);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			ret.push_back(20 * g);
			break;

		case MODEL_LABMASTER_ZI_A:
			ret.push_back(200 * m);
			ret.push_back(500 * m);
			ret.push_back(1 * g);
			ret.push_back(2 * g);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			ret.push_back(20 * g);		//FIXME: 20 and 40 Gsps give garbage data in the MAUI Studio simulator.
			ret.push_back(40 * g);		//Data looks wrong in MAUI as well as glscopeclient so doesn't seem to be something
										//that we did. Looks like bits and pieces of waveform with gaps or overlap.
										//Unclear if sim bug or actual issue, no testing on actual LabMaster hardware
										//has been performed to date.
			ret.push_back(80 * g);
			//TODO: exact sample rates may depend on the acquisition module(s) connected
			break;

		case MODEL_MDA_800:
			ret.push_back(200 * m);
			ret.push_back(500 * m);
			ret.push_back(1250 * m);
			ret.push_back(2500 * m);
			ret.push_back(10 * g);
			break;

		case MODEL_WAVEMASTER_8ZI_B:
			ret.push_back(250 * m);
			ret.push_back(500 * m);
			ret.push_back(1 * g);
			ret.push_back(2500 * m);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			ret.push_back(20 * g);
			ret.push_back(40 * g);
			break;

		case MODEL_WAVEPRO_HD:
			ret.push_back(250 * m);
			ret.push_back(500 * m);
			ret.push_back(1 * g);
			ret.push_back(2500 * m);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			break;

		case MODEL_WAVERUNNER_8K:
			ret.push_back(200 * m);
			ret.push_back(500 * m);
			ret.push_back(1 * g);
			ret.push_back(2 * g);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			if(m_hasFastSampleRate)
				ret.push_back(20 * g);
			break;

		case MODEL_WAVERUNNER_8K_HD:
			ret.push_back(250 * m);
			ret.push_back(500 * m);
			ret.push_back(1250 * m);
			ret.push_back(2500 * m);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			break;

		case MODEL_WAVERUNNER_9K:
			ret.push_back(250 * m);
			ret.push_back(500 * m);
			ret.push_back(1 * g);
			ret.push_back(2 * g);
			ret.push_back(5 * g);
			ret.push_back(10 * g);
			if(m_hasFastSampleRate)
				ret.push_back(20 * g);
			break;

		default:
			break;
	}

	return ret;
}

vector<uint64_t> LeCroyOscilloscope::GetSampleRatesInterleaved()
{
	vector<uint64_t> ret = GetSampleRatesNonInterleaved();

	switch(m_modelid)
	{
		//A few models do not have interleaving capability at all.
		case MODEL_HDO_4KA:
		case MODEL_HDO_6KA:
		case MODEL_LABMASTER_ZI_A:
		case MODEL_MDA_800:
		case MODEL_WAVEMASTER_8ZI_B:
		case MODEL_WAVERUNNER_8K_HD:
			break;

		//Same as non-interleaved, plus double, for all other known scopes
		default:
			ret.push_back(ret[ret.size()-1] * 2);
			break;
	}

	return ret;
}

vector<uint64_t> LeCroyOscilloscope::GetSampleDepthsNonInterleaved()
{
	const int64_t k = 1000;
	const int64_t m = k*k;

	vector<uint64_t> ret;

	//Standard sample depths for everything.
	//The front panel allows going as low as 2 samples on some instruments, but don't allow that here.
	ret.push_back(500);
	ret.push_back(1 * k);
	ret.push_back(2 * k);
	ret.push_back(5 * k);
	ret.push_back(10 * k);
	ret.push_back(20 * k);
	ret.push_back(40 * k);			//20/40 Gsps scopes can use values other than 1/2/5.
									//TODO: figure out which models allow this
	ret.push_back(50 * k);
	ret.push_back(80 * k);
	ret.push_back(100 * k);
	ret.push_back(200 * k);
	ret.push_back(250 * k);
	ret.push_back(400 * k);
	ret.push_back(500 * k);

	ret.push_back(1 * m);
	ret.push_back(2 * m);
	ret.push_back(5 * m);
	ret.push_back(10 * m);

	switch(m_modelid)
	{
		//TODO: are there any options between 10M and 24M? is there a 20M?
		//TODO: XXL option gives 48M
		case MODEL_DDA_5K:
			ret.push_back(24 * m);
			break;

		//VERY limited range of depths here
		case MODEL_HDO_4KA:
			ret.clear();
			ret.push_back(500);
			ret.push_back(10 * k);
			ret.push_back(100 * k);
			ret.push_back(1 * m);
			ret.push_back(2500 * k);
			ret.push_back(5 * m);
			ret.push_back(10 * m);
			ret.push_back(12500 * k);
			break;

		case MODEL_HDO_6KA:
			ret.push_back(25 * m);
			ret.push_back(50 * m);
			break;

		//TODO: seems like we can have multiples of 400 instead of 500 sometimes?
		case MODEL_HDO_9K:
			ret.push_back(25 * m);
			ret.push_back(50 * m);
			ret.push_back(64 * m);
			break;

		//standard memory, are there options to increase this?
		case MODEL_LABMASTER_ZI_A:
			ret.push_back(20 * m);
			break;

		case MODEL_MDA_800:
			ret.push_back(25 * m);
			ret.push_back(50 * m);
			break;

		//standard memory
		case MODEL_WAVEMASTER_8ZI_B:
			break;

		case MODEL_WAVEPRO_HD:
			ret.push_back(25 * m);

			if(m_memoryDepthOption >= 100)
				ret.push_back(50 * m);
			break;

		case MODEL_WAVERUNNER_8K_HD:
			ret.push_back(25 * m);
			ret.push_back(50 * m);

			//FIXME: largest depth is 2-channel mode only
			//Second largest is 2/4 channel mode only
			//All others can be used in 8 channel
			ret.push_back(100 * m);

			if(m_memoryDepthOption >= 200)
				ret.push_back(200 * m);
			if(m_memoryDepthOption >= 500)
				ret.push_back(500 * m);
			if(m_memoryDepthOption >= 1000)
				ret.push_back(1000 * m);
			if(m_memoryDepthOption >= 2000)
				ret.push_back(2000 * m);
			if(m_memoryDepthOption >= 5000)
				ret.push_back(5000 * m);
			break;

		//deep memory option gives us 4x the capacity
		case MODEL_WAVERUNNER_8K:
		case MODEL_WAVERUNNER_9K:
			ret.push_back(16 * m);
			if(m_memoryDepthOption == 128)
			{
				ret.push_back(32 * m);
				ret.push_back(64 * m);
			}
			break;

		//TODO: add more models here
		default:
			break;
	}

	return ret;
}

vector<uint64_t> LeCroyOscilloscope::GetSampleDepthsInterleaved()
{
	vector<uint64_t> base = GetSampleDepthsNonInterleaved();

	//Default to doubling the non-interleaved depths
	vector<uint64_t> ret;
	for(auto rate : base)
		ret.push_back(rate*2);

	switch(m_modelid)
	{
		//DDA5 is weird, not a power of two
		//TODO: XXL option gives 100M, with 48M on all channels
		case MODEL_DDA_5K:
		case MODEL_HDO_4KA:
		case MODEL_HDO_9K:
		case MODEL_WAVERUNNER_8K:
		case MODEL_WAVERUNNER_9K:
		case MODEL_WAVEPRO_HD:
			return ret;

		//memory is dedicated per channel, no interleaving possible
		case MODEL_HDO_6KA:
		case MODEL_LABMASTER_ZI_A:
		case MODEL_MDA_800:
		case MODEL_WAVEMASTER_8ZI_B:
			return base;

		//TODO: add more models here
		default:
			break;
	}

	return ret;
}

set<LeCroyOscilloscope::InterleaveConflict> LeCroyOscilloscope::GetInterleaveConflicts()
{
	set<InterleaveConflict> ret;

	//All scopes normally interleave channels 1/2 and 3/4.
	//If both channels in either pair is in use, that's a problem.
	ret.emplace(InterleaveConflict(m_channels[0], m_channels[1]));
	if(m_analogChannelCount > 2)
		ret.emplace(InterleaveConflict(m_channels[2], m_channels[3]));

	switch(m_modelid)
	{
		//Any use of 1 or 4 disqualifies interleaving in these models
		case MODEL_HDO_9K:
		case MODEL_WAVERUNNER_8K:
			ret.emplace(InterleaveConflict(m_channels[0], m_channels[0]));
			ret.emplace(InterleaveConflict(m_channels[3], m_channels[3]));
			break;

		default:
			break;
	}

	return ret;
}

uint64_t LeCroyOscilloscope::GetSampleRate()
{
	if(!m_sampleRateValid)
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("VBS? 'return = app.Acquisition.Horizontal.SamplingRate'");
		//What's the difference between SampleRate and SamplingRate?
		//Seems like at low speed we want to use SamplingRate, not SampleRate
		string reply = m_transport->ReadReply();

		sscanf(reply.c_str(), "%ld", &m_sampleRate);
		m_sampleRateValid = true;
	}

	return m_sampleRate;
}

uint64_t LeCroyOscilloscope::GetSampleDepth()
{
	if(!m_memoryDepthValid)
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("MSIZ?");
		string reply = m_transport->ReadReply();
		float size;
		sscanf(reply.c_str(), "%f", &size);

		m_memoryDepth = size;
		m_memoryDepthValid = true;
	}

	return m_memoryDepth;
}

void LeCroyOscilloscope::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "MSIZ %lu", depth);
	m_transport->SendCommand(tmp);
	m_memoryDepth = depth;

	//We need to reconfigure the trigger in order to keep the offset left-aligned when changing depth
	size_t off = GetTriggerOffset();
	m_triggerOffsetValid = false;
	SetTriggerOffset(off);
}

void LeCroyOscilloscope::SetSampleRate(uint64_t rate)
{
	uint64_t ps_per_sample = 1000000000000L / rate;
	double time_per_sample = ps_per_sample * 1.0e-12;
	double time_per_plot = time_per_sample * GetSampleDepth();
	double time_per_div = time_per_plot / 10;
	m_sampleRate = rate;

	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "TDIV %.0e", time_per_div);
	m_transport->SendCommand(tmp);
}

void LeCroyOscilloscope::EnableTriggerOutput()
{
	//Enable 400ns trigger-out pulse, 1V p-p
	m_transport->SendCommand("VBS? 'app.Acquisition.AuxOutput.AuxMode=\"TriggerOut\"'");
	m_transport->SendCommand("VBS? 'app.Acquisition.AuxOutput.TrigOutPulseWidth=4e-7'");
	m_transport->SendCommand("VBS? 'app.Acquisition.AuxOutput.Amplitude=1'");
}

void LeCroyOscilloscope::SetUseExternalRefclk(bool external)
{
	if(external)
		m_transport->SendCommand("RCLK EXTERNAL");
	else
		m_transport->SendCommand("RCLK INTERNAL");
}

void LeCroyOscilloscope::SetTriggerOffset(int64_t offset)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//LeCroy's standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(1e12f * halfdepth / rate));

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "TRDL %e", (offset - halfwidth) * 1e-12);
	m_transport->SendCommand(tmp);

	//Don't update the cache because the scope is likely to round the offset we ask for.
	//If we query the instrument later, the cache will be updated then.
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_triggerOffsetValid = false;
}

int64_t LeCroyOscilloscope::GetTriggerOffset()
{
	//Early out if the value is in cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_triggerOffsetValid)
			return m_triggerOffset;
	}

	string reply;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("TRDL?");
		reply = m_transport->ReadReply();
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Result comes back in scientific notation
	double sec;
	sscanf(reply.c_str(), "%le", &sec);
	m_triggerOffset = static_cast<int64_t>(round(sec * 1e12));

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(1e12f * halfdepth / rate));
	m_triggerOffset += halfwidth;

	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

void LeCroyOscilloscope::SetDeskewForChannel(size_t channel, int64_t skew)
{
	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return;

	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS? 'app.Acquisition.%s.Deskew=%e'",
		m_channels[channel]->GetHwname().c_str(),
		skew * 1e-12
		);
	m_transport->SendCommand(tmp);

	//Update cache
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew;
}

int64_t LeCroyOscilloscope::GetDeskewForChannel(size_t channel)
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

	//Read the deskew
	lock_guard<recursive_mutex> lock(m_mutex);
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS? 'return = app.Acquisition.%s.Deskew'", m_channels[channel]->GetHwname().c_str());
	m_transport->SendCommand(tmp);
	string reply = m_transport->ReadReply();

	//Value comes back as floating point ps
	float skew;
	sscanf(reply.c_str(), "%f", &skew);
	int64_t skew_ps = round(skew * 1e12f);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew_ps;

	return skew_ps;
}

bool LeCroyOscilloscope::IsInterleaving()
{
	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_interleavingValid)
			return m_interleaving;
	}

	lock_guard<recursive_mutex> lock(m_mutex);

	m_transport->SendCommand("COMBINE_CHANNELS?");
	auto reply = m_transport->ReadReply();
	if(reply[0] == '1')
		m_interleaving = false;
	else if(reply[0] == '2')
		m_interleaving = true;

	//We don't support "auto" mode. Default to off for now
	else
	{
		m_transport->SendCommand("COMBINE_CHANNELS 1");
		m_interleaving = false;
	}

	m_interleavingValid = true;
	return m_interleaving;
}

bool LeCroyOscilloscope::SetInterleaving(bool combine)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Setting to "off" always is possible
	if(!combine)
	{
		m_transport->SendCommand("COMBINE_CHANNELS 1");

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_interleaving = false;
		m_interleavingValid = true;
	}

	//Turning on requires we check for conflicts
	else if(!CanInterleave())
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_interleaving = false;
		m_interleavingValid = true;
	}

	//All good, turn it on for real
	else
	{
		m_transport->SendCommand("COMBINE_CHANNELS 2");

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_interleaving = true;
		m_interleavingValid = true;
	}

	return m_interleaving;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> LeCroyOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;

	if(m_hasLA)
	{
		for(size_t n=0; n<2; n++)
		{
			DigitalBank bank;

			for(size_t i=0; i<8; i++)
				bank.push_back(m_digitalChannels[i + n*8]);

			banks.push_back(bank);
		}
	}

	return banks;
}

Oscilloscope::DigitalBank LeCroyOscilloscope::GetDigitalBank(size_t channel)
{
	DigitalBank ret;
	if(m_hasLA)
	{
		if(channel <= m_digitalChannels[7]->GetIndex() )
		{
			for(size_t i=0; i<8; i++)
				ret.push_back(m_digitalChannels[i]);
		}
		else
		{
			for(size_t i=0; i<8; i++)
				ret.push_back(m_digitalChannels[i+8]);
		}
	}
	return ret;
}

bool LeCroyOscilloscope::IsDigitalHysteresisConfigurable()
{
	return true;
}

bool LeCroyOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float LeCroyOscilloscope::GetDigitalHysteresis(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if(channel <= m_digitalChannels[7]->GetIndex() )
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxHysteresis0'");
	else
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxHysteresis1'");

	return atof(m_transport->ReadReply().c_str());
}

float LeCroyOscilloscope::GetDigitalThreshold(size_t channel)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	if(channel <= m_digitalChannels[7]->GetIndex() )
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxThreshold0'");
	else
		m_transport->SendCommand("VBS? 'return = app.LogicAnalyzer.MSxxThreshold1'");

	return atof(m_transport->ReadReply().c_str());
}

void LeCroyOscilloscope::SetDigitalHysteresis(size_t channel, float level)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[128];
	if(channel <= m_digitalChannels[7]->GetIndex() )
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxHysteresis0 = %e'", level);
	else
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxHysteresis1 = %e'", level);
	m_transport->SendCommand(tmp);
}

void LeCroyOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	lock_guard<recursive_mutex> lock(m_mutex);

	char tmp[128];
	if(channel <= m_digitalChannels[7]->GetIndex() )
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxThreshold0 = %e'", level);
	else
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxThreshold1 = %e'", level);
	m_transport->SendCommand(tmp);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

void LeCroyOscilloscope::PullTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Figure out what kind of trigger is active.
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Type'");
	string reply = Trim(m_transport->ReadReply());
	if (reply == "Dropout")
		PullDropoutTrigger();
	else if (reply == "Edge")
		PullEdgeTrigger();
	else if (reply == "Glitch")
		PullGlitchTrigger();
	else if (reply == "Runt")
		PullRuntTrigger();
	else if (reply == "SlewRate")
		PullSlewRateTrigger();
	else if (reply == "UART")
		PullUartTrigger();
	else if (reply == "Width")
		PullPulseWidthTrigger();
	else if (reply == "Window")
		PullWindowTrigger();

	//Unrecognized trigger type
	else
	{
		LogWarning("Unknown trigger type \"%s\"\n", reply.c_str());
		m_trigger = NULL;
		return;
	}

	//Pull the source (same for all types of trigger)
	PullTriggerSource(m_trigger);

	//TODO: holdoff
}

/**
	@brief Reads the source of a trigger from the instrument
 */
void LeCroyOscilloscope::PullTriggerSource(Trigger* trig)
{
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Source'");		//not visible in XStream Browser?
	string reply = Trim(m_transport->ReadReply());
	auto chan = GetChannelByHwName(reply);
	trig->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source \"%s\"\n", reply.c_str());
}

/**
	@brief Reads settings for a dropout trigger from the instrument
 */
void LeCroyOscilloscope::PullDropoutTrigger()
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
	DropoutTrigger* dt = dynamic_cast<DropoutTrigger*>(m_trigger);

	//Level
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Dropout.Level'");
	dt->SetLevel(stof(m_transport->ReadReply()));

	//Dropout time
	Unit ps(Unit::UNIT_PS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Dropout.DropoutTime'");
	dt->SetDropoutTime(ps.ParseString(m_transport->ReadReply()));

	//Edge type
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Dropout.Slope'");
	if(Trim(m_transport->ReadReply()) == "Positive")
		dt->SetType(DropoutTrigger::EDGE_RISING);
	else
		dt->SetType(DropoutTrigger::EDGE_FALLING);

	//Reset type
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Dropout.IgnoreLastEdge'");
	if(Trim(m_transport->ReadReply()) == "0")
		dt->SetResetType(DropoutTrigger::RESET_OPPOSITE);
	else
		dt->SetResetType(DropoutTrigger::RESET_NONE);
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void LeCroyOscilloscope::PullEdgeTrigger()
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

	//Level
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Edge.Level'");
	et->SetLevel(stof(m_transport->ReadReply()));

	//TODO: OptimizeForHF (changes hysteresis for fast signals)

	//Slope
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Edge.Slope'");
	GetTriggerSlope(et, Trim(m_transport->ReadReply()));
}

/**
	@brief Reads settings for a glitch trigger from the instrument
 */
void LeCroyOscilloscope::PullGlitchTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<GlitchTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new GlitchTrigger(this);
	GlitchTrigger* gt = dynamic_cast<GlitchTrigger*>(m_trigger);

	//Level
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Glitch.Level'");
	gt->SetLevel(stof(m_transport->ReadReply()));

	//Slope
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Glitch.Slope'");
	GetTriggerSlope(gt, Trim(m_transport->ReadReply()));

	//Condition
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Glitch.Condition'");
	gt->SetCondition(GetCondition(m_transport->ReadReply()));

	//Min range
	Unit ps(Unit::UNIT_PS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Glitch.TimeLow'");
	gt->SetLowerBound(ps.ParseString(m_transport->ReadReply()));

	//Max range
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Glitch.TimeHigh'");
	gt->SetUpperBound(ps.ParseString(m_transport->ReadReply()));
}

/**
	@brief Reads settings for an edge trigger from the instrument
 */
void LeCroyOscilloscope::PullPulseWidthTrigger()
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
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);

	//Level
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Width.Level'");
	pt->SetLevel(stof(m_transport->ReadReply()));

	//Condition
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Width.Condition'");
	pt->SetCondition(GetCondition(m_transport->ReadReply()));

	//Min range
	Unit ps(Unit::UNIT_PS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Width.TimeLow'");
	pt->SetLowerBound(ps.ParseString(m_transport->ReadReply()));

	//Max range
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Width.TimeHigh'");
	pt->SetUpperBound(ps.ParseString(m_transport->ReadReply()));

	//Slope
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Width.Slope'");
	GetTriggerSlope(pt, Trim(m_transport->ReadReply()));
}

/**
	@brief Reads settings for a runt-pulse trigger from the instrument
 */
void LeCroyOscilloscope::PullRuntTrigger()
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
	RuntTrigger* rt = dynamic_cast<RuntTrigger*>(m_trigger);

	//Lower bound
	Unit v(Unit::UNIT_VOLTS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Runt.LowerLevel'");
	rt->SetLowerBound(v.ParseString(m_transport->ReadReply()));

	//Upper bound
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Runt.UpperLevel'");
	rt->SetUpperBound(v.ParseString(m_transport->ReadReply()));

	//Lower interval
	Unit ps(Unit::UNIT_PS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Runt.TimeLow'");
	rt->SetLowerInterval(ps.ParseString(m_transport->ReadReply()));

	//Upper interval
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Runt.TimeHigh'");
	rt->SetUpperInterval(ps.ParseString(m_transport->ReadReply()));

	//Slope
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Runt.Slope'");
	auto reply = Trim(m_transport->ReadReply());
	if(reply == "Positive")
		rt->SetSlope(RuntTrigger::EDGE_RISING);
	else if(reply == "Negative")
		rt->SetSlope(RuntTrigger::EDGE_FALLING);

	//Condition
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Runt.Condition'");
	rt->SetCondition(GetCondition(m_transport->ReadReply()));
}

/**
	@brief Reads settings for a slew rate trigger from the instrument
 */
void LeCroyOscilloscope::PullSlewRateTrigger()
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
	SlewRateTrigger* st = dynamic_cast<SlewRateTrigger*>(m_trigger);

	//Lower bound
	Unit v(Unit::UNIT_VOLTS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.SlewRate.LowerLevel'");
	st->SetLowerBound(v.ParseString(m_transport->ReadReply()));

	//Upper bound
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.SlewRate.UpperLevel'");
	st->SetUpperBound(v.ParseString(m_transport->ReadReply()));

	//Lower interval
	Unit ps(Unit::UNIT_PS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.SlewRate.TimeLow'");
	st->SetLowerInterval(ps.ParseString(m_transport->ReadReply()));

	//Upper interval
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.SlewRate.TimeHigh'");
	st->SetUpperInterval(ps.ParseString(m_transport->ReadReply()));

	//Slope
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.SlewRate.Slope'");
	auto reply = Trim(m_transport->ReadReply());
	if(reply == "Positive")
		st->SetSlope(SlewRateTrigger::EDGE_RISING);
	else if(reply == "Negative")
		st->SetSlope(SlewRateTrigger::EDGE_FALLING);

	//Condition
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.SlewRate.Condition'");
	st->SetCondition(GetCondition(m_transport->ReadReply()));
}

/**
	@brief Reads settings for a UART trigger from the instrument
 */
void LeCroyOscilloscope::PullUartTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != NULL) && (dynamic_cast<UartTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new UartTrigger(this);
	UartTrigger* ut = dynamic_cast<UartTrigger*>(m_trigger);

	//Bit rate
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.BitRate'");
	ut->SetBitRate(stoi(m_transport->ReadReply()));

	//Level
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.LevelAbsolute'");
	ut->SetLevel(stof(m_transport->ReadReply()));

	//Ignore ByteBitOrder, assume LSB for now
	//Ignore NumDataBits, assume 8 for now

	/*
		Ignore these as they seem redundant or have unknown functionality:
		* BytesPerStreamWrite
		* DataBytesLenValue1
		* DataBytesLenValue2
		* DataCondition
		* DefaultLevel
		* FrameDelimiter
		* HeaderByteVal
		* InterFrameMinBits
		* NeedDualLevels
		* NeededSources
		* PatternLength
		* PatternPosition
		* RS232Mode (how is this different from polarity inversion?)
		* SupportsDigital
		* UARTCondition
		* ViewingMode
	*/

	//Parity
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.ParityType'");
	auto reply = Trim(m_transport->ReadReply());
	if(reply == "None")
		ut->SetParityType(UartTrigger::PARITY_NONE);
	else if(reply == "Even")
		ut->SetParityType(UartTrigger::PARITY_EVEN);
	else if(reply == "Odd")
		ut->SetParityType(UartTrigger::PARITY_ODD);

	//Operator
	bool ignore_p2 = true;
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.PatternOperator'");
	reply = Trim(m_transport->ReadReply());
	if(reply == "Equal")
		ut->SetCondition(Trigger::CONDITION_EQUAL);
	else if(reply == "NotEqual")
		ut->SetCondition(Trigger::CONDITION_NOT_EQUAL);
	else if(reply == "Smaller")
		ut->SetCondition(Trigger::CONDITION_LESS);
	else if(reply == "SmallerOrEqual")
		ut->SetCondition(Trigger::CONDITION_LESS_OR_EQUAL);
	else if(reply == "Greater")
		ut->SetCondition(Trigger::CONDITION_GREATER);
	else if(reply == "GreaterOrEqual")
		ut->SetCondition(Trigger::CONDITION_GREATER_OR_EQUAL);
	else if(reply == "InRange")
	{
		ignore_p2 = false;
		ut->SetCondition(Trigger::CONDITION_BETWEEN);
	}
	else if(reply == "OutRange")
	{
		ignore_p2 = false;
		ut->SetCondition(Trigger::CONDITION_NOT_BETWEEN);
	}

	//Idle polarity
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.Polarity'");
	reply = Trim(m_transport->ReadReply());
	if(reply == "IdleHigh")
		ut->SetPolarity(UartTrigger::IDLE_HIGH);
	else if(reply == "IdleLow")
		ut->SetPolarity(UartTrigger::IDLE_LOW);

	//Stop bits
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.StopBitLength'");
	ut->SetStopBits(stof(Trim(m_transport->ReadReply())));

	//Trigger type
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.TrigOnBadParity'");
	reply = Trim(m_transport->ReadReply());
	if(reply == "-1")
		ut->SetMatchType(UartTrigger::TYPE_PARITY_ERR);
	else
		ut->SetMatchType(UartTrigger::TYPE_DATA);

	//PatternValue1 / 2
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.PatternValue'");
	string p1 = Trim(m_transport->ReadReply());
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Serial.UART.PatternValue2'");
	string p2 = Trim(m_transport->ReadReply());
	ut->SetPatterns(p1, p2, ignore_p2);
}

/**
	@brief Reads settings for a window trigger from the instrument
 */
void LeCroyOscilloscope::PullWindowTrigger()
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
	WindowTrigger* wt = dynamic_cast<WindowTrigger*>(m_trigger);

	//Lower bound
	Unit v(Unit::UNIT_VOLTS);
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Window.LowerLevel'");
	wt->SetLowerBound(v.ParseString(m_transport->ReadReply()));

	//Upper bound
	m_transport->SendCommand("VBS? 'return = app.Acquisition.Trigger.Window.UpperLevel'");
	wt->SetUpperBound(v.ParseString(m_transport->ReadReply()));
}

/**
	@brief Processes the slope for an edge or edge-derived trigger
 */
void LeCroyOscilloscope::GetTriggerSlope(EdgeTrigger* trig, string reply)
{
	reply = Trim(reply);

	if(reply == "Positive")
		trig->SetType(EdgeTrigger::EDGE_RISING);
	else if(reply == "Negative")
		trig->SetType(EdgeTrigger::EDGE_FALLING);
	else if(reply == "Either")
		trig->SetType(EdgeTrigger::EDGE_ANY);
	else
		LogWarning("Unknown trigger slope %s\n", reply.c_str());
}

/**
	@brief Parses a trigger condition
 */
Trigger::Condition LeCroyOscilloscope::GetCondition(string reply)
{
	reply = Trim(reply);

	if(reply == "LessThan")
		return Trigger::CONDITION_LESS;
	else if(reply == "GreaterThan")
		return Trigger::CONDITION_GREATER;
	else if(reply == "InRange")
		return Trigger::CONDITION_BETWEEN;
	else if(reply == "OutOfRange")
		return Trigger::CONDITION_NOT_BETWEEN;

	//unknown
	return Trigger::CONDITION_LESS;
}

void LeCroyOscilloscope::PushTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);

	//Source is the same for every channel
	char tmp[128];
	snprintf(
		tmp,
		sizeof(tmp),
		"VBS? 'app.Acquisition.Trigger.Source = \"%s\"'",
		m_trigger->GetInput(0).m_channel->GetHwname().c_str());
	m_transport->SendCommand(tmp);

	//The rest depends on the type
	auto dt = dynamic_cast<DropoutTrigger*>(m_trigger);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	auto gt = dynamic_cast<GlitchTrigger*>(m_trigger);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto rt = dynamic_cast<RuntTrigger*>(m_trigger);
	auto st = dynamic_cast<SlewRateTrigger*>(m_trigger);
	auto ut = dynamic_cast<UartTrigger*>(m_trigger);
	auto wt = dynamic_cast<WindowTrigger*>(m_trigger);
	if(dt)
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"Dropout\"");
		PushDropoutTrigger(dt);
	}
	else if(pt)
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"Width\"");
		PushPulseWidthTrigger(pt);
	}
	else if(gt)
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"Glitch\"");
		PushGlitchTrigger(gt);
	}
	else if(rt)
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"Runt\"");
		PushRuntTrigger(rt);
	}
	else if(st)
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"SlewRate\"");
		PushSlewRateTrigger(st);
	}
	else if(ut)
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"UART\"");
		PushUartTrigger(ut);
	}
	else if(wt)
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"Window\"");
		PushWindowTrigger(wt);
	}
	else if(et)	//must be last
	{
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Type = \"Edge\"");
		PushEdgeTrigger(et, "app.Acquisition.Trigger.Edge");
	}

	else
		LogWarning("Unknown trigger type (not an edge)\n");
}

/**
	@brief Pushes settings for a dropout trigger to the instrument
 */
void LeCroyOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
{
	PushFloat("app.Acquisition.Trigger.Dropout.Level", trig->GetLevel());
	PushFloat("app.Acquisition.Trigger.Dropout.DropoutTime", trig->GetDropoutTime() * 1e-12f);

	if(trig->GetResetType() == DropoutTrigger::RESET_OPPOSITE)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Dropout.IgnoreLastEdge = 0'");
	else
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Dropout.IgnoreLastEdge = -1'");

	if(trig->GetType() == DropoutTrigger::EDGE_RISING)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Dropout.Slope = \"Positive\"'");
	else
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Dropout.Slope = \"Negative\"'");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void LeCroyOscilloscope::PushEdgeTrigger(EdgeTrigger* trig, const string& tree)
{
	//Level
	PushFloat(tree + ".Level", trig->GetLevel());

	//Slope
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommand(string("VBS? '") + tree + ".Slope = \"Positive\"'");
			break;

		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommand(string("VBS? '") + tree + ".Slope = \"Negative\"'");
			break;

		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommand(string("VBS? '") + tree + ".Slope = \"Either\"'");
			break;

		default:
			LogWarning("Invalid trigger type %d\n", trig->GetType());
			break;
	}
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument
 */
void LeCroyOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	PushEdgeTrigger(trig, "app.Acquisition.Trigger.Width");
	PushCondition("app.Acquisition.Trigger.Width.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.Width.TimeHigh", trig->GetUpperBound() * 1e-12f);
	PushFloat("app.Acquisition.Trigger.Width.TimeLow", trig->GetLowerBound() * 1e-12f);
}

/**
	@brief Pushes settings for a glitch trigger to the instrument
 */
void LeCroyOscilloscope::PushGlitchTrigger(GlitchTrigger* trig)
{
	PushEdgeTrigger(trig, "app.Acquisition.Trigger.Glitch");
	PushCondition("app.Acquisition.Trigger.Glitch.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.Glitch.TimeHigh", trig->GetUpperBound() * 1e-12f);
	PushFloat("app.Acquisition.Trigger.Glitch.TimeLow", trig->GetLowerBound() * 1e-12f);
}

/**
	@brief Pushes settings for a runt trigger to the instrument
 */
void LeCroyOscilloscope::PushRuntTrigger(RuntTrigger* trig)
{
	PushCondition("app.Acquisition.Trigger.Runt.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.Runt.TimeHigh", trig->GetUpperInterval() * 1e-12f);
	PushFloat("app.Acquisition.Trigger.Runt.TimeLow", trig->GetLowerInterval() * 1e-12f);
	PushFloat("app.Acquisition.Trigger.Runt.UpperLevel", trig->GetUpperBound());
	PushFloat("app.Acquisition.Trigger.Runt.LowerLevel", trig->GetLowerBound());

	if(trig->GetSlope() == RuntTrigger::EDGE_RISING)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Runt.Slope = \"Positive\"");
	else
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Runt.Slope = \"Negative\"");
}

/**
	@brief Pushes settings for a slew rate trigger to the instrument
 */
void LeCroyOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
{
	PushCondition("app.Acquisition.Trigger.SlewRate.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.SlewRate.TimeHigh", trig->GetUpperInterval() * 1e-12f);
	PushFloat("app.Acquisition.Trigger.SlewRate.TimeLow", trig->GetLowerInterval() * 1e-12f);
	PushFloat("app.Acquisition.Trigger.SlewRate.UpperLevel", trig->GetUpperBound());
	PushFloat("app.Acquisition.Trigger.SlewRate.LowerLevel", trig->GetLowerBound());

	if(trig->GetSlope() == SlewRateTrigger::EDGE_RISING)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.SlewRate.Slope = \"Positive\"");
	else
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.SlewRate.Slope = \"Negative\"");
}

/**
	@brief Pushes settings for a UART trigger to the instrument
 */
void LeCroyOscilloscope::PushUartTrigger(UartTrigger* trig)
{
	//Special parameter for trigger level
	PushFloat("app.Acquisition.Trigger.Serial.LevelAbsolute", trig->GetLevel());

	//AtPosition
	//Bit9State
	PushFloat("app.Acquisition.Trigger.Serial.UART.BitRate", trig->GetBitRate());
	m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.ByteBitOrder = \"LSB\"");
	//DataBytesLenValue1
	//DataBytesLenValue2
	//DataCondition
	//FrameDelimiter
	//InterframeMinBits
	//NeedDualLevels
	//NeededSources
	m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.NumDataBits = \"8\"");

	switch(trig->GetParityType())
	{
		case UartTrigger::PARITY_NONE:
			m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.ParityType = \"None\"");
			break;

		case UartTrigger::PARITY_ODD:
			m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.ParityType = \"Odd\"");
			break;

		case UartTrigger::PARITY_EVEN:
			m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.ParityType = \"Even\"");
			break;
	}

	//Pattern length depends on the current format.
	//Note that the pattern length is in bytes, not bits, even though patterns are in binary.
	auto pattern1 = trig->GetPattern1();
	char tmp[256];
	snprintf(tmp, sizeof(tmp),
		"VBS? 'app.Acquisition.Trigger.Serial.UART.PatternLength = \"%d\"",
		(int)pattern1.length() / 8);
	m_transport->SendCommand(tmp);

	PushPatternCondition("app.Acquisition.Trigger.Serial.UART.PatternOperator", trig->GetCondition());

	//PatternPosition

	m_transport->SendCommand(
		string("VBS? 'app.Acquisition.Trigger.Serial.UART.PatternValue = \"") + pattern1 + " \"'");

	//PatternValue2 only for Between/NotBetween
	switch(trig->GetCondition())
	{
		case Trigger::CONDITION_BETWEEN:
		case Trigger::CONDITION_NOT_BETWEEN:
			m_transport->SendCommand(
				string("VBS? 'app.Acquisition.Trigger.Serial.UART.PatternValue2 = \"") + trig->GetPattern2() + " \"'");
			break;

		default:
			break;
	}

	//Polarity
	if(trig->GetPolarity() == UartTrigger::IDLE_HIGH)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.Polarity = \"IdleHigh\"");
	else
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.Polarity = \"IdleLow\"");

	m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.RS232Mode = \"0\" ");

	auto nstop = trig->GetStopBits();
	if(nstop == 1)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.StopBitLength = \"1bit\"");
	else if(nstop == 2)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.StopBitLength = \"2bits\"");
	else
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.StopBitLength = \"1.5bit\"");

	//Match type
	if(trig->GetMatchType() == UartTrigger::TYPE_DATA)
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.TrigOnBadParity = \"0\"");
	else
		m_transport->SendCommand("VBS? 'app.Acquisition.Trigger.Serial.UART.TrigOnBadParity = \"-1\"");

	//UARTCondition
	//ViewingMode
}

/**
	@brief Pushes settings for a window trigger to the instrument
 */
void LeCroyOscilloscope::PushWindowTrigger(WindowTrigger* trig)
{
	PushFloat("app.Acquisition.Trigger.Window.LowerLevel", trig->GetLowerBound());
	PushFloat("app.Acquisition.Trigger.Window.UpperLevel", trig->GetUpperBound());
}

/**
	@brief Pushes settings for a trigger condition under a .Condition field
 */
void LeCroyOscilloscope::PushCondition(const string& path, Trigger::Condition cond)
{
	switch(cond)
	{
		case Trigger::CONDITION_LESS:
			m_transport->SendCommand(string("VBS? '") + path + " = \"LessThan\"'");
			break;

		case Trigger::CONDITION_GREATER:
			m_transport->SendCommand(string("VBS? '") + path + " = \"GreaterThan\"'");
			break;

		case Trigger::CONDITION_BETWEEN:
			m_transport->SendCommand(string("VBS? '") + path + " = \"InRange\"'");
			break;

		case Trigger::CONDITION_NOT_BETWEEN:
			m_transport->SendCommand(string("VBS? '") + path + " = \"OutOfRange\"'");
			break;

		//Other values are not legal here, it seems
		default:
			break;
	}
}

/**
	@brief Pushes settings for a trigger condition under a .PatternOperator field
 */
void LeCroyOscilloscope::PushPatternCondition(const string& path, Trigger::Condition cond)
{
	//Note that these enum strings are NOT THE SAME as used by PushCondition()!
	//For example CONDITION_LESS is "Smaller" vs "LessThan"
	switch(cond)
	{
		case Trigger::CONDITION_EQUAL:
			m_transport->SendCommand(string("VBS? '") + path + " = \"Equal\"'");
			break;

		case Trigger::CONDITION_NOT_EQUAL:
			m_transport->SendCommand(string("VBS? '") + path + " = \"NotEqual\"'");
			break;

		case Trigger::CONDITION_LESS:
			m_transport->SendCommand(string("VBS? '") + path + " = \"Smaller\"'");
			break;

		case Trigger::CONDITION_LESS_OR_EQUAL:
			m_transport->SendCommand(string("VBS? '") + path + " = \"SmallerOrEqual\"'");
			break;

		case Trigger::CONDITION_GREATER:
			m_transport->SendCommand(string("VBS? '") + path + " = \"Greater\"'");
			break;

		case Trigger::CONDITION_GREATER_OR_EQUAL:
			m_transport->SendCommand(string("VBS? '") + path + " = \"GreaterOrEqual\"'");
			break;

		case Trigger::CONDITION_BETWEEN:
			m_transport->SendCommand(string("VBS? '") + path + " = \"InRange\"'");
			break;

		case Trigger::CONDITION_NOT_BETWEEN:
			m_transport->SendCommand(string("VBS? '") + path + " = \"OutRange\"'");
			break;

		//CONDITION_ANY not supported by LeCroy scopes
		default:
			break;
	}
}

void LeCroyOscilloscope::PushFloat(string path, float f)
{
	char tmp[128];
	snprintf(
		tmp,
		sizeof(tmp),
		"VBS? '%s = %e'",
		path.c_str(),
		f);
	m_transport->SendCommand(tmp);
}

vector<string> LeCroyOscilloscope::GetTriggerTypes()
{
	vector<string> ret;
	ret.push_back(DropoutTrigger::GetTriggerName());
	ret.push_back(EdgeTrigger::GetTriggerName());
	ret.push_back(GlitchTrigger::GetTriggerName());
	ret.push_back(PulseWidthTrigger::GetTriggerName());
	ret.push_back(RuntTrigger::GetTriggerName());
	ret.push_back(SlewRateTrigger::GetTriggerName());
	if(m_hasUartTrigger)
		ret.push_back(UartTrigger::GetTriggerName());
	ret.push_back(WindowTrigger::GetTriggerName());

	//TODO m_hasI2cTrigger m_hasSpiTrigger m_hasUartTrigger
	return ret;
}
