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
	@brief Implementation of LeCroyOscilloscope
	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "LeCroyOscilloscope.h"
#include "base64.h"

#include "CDR8B10BTrigger.h"
#include "CDRNRZPatternTrigger.h"
#include "DropoutTrigger.h"
#include "EdgeTrigger.h"
#include "GlitchTrigger.h"
#include "PulseWidthTrigger.h"
#include "RuntTrigger.h"
#include "SlewRateTrigger.h"
#include "UartTrigger.h"
#include "WindowTrigger.h"

#include <cinttypes>
#include <locale>
#include <omp.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyOscilloscope::LeCroyOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_hasLA(false)
	, m_hasDVM(false)
	, m_hasFunctionGen(false)
	, m_hasFastSampleRate(false)
	, m_memoryDepthOption(0)
	, m_hasI2cTrigger(false)
	, m_hasSpiTrigger(false)
	, m_hasUartTrigger(false)
	, m_has8b10bTrigger(false)
	, m_hasNrzTrigger(false)
	, m_hasXdev(false)
	, m_maxBandwidth(10000)
	, m_triggerArmed(false)
	, m_triggerReallyArmed(false)
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
	, m_dmmAutorangeValid(false)
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
		"#808080",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_channels.push_back(m_extTrigChannel);

	//Add dummy channels for AC line and internal fast edge source
	m_acLineChannel = new OscilloscopeChannel(
		this,
		"Line",
		"#808080",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_acLineChannel->SetDisplayName("ACLine");
	m_channels.push_back(m_acLineChannel);

	m_fastEdgeChannel = new OscilloscopeChannel(
		this,
		"FastEdge",
		"#808080",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_channels.push_back(m_fastEdgeChannel);

	//Desired format for waveform data
	//Only use increased bit depth if the scope actually puts content there!
	if(m_highDefinition)
		m_transport->SendCommandQueued("COMM_FORMAT DEF9,WORD,BIN");
	else
		m_transport->SendCommandQueued("COMM_FORMAT DEF9,BYTE,BIN");

	//Always use "fixed sample rate" config for setting timebase
	if(m_modelid != MODEL_WAVESURFER_3K)
		m_transport->SendCommandQueued("VBS 'app.Acquisition.Horizontal.Maximize=\"FixedSampleRate\"'");

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
	m_transport->SendCommandQueued("CHDR OFF");

	//Ask for the ID
	string reply = m_transport->SendCommandQueuedWithReply("*IDN?");
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
	else if(m_model.find("SDA8") == 0)
	{
		if(m_model.find("ZI-B") != string::npos)
			m_modelid = MODEL_SDA_8ZI_B;
		else if(m_model.find("ZI-A") != string::npos)
			m_modelid = MODEL_SDA_8ZI_A;
		else
			m_modelid = MODEL_SDA_8ZI;

		m_maxBandwidth = stoi(m_model.substr(4, 2)) * 1000;
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

	auto reply = m_transport->SendCommandQueuedWithReply("*OPT?");
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
				type = "Protocol Decode";

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

			//GPIB for DDA 5000A
			else if(o == "GPIB1")
			{
				type = "Hardware";
				desc = "GPIB interface";
				action = "Ignoring";
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

			//Memory capacity options for WaveMaster/SDA/DDA 8Zi/Zi-A/Zi-B family
			else if(o == "-S")
			{
				type = "Hardware";
				desc = "Small (32M point) memory";
				m_memoryDepthOption = 32;
				action = "Enabled";
			}
			else if(o == "-S")
			{
				type = "Hardware";
				desc = "Medium (64M point) memory";
				m_memoryDepthOption = 64;
				action = "Enabled";
			}
			else if(o == "-L")
			{
				type = "Hardware";
				desc = "Large (128M point) memory";
				m_memoryDepthOption = 128;
				action = "Enabled";
			}
			else if(o == "-V")
			{
				type = "Hardware";
				desc = "Very large (256M point) memory";
				m_memoryDepthOption = 256;
				action = "Enabled";
			}

			//Memory capacity options for DDA 5000 family
			else if(o == "-XL")
			{
				type = "Hardware";
				desc = "Extra large (48M point) memory";
				m_memoryDepthOption = 48;
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
			//High speed serial trigger
			else if(o == "SERIALPAT_T")
			{
				type = "Trigger";
				desc = "Serial Pattern (8b10b/64b66b/NRZ)";
				action = "Enabling trigger";
				m_has8b10bTrigger = true;
				m_hasNrzTrigger = true;
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
				type = "Protocol Decode";
				desc = "USB2 HSIC";
			}

			//What is DECODE_MEASURE? (with no protocol name)

			//Currently unsupported trigger/decodes, to be added in the future
			else if(o.find("CAN_FD") == 0)		//CAN_FD_TD, CAN_FD_TDM, CAN_FD_TD_SYMB
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

			//QualiPHY
			else if(o.find("QPHY-") == 0)
			{
				type = "Protocol Compliance";
				desc = "QualiPHY";
			}
			else if(o == "HDMI")
			{
				type = "Protocol Compliance";
				desc = "HDMI";
			}

			//Protocol decodes without trigger capability
			//Print out name but otherwise ignore
			else if(o == "10-100M-ENET-BUS")
			{
				type = "Protocol Decode";
				desc = "10/100 Ethernet";
			}
			else if(o == "ENET")
			{
				type = "Protocol Decode";
				desc = "Ethernet";			//TODO: What speed?
			}
			else if(o == "ENET100G")
			{
				type = "Protocol Decode";
				desc = "100G Ethernet";
			}
			else if(o == "10G-ENET-BUS")
			{
				type = "Protocol Decode";
				desc = "10G Ethernet";
			}
			else if(o == "8B10B-BUS")
			{
				type = "Protocol Decode";
				desc = "8B/10B";
			}
			else if(o == "64B66B-BUS")
			{
				type = "Protocol Decode";
				desc = "64B/66B";
			}
			else if(
				(o == "ARINC429") ||
				(o == "ARINC429_DME_SYMB") )
			{
				type = "Protocol Decode";
				desc = "ARINC 429";
			}
			else if(o == "AUTOENETDEBUG")
			{
				type = "Protocol Decode";
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
				type = "Protocol Decode";
				desc = "Electrical Telecom";
			}
			else if(o == "MANCHESTER-BUS")
			{
				type = "Protocol Decode";
				desc = "Manchester";
			}
			else if(o == "MDIO")
			{
				type = "Protocol Decode";
				desc = "Ethernet MDIO";
			}
			else if(o == "MPHY-DECODE")
				desc = "MIPI M-PHY";
			else if(o == "PCIE")
			{
				desc = "PCIe";
				type = "Protocol Decode";	//TODO: What's difference between PCIE and PCIE_D
			}
			else if(o == "PCIE_D")
			{
				desc = "PCIe";
				type = "Protocol Decode";
			}
			else if( (o == "SAS") || (o == "SAS_TD" ) )
			{
				desc = "Serial Attached SCSI";
				type = "Protocol Decode";	//TODO: What's difference between SAS and SAS_TD
			}
			else if(o == "SPACEWIRE")
			{
				type = "Protocol Decode";
				desc = "SpaceWire";
			}
			else if(o == "NRZ-BUS")
			{
				desc = "NRZ";
				type = "Protocol Decode";
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
			else if(o == "DDA")
			{
				type = "Protocol Decode";
				desc = "Disk Drive Analysis";
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
			else if( (o == "SDA") || (o == "SDA2") || (o == "SDA3") || (o == "SDA3-LINQ") || (o == "ASDA") )
			{
				type = "Signal Integrity";
				desc = "Serial Data Analysis";
			}
			else if( (o == "THREEPHASEHARMONICS") || (o == "THREEPHASEPOWER") )
			{
				type = "Miscellaneous";
				desc = "3-Phase Power Analysis";
			}
			else if(o == "AORM")
			{
				type = "Measurement";
				desc = "Advanced Optical Recording Measurements";
			}
			else if(o == "PMA2")
			{
				type = "Miscellaneous";
				desc = "PowerMeasure Analysis";
			}
			else if(o == "CROSS-SYNC-PHY")
			{
				type = "Miscellaneous";
				desc = "Protocol analyzer cross-trigger";
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
				action = "FastWavePort waveform download available";
				m_hasXdev = true;
			}

			//Ignore meta-options
			else if(o == "DEMO-BUNDLE")
			{
				type = "Informational";
				desc = "Software licenses are demo/trial";
			}
			else if(o == "DEMOSCOPE")
			{
				type = "Informational";
				desc = "Scope is a demo unit";
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

	if(m_hasFunctionGen)
	{
		m_awgChannel = new FunctionGeneratorChannel(this, "AWG", "#808080", m_channels.size());
		m_channels.push_back(m_awgChannel);
	}
	else
		m_awgChannel = nullptr;

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
			GetDefaultChannelColor(m_channels.size()),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_COUNTS),
			Stream::STREAM_TYPE_DIGITAL,
			m_channels.size());
		m_channels.push_back(chan);
		m_digitalChannels.push_back(chan);
	}

	//Set the threshold to "user defined" vs using a canned family
	m_transport->SendCommandQueued("VBS? 'app.LogicAnalyzer.MSxxLogicFamily0 = \"USERDEFINED\" '");
	m_transport->SendCommandQueued("VBS? 'app.LogicAnalyzer.MSxxLogicFamily1 = \"USERDEFINED\" '");

	//Select display to be "CUSTOM" so we can assign nicknames to the bits
	m_transport->SendCommandQueued("VBS 'app.LogicAnalyzer.Digital1.Labels=\"CUSTOM\"'");
}

/**
	@brief Figures out how many analog channels we have, and add them to the device

	If you're lucky, the last digit of the model number will be the number of channels (HDO9204)

	But, since we can't have nice things, theres are plenty of exceptions. Known formats so far:
	* WAVERUNNER8104-MS has 4 channels (plus 16 digital)
	* DDA5005 / DDA5005A have 4 channels
	* SDA3010 have 4 channels
	* SDA8xxZi have 4 channels
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

		//All SDA / WaveMaster 8Zi have 4 channels
		case MODEL_SDA_8ZI:
		case MODEL_SDA_8ZI_A:
		case MODEL_SDA_8ZI_B:
		case MODEL_WAVEMASTER_8ZI_B:
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
					auto reply = m_transport->SendCommandQueuedWithReply(tmp);

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
			color,
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_ANALOG,
			i));
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
	m_channelDigitalThresholds.clear();
	m_channelsEnabled.clear();
	m_channelDeskew.clear();
	m_probeIsActive.clear();
	m_channelNavg.clear();
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_triggerOffsetValid = false;
	m_interleavingValid = false;
	m_meterModeValid = false;
	m_dmmAutorangeValid = false;

	//Clear cached display name of all channels
	for(auto c : m_channels)
	{
		if(GetInstrumentTypesForChannel(c->GetIndex()) & Instrument::INST_OSCILLOSCOPE)
			c->ClearCachedDisplayName();
	}
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
unsigned int LeCroyOscilloscope::GetInstrumentTypes() const
{
	unsigned int type = INST_OSCILLOSCOPE;
	if(m_hasDVM)
		type |= INST_DMM;
	if(m_hasFunctionGen)
		type |= INST_FUNCTION;
	return type;
}

uint32_t LeCroyOscilloscope::GetInstrumentTypesForChannel(size_t i) const
{
	//AWG outputs
	if(m_hasFunctionGen && (i == m_awgChannel->GetIndex()))
		return Instrument::INST_FUNCTION;

	//If we get here, it's an oscilloscope channel
	//Report DMM functionality if available
	if(m_hasDVM && (i < m_analogChannelCount))
		return Instrument::INST_OSCILLOSCOPE | Instrument::INST_DMM;
	else
		return Instrument::INST_OSCILLOSCOPE;
}

string LeCroyOscilloscope::GetName() const
{
	return m_model;
}

string LeCroyOscilloscope::GetVendor() const
{
	return m_vendor;
}

string LeCroyOscilloscope::GetSerial() const
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool LeCroyOscilloscope::IsChannelEnabled(size_t i)
{
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

	//Analog
	if(i < m_analogChannelCount)
	{
		//See if the channel is enabled, hide it if not
		string cmd = GetOscilloscopeChannel(i)->GetHwname() + ":TRACE?";
		auto reply = m_transport->SendCommandQueuedWithReply(cmd);

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		if(reply.find("OFF") == 0)	//may have a trailing newline, ignore that
			m_channelsEnabled[i] = false;
		else
			m_channelsEnabled[i] = true;
	}

	//Digital
	else if( (i >= m_digitalChannelBase) && (i < (m_digitalChannelBase + m_digitalChannelCount) ) )
	{
		//If the digital channel *group* is off, don't show anything
		auto reply = Trim(m_transport->SendCommandQueuedWithReply("VBS? 'return = app.LogicAnalyzer.Digital1.UseGrid'"));
		if(reply == "NotOnGrid")
		{
			lock_guard<recursive_mutex> lock2(m_cacheMutex);
			m_channelsEnabled[i] = false;
			return false;
		}

		//See if the channel is on
		//Note that GetHwname() returns Dn, as used by triggers, not Digitaln, as used here
		size_t nchan = i - m_digitalChannelBase;
		reply = Trim(m_transport->SendCommandQueuedWithReply(
			string("VBS? 'return = app.LogicAnalyzer.Digital1.Digital") + to_string(nchan) + "'"));

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		if(reply == "0")
			m_channelsEnabled[i] = false;
		else
			m_channelsEnabled[i] = true;
	}

	else
		return false;

	return m_channelsEnabled[i];
}

void LeCroyOscilloscope::EnableChannel(size_t i)
{
	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
	{
		//Disable interleaving if we created a conflict
		auto chan = GetOscilloscopeChannel(i);
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

		m_transport->SendCommandQueued(chan->GetHwname() + ":TRACE ON");
	}

	//Digital channel
	else if( (i >= m_digitalChannelBase) && (i < (m_digitalChannelBase + m_digitalChannelCount) ) )
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
			m_transport->SendCommandQueued("VBS 'app.LogicAnalyzer.Digital1.UseGrid=\"YT1\"'");

		//Enable this channel on the hardware
		//Note that GetHwname() returns Dn, as used by triggers, not Digitaln, as used here
		size_t nchan = i - m_digitalChannelBase;
		m_transport->SendCommandQueued(string("VBS 'app.LogicAnalyzer.Digital1.Digital") + to_string(nchan) + " = 1'");
		char tmp[128];
		size_t nbit = (i - m_digitalChannels[0]->GetIndex());
		snprintf(tmp, sizeof(tmp), "VBS 'app.LogicAnalyzer.Digital1.BitIndex%zu = %zu'", nbit, nbit);
		m_transport->SendCommandQueued(tmp);
	}

	m_channelsEnabled[i] = true;
}

bool LeCroyOscilloscope::CanEnableChannel(size_t i)
{
	//In DBI models, additional checks are needed.
	//Separate from normal interleaving
	if(HasDBICapability())
	{
		//DBI active on channel 2 blocks channel 1 from being enabled
		if(i == 0 && IsDBIEnabled(1))
			return false;

		//DBI active on channel 3 blocks channel 4 from being enabled
		if(i == 3 && IsDBIEnabled(2))
			return false;
	}

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
		case MODEL_SDA_8ZI_A:
		case MODEL_SDA_8ZI_B:
		case MODEL_WAVEMASTER_8ZI_A:
		case MODEL_WAVEMASTER_8ZI_B:
		case MODEL_WAVEPRO_HD:
		case MODEL_WAVERUNNER_9K:
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
	m_channelsEnabled[i] = false;

	//If this is an analog channel, just toggle it
	if(i < m_analogChannelCount)
		m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TRACE OFF");

	//Digital channel
	else if(i >= m_digitalChannelBase)
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
			m_transport->SendCommandQueued("VBS 'app.LogicAnalyzer.Digital1.UseGrid=\"NotOnGrid\"'");

		//Disable this channel
		size_t nchan = i - m_digitalChannelBase;
		m_transport->SendCommandQueued(string("VBS 'app.LogicAnalyzer.Digital1.Digital") + to_string(nchan) + " = 0'");
	}
}

vector<OscilloscopeChannel::CouplingType> LeCroyOscilloscope::GetAvailableCouplings(size_t i)
{
	vector<OscilloscopeChannel::CouplingType> ret;

	//For WaveMaster/SDA/DDA scopes, we cannot use 1M ohm coupling if the ProLink input is selected
	bool isProBus = true;
	if(HasInputMux(i))
	{
		if(GetInputMuxSetting(i) == 0)
			isProBus = false;
	}

	if(isProBus)
	{
		ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
		ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	}

	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);
	return ret;
}

OscilloscopeChannel::CouplingType LeCroyOscilloscope::GetChannelCoupling(size_t i)
{
	if(i >= m_analogChannelCount)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;

	string reply;
	{
		reply = Trim(m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":COUPLING?"));
		reply = reply.substr(0,3);
	}

	//Check if we have an active probe connected
	auto name = GetProbeName(i);
	{
		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_probeIsActive[i] = (name != "");
	}

	if(reply == "A1M")
		return OscilloscopeChannel::COUPLE_AC_1M;
	else if(reply == "D1M")
		return OscilloscopeChannel::COUPLE_DC_1M;
	else if(reply == "D50")
		return OscilloscopeChannel::COUPLE_DC_50;
	else if(reply == "GND")
		return OscilloscopeChannel::COUPLE_GND;
	else if( (reply == "DC") || (reply == "DC1") )
		return OscilloscopeChannel::COUPLE_DC_50;

	//invalid
	LogWarning("LeCroyOscilloscope::GetChannelCoupling got invalid coupling %s\n", reply.c_str());
	return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void LeCroyOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(i >= m_analogChannelCount)
		return;

	//Get the old coupling value first.
	//This ensures that m_probeIsActive[i] is valid
	GetChannelCoupling(i);

	//If we have an active probe, don't touch the hardware config
	if(m_probeIsActive[i])
		return;

	switch(type)
	{
		case OscilloscopeChannel::COUPLE_AC_1M:
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUPLING A1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_1M:
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUPLING D1M");
			break;

		case OscilloscopeChannel::COUPLE_DC_50:
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUPLING D50");
			break;

		//treat unrecognized as ground
		case OscilloscopeChannel::COUPLE_GND:
		default:
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":COUPLING GND");
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

	auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":ATTENUATION?");

	double d;
	sscanf(reply.c_str(), "%lf", &d);
	return d;
}

void LeCroyOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	if(i >= m_analogChannelCount)
		return;

	//Get the old coupling value first.
	//This ensures that m_probeIsActive[i] is valid
	GetChannelCoupling(i);

	//Don't allow changing attenuation on active probes
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_probeIsActive[i])
			return;
	}

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:ATTENUATION %f", GetOscilloscopeChannel(i)->GetHwname().c_str(), atten);
	m_transport->SendCommandQueued(cmd);
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

		case MODEL_WAVEMASTER_8ZI:
		case MODEL_WAVEMASTER_8ZI_A:
		case MODEL_WAVEMASTER_8ZI_B:
		case MODEL_SDA_8ZI:
		case MODEL_SDA_8ZI_A:
		case MODEL_SDA_8ZI_B:
			ret.push_back(1000);
			if(m_maxBandwidth >= 4000)
				ret.push_back(3000);
			if(m_maxBandwidth >= 6000)
				ret.push_back(4000);
			if(m_maxBandwidth >= 8000)
				ret.push_back(6000);
			if(m_maxBandwidth >= 13000)
				ret.push_back(8000);
			if(m_maxBandwidth >= 16000)
				ret.push_back(13000);
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
		default:
			break;
	}

	return ret;
}

unsigned int LeCroyOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	if(i > m_analogChannelCount)
		return 0;

	auto reply = m_transport->SendCommandQueuedWithReply("BANDWIDTH_LIMIT?");

	size_t index = reply.find(GetOscilloscopeChannel(i)->GetHwname());
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
	else if(sbw == "8GHZ")
		return 8000;
	else if(sbw == "13GHZ")
		return 13000;

	LogWarning("LeCroyOscilloscope::GetChannelBandwidthLimit got invalid BW limit %s\n", reply.c_str());
	return 0;
}

void LeCroyOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	char cmd[128];
	if(limit_mhz == 0)
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,OFF", GetOscilloscopeChannel(i)->GetHwname().c_str());
	else if(limit_mhz >= 1000)
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,%uGHZ", GetOscilloscopeChannel(i)->GetHwname().c_str(), limit_mhz/1000);
	else
		snprintf(cmd, sizeof(cmd), "BANDWIDTH_LIMIT %s,%uMHZ", GetOscilloscopeChannel(i)->GetHwname().c_str(), limit_mhz);

	m_transport->SendCommandQueued(cmd);
}

bool LeCroyOscilloscope::CanInvert(size_t i)
{
	//All analog channels, and only analog channels, can be inverted
	return (i < m_analogChannelCount);
}

void LeCroyOscilloscope::Invert(size_t i, bool invert)
{
	if(i >= m_analogChannelCount)
		return;

	if(invert)
		m_transport->SendCommandQueued(string("VBS 'app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() + ".Invert = true'");
	else
		m_transport->SendCommandQueued(string("VBS 'app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() + ".Invert = false'");
}

bool LeCroyOscilloscope::IsInverted(size_t i)
{
	if(i >= m_analogChannelCount)
		return false;

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(
		string("VBS? 'return = app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() + ".Invert'"));
	return (reply == "-1");
}

string LeCroyOscilloscope::GetProbeName(size_t i)
{
	if(i >= m_analogChannelCount)
		return "";

	//Step 1: Determine which input is active.
	//There's always a mux selector in software, even if only one is present on the physical acquisition board
	string prefix = string("app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname();
	auto mux = Trim(m_transport->SendCommandQueuedWithReply(string("VBS? 'return = ") + prefix + ".ActiveInput'"));

	//Step 2: Identify the probe connected to this mux channel
	auto name = Trim(m_transport->SendCommandQueuedWithReply(
		string("VBS? 'return = ") + prefix + "." + mux + ".ProbeName'"));

	//API requires empty string if no probe
	if(name == "None")
		return "";
	else
		return name;
}

bool LeCroyOscilloscope::CanAutoZero(size_t i)
{
	if(i >= m_analogChannelCount)
		return false;

	auto probe = GetProbeName(i);

	//Per email w/ Honam at Lecroy Apps (TLC#00291415) there is no command to check for autozero capability
	//So we need to just use heuristics based on known probe names

	//Passive or no probe
	if(probe.empty())
		return false;

	//All differential, current, and power rail probes should support auto zeroing
	else if(probe.find("D") == 0)
		return true;
	else if(probe.find("RP") == 0)
		return true;
	else if(probe.find("CP") == 0)
		return true;

	//Recent firmware reports "ring" for passive probes detected via the resistive ID ring
	//None of these have auto zero capability
	else if(probe.find("Ring") == 0)
		return false;

	//ZS series single ended probes do not
	else if(probe.find("ZS") == 0)
		return false;

	//When in doubt, show the option
	else
	{
		LogWarning(
			"Probe model \"%s\" is unknown. Guessing auto zero might be available.\n"
			"Please contact the glscopeclient developers to add your probe to the database and "
			"eliminate this warning.\n", probe.c_str());
		return true;
	}
}

void LeCroyOscilloscope::AutoZero(size_t i)
{
	if(i >= m_analogChannelCount)
		return;

	//Get the active input and probe name
	string prefix = string("app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname();
	auto mux = Trim(m_transport->SendCommandQueuedWithReply(
		string("VBS? 'return = ") + prefix + ".ActiveInput'"));
	auto name = Trim(m_transport->SendCommandQueuedWithReply(
		string("VBS? 'return = ") + prefix + "." + mux + ".ProbeName'"));
	m_transport->SendCommandQueued(string("VBS? '") + prefix + "." + mux + "." + name + ".AutoZero'");
}

bool LeCroyOscilloscope::HasInputMux(size_t i)
{
	if(i >= m_analogChannelCount)
		return false;

	switch(m_modelid)
	{
		//Add other models with muxes here
		case MODEL_SDA_8ZI:
		case MODEL_SDA_8ZI_A:
		case MODEL_SDA_8ZI_B:
		case MODEL_WAVEMASTER_8ZI_B:
			return true;

		default:
			return false;
	}
}

size_t LeCroyOscilloscope::GetInputMuxSetting(size_t i)
{
	//If no mux, always report 0
	if(!HasInputMux(i))
		return 0;

	//Get the active input and probe name
	string prefix = string("app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname();
	auto mux = Trim(m_transport->SendCommandQueuedWithReply(string("VBS? 'return = ") + prefix + ".ActiveInput'"));
	if(mux == "InputA")
		return 0;
	else if(mux == "InputB")
		return 1;
	else
	{
		LogWarning("Unknown input mux setting %zu\n", i);
		return 0;
	}
}

vector<string> LeCroyOscilloscope::GetInputMuxNames(size_t i)
{
	//All currently supported scopes have this combination
	vector<string> ret;
	if(HasInputMux(i))
	{
		ret.push_back("A (ProLink, upper)");
		ret.push_back("B (ProBus, lower)");
	}
	return ret;
}

void LeCroyOscilloscope::SetInputMux(size_t i, size_t select)
{
	if(i >= m_analogChannelCount)
		return;

	if(!HasInputMux(i))
		return;

	if(select == 0)
	{
		m_transport->SendCommandQueued(
			string("VBS 'app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() + ".ActiveInput = \"InputA\"'");
	}
	else
	{
		m_transport->SendCommandQueued(
			string("VBS 'app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() + ".ActiveInput = \"InputB\"'");
	}
}

void LeCroyOscilloscope::SetChannelDisplayName(size_t i, string name)
{
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return;

	//External trigger cannot be renamed in hardware.
	//TODO: allow clientside renaming?
	if(chan == m_extTrigChannel)
		return;

	//Update in hardware
	if(i < m_analogChannelCount)
		m_transport->SendCommandQueued(string("VBS 'app.Acquisition.") + chan->GetHwname() + ".Alias = \"" + name + "\"");

	else if(i >= m_digitalChannelBase)
	{
		m_transport->SendCommandQueued(string("VBS 'app.LogicAnalyzer.Digital1.CustomBitName") +
			to_string(i - m_digitalChannelBase) + " = \"" + name + "\"");
	}
}

string LeCroyOscilloscope::GetChannelDisplayName(size_t i)
{
	auto chan = GetOscilloscopeChannel(i);
	if(!chan)
		return "";

	//Analog and digital channels use completely different namespaces, as usual.
	//Because clean, orthogonal APIs are apparently for losers?
	string name;
	if(i < m_analogChannelCount)
		name = GetPossiblyEmptyString(string("app.Acquisition.") + chan->GetHwname() + ".Alias");
	else if(i > m_digitalChannelBase)
	{
		auto prop = string("app.LogicAnalyzer.Digital1.CustomBitName") + to_string(i - m_digitalChannelBase);
		name = GetPossiblyEmptyString(prop);

		//Default name, change it to the hwname for now
		if(name.find("Custom.") == 0)
		{
			m_transport->SendCommandQueued(string("VBS '") + prop + " = \"" + chan->GetHwname() + "\"'");
			name = "";
		}
	}

	//External trigger, ACLine, FastEdge cannot be renamed in hardware.
	//TODO: allow clientside renaming?

	//Default to using hwname if no alias defined
	if(name == "")
		name = chan->GetHwname();

	return name;
}

/**
	@brief Get an
 */
string LeCroyOscilloscope::GetPossiblyEmptyString(const string& property)
{
	//Get string length first since reading empty strings is problematic over SCPI
	string slen = Trim(m_transport->SendCommandQueuedWithReply(string("VBS? 'return = Len(") + property + ")'"));
	if(slen == "0")
		return "";

	return Trim(m_transport->SendCommandQueuedWithReply(string("VBS? 'return = ") + property + "'"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

int LeCroyOscilloscope::GetMeterDigits()
{
	return 5;
}

bool LeCroyOscilloscope::GetMeterAutoRange()
{
	if(m_dmmAutorangeValid)
		return m_dmmAutorange;

	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.acquisition.DVM.AutoRange'");
	int ret;
	sscanf(str.c_str(), "%d", &ret);
	m_dmmAutorange = (ret ? true : false);

	m_dmmAutorangeValid = true;
	return m_dmmAutorange;
}

void LeCroyOscilloscope::SetMeterAutoRange(bool enable)
{
	m_dmmAutorange = enable;
	m_dmmAutorangeValid = true;

	if(enable)
		m_transport->SendCommandQueued("VBS 'app.acquisition.DVM.AutoRange = 1'");
	else
		m_transport->SendCommandQueued("VBS 'app.acquisition.DVM.AutoRange = 0'");
}

void LeCroyOscilloscope::StartMeter()
{
	m_transport->SendCommandQueued("VBS 'app.acquisition.DVM.DvmEnable = 1'");
}

void LeCroyOscilloscope::StopMeter()
{
	m_transport->SendCommandQueued("VBS 'app.acquisition.DVM.DvmEnable = 0'");
}

double LeCroyOscilloscope::GetMeterValue()
{
	string reply;

	switch(GetMeterMode())
	{
		case Multimeter::DC_VOLTAGE:
			reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.acquisition.DVM.Voltage'");
			break;

		case Multimeter::DC_RMS_AMPLITUDE:
		case Multimeter::AC_RMS_AMPLITUDE:
			reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.acquisition.DVM.Amplitude'");
			break;

		case Multimeter::FREQUENCY:
			reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.acquisition.DVM.Frequency'");
			break;

		default:
			return 0;
	}

	return stod(reply);
}

int LeCroyOscilloscope::GetMeterChannelCount()
{
	return m_analogChannelCount;
}

int LeCroyOscilloscope::GetCurrentMeterChannel()
{
	if(!m_hasDVM)
		return 0;

	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.acquisition.DVM.DvmSource'");
	int i;
	sscanf(str.c_str(), "C%d", &i);
	return i - 1;	//scope channels are 1 based
}

void LeCroyOscilloscope::SetCurrentMeterChannel(int chan)
{
	char cmd[128];
	snprintf(
		cmd,
		sizeof(cmd),
		"VBS 'app.acquisition.DVM.DvmSource = \"C%d\"",
		chan + 1);	//scope channels are 1 based
	m_transport->SendCommandQueued(cmd);
}

Multimeter::MeasurementTypes LeCroyOscilloscope::GetMeterMode()
{
	if(m_meterModeValid)
		return m_meterMode;

	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.acquisition.DVM.DvmMode'");

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
		default:
			LogWarning("unsupported multimeter mode\n");
			return;

	}

	m_transport->SendCommandQueued(string("VBS 'app.acquisition.DVM.DvmMode = \"") + stype + "\"");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function generator mode

vector<FunctionGenerator::WaveShape> LeCroyOscilloscope::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(FunctionGenerator::SHAPE_SINE);
	ret.push_back(FunctionGenerator::SHAPE_SQUARE);
	ret.push_back(FunctionGenerator::SHAPE_TRIANGLE);
	ret.push_back(FunctionGenerator::SHAPE_PULSE);
	ret.push_back(FunctionGenerator::SHAPE_DC);
	ret.push_back(FunctionGenerator::SHAPE_NOISE);
	return ret;
}

bool LeCroyOscilloscope::GetFunctionChannelActive(int /*chan*/)
{
	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.enable'");
	if(str == "0")
		return false;
	else
		return true;
}

void LeCroyOscilloscope::SetFunctionChannelActive(int /*chan*/, bool on)
{
	if(on)
		m_transport->SendCommandQueued("VBS 'app.wavesource.enable=True'");
	else
		m_transport->SendCommandQueued("VBS 'app.wavesource.enable=False'");
}

float LeCroyOscilloscope::GetFunctionChannelDutyCycle(int /*chan*/)
{
	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.DutyCycle'");
	return stof(str) / 100;
}

void LeCroyOscilloscope::SetFunctionChannelDutyCycle(int /*chan*/, float duty)
{
	string cmd = string("VBS 'app.wavesource.DutyCycle = ") + to_string(duty * 100) + "'";
	m_transport->SendCommandQueued(cmd.c_str());
}

float LeCroyOscilloscope::GetFunctionChannelAmplitude(int /*chan*/)
{
	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.Amplitude'");
	return stof(str);
}

void LeCroyOscilloscope::SetFunctionChannelAmplitude(int /*chan*/, float amplitude)
{
	string cmd = string("VBS 'app.WaveSource.amplitude = ") + to_string(amplitude) + "'";
	m_transport->SendCommandQueued(cmd.c_str());
}

float LeCroyOscilloscope::GetFunctionChannelOffset(int /*chan*/)
{
	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.Offset'");
	return stof(str);
}

void LeCroyOscilloscope::SetFunctionChannelOffset(int /*chan*/, float offset)
{
	string cmd = string("VBS 'app.WaveSource.Offset = ") + to_string(offset) + "'";
	m_transport->SendCommandQueued(cmd.c_str());
}

float LeCroyOscilloscope::GetFunctionChannelFrequency(int /*chan*/)
{
	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.Frequency'");
	return stof(str);
}

void LeCroyOscilloscope::SetFunctionChannelFrequency(int /*chan*/, float hz)
{
	string cmd = string("VBS 'app.WaveSource.Frequency = ") + to_string(hz) + "'";
	m_transport->SendCommandQueued(cmd.c_str());
}

FunctionGenerator::WaveShape LeCroyOscilloscope::GetFunctionChannelShape(int /*chan*/)
{
	auto str = Trim(m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.shape'"));
	if(str == "Sine")
		return FunctionGenerator::SHAPE_SINE;
	else if(str == "Square")
		return FunctionGenerator::SHAPE_SQUARE;
	else if(str == "Triangle")
		return FunctionGenerator::SHAPE_TRIANGLE;
	else if(str == "Pulse")
		return FunctionGenerator::SHAPE_PULSE;
	else if(str == "DC")
		return FunctionGenerator::SHAPE_DC;
	else if(str == "Noise")
		return FunctionGenerator::SHAPE_NOISE;

	//TOD: Arbitrary

	LogWarning("LeCroyOscilloscope::GetFunctionChannelShape unimplemented (%s)\n", str.c_str());
	return FunctionGenerator::SHAPE_SINE;
}

void LeCroyOscilloscope::SetFunctionChannelShape(int /*chan*/, WaveShape shape)
{
	switch(shape)
	{
		case FunctionGenerator::SHAPE_SINE:
			m_transport->SendCommandQueued("VBS 'app.WaveSource.shape = \"Sine\"'");
			break;

		case FunctionGenerator::SHAPE_SQUARE:
			m_transport->SendCommandQueued("VBS 'app.WaveSource.shape = \"Square\"'");
			break;

		case FunctionGenerator::SHAPE_TRIANGLE:
			m_transport->SendCommandQueued("VBS 'app.WaveSource.shape = \"Triangle\"'");
			break;

		case FunctionGenerator::SHAPE_PULSE:
			m_transport->SendCommandQueued("VBS 'app.WaveSource.shape = \"Pulse\"'");
			break;

		case FunctionGenerator::SHAPE_DC:
			m_transport->SendCommandQueued("VBS 'app.WaveSource.shape = \"DC\"'");
			break;

		case FunctionGenerator::SHAPE_NOISE:
			m_transport->SendCommandQueued("VBS 'app.WaveSource.shape = \"Noise\"'");
			break;

		default:
			break;
	}
}

bool LeCroyOscilloscope::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return true;
}

float LeCroyOscilloscope::GetFunctionChannelRiseTime(int /*chan*/)
{
	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.risetime'");
	return stof(str) * FS_PER_SECOND;
}

void LeCroyOscilloscope::SetFunctionChannelRiseTime(int /*chan*/, float fs)
{
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%.10f", fs * SECONDS_PER_FS);
	string cmd = string("VBS 'app.wavesource.risetime = ") + tmp + "'";
	m_transport->SendCommandQueued(cmd.c_str());
}

float LeCroyOscilloscope::GetFunctionChannelFallTime(int /*chan*/)
{
	auto str = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.falltime'");
	return stof(str) * FS_PER_SECOND;
}

void LeCroyOscilloscope::SetFunctionChannelFallTime(int /*chan*/, float fs)
{
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%.10f", fs * SECONDS_PER_FS);
	string cmd = string("VBS 'app.wavesource.falltime = ") + tmp + "'";
	m_transport->SendCommandQueued(cmd.c_str());
}

FunctionGenerator::OutputImpedance LeCroyOscilloscope::GetFunctionChannelOutputImpedance(int /*chan*/)
{
	auto str = Trim(m_transport->SendCommandQueuedWithReply("VBS? 'return = app.WaveSource.load'"));
	if(str == "HiZ")
		return IMPEDANCE_HIGH_Z;
	else
		return IMPEDANCE_50_OHM;
}

void LeCroyOscilloscope::SetFunctionChannelOutputImpedance(int /*chan*/, OutputImpedance z)
{
	if(z == IMPEDANCE_HIGH_Z)
		m_transport->SendCommandQueued("VBS 'app.wavesource.load = \"HiZ\"'");
	else
		m_transport->SendCommandQueued("VBS 'app.wavesource.load = \"50\"'");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

bool LeCroyOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

Oscilloscope::TriggerMode LeCroyOscilloscope::PollTrigger()
{
	//Read the Internal State Change Register
	auto sinr = m_transport->SendCommandQueuedWithReply("INR?");
	int inr = atoi(sinr.c_str());

	//See if we got a waveform
	if(inr & 0x0001)
	{
		//Only mark the trigger as disarmed if this was a one-shot trigger.
		//If this is a repeating trigger, we're still armed from the client's perspective,
		//since AcquireData() will reset the trigger for the next acquisition.
		if(m_triggerOneShot)
			m_triggerArmed = false;

		//May still be conceptually in the "armed" state if this is a repeating trigger
		//but in dead time between triggers
		m_triggerReallyArmed = false;
		return TRIGGER_MODE_TRIGGERED;
	}

	//No waveform, but ready for one?
	if(inr & 0x2000)
	{
		m_triggerArmed = true;
		m_triggerReallyArmed = true;
		return TRIGGER_MODE_RUN;
	}

	//Stopped, no data available
	if(m_triggerArmed)
		return TRIGGER_MODE_RUN;
	else
		return TRIGGER_MODE_STOP;
}

bool LeCroyOscilloscope::PeekTriggerArmed()
{
	if(m_triggerReallyArmed)
		return true;

	switch(m_modelid)
	{
		//WavePro HD and WR8K HD series have direct FPGA "is trigger ready" flag
		//per Honam @ LeCroy apps (TLC#00338786)
		//All other scopes do not as of 2023-09-08 but they are thinking of rolling it out to
		//other recent models in a future FW update
		case MODEL_WAVEPRO_HD:
		case MODEL_WAVERUNNER_8K_HD:
			{
				auto str = Trim(m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.IsTriggerReady'"));
				if(str == "1")
				{
					m_triggerReallyArmed = true;
					return true;
				}
				else
					return false;
			}

		//For all other models: Read the Internal State Change Register
		//This isn't perfect and has a small race window still so add a fixed time delay after it returns ready
		//Experimentally, 5ms seems adequate on a WaveRunner 8404M-MS (measured race window is ~1ms)
		default:
		{
			auto sinr = m_transport->SendCommandQueuedWithReply("INR?");
			int inr = atoi(sinr.c_str());

			if(inr & 0x2000)
			{
				this_thread::sleep_for(chrono::milliseconds(5));
				m_triggerReallyArmed = true;
				return true;
			}

			//TODO: if 0x0001 is set we got a waveform
			//but PollTrigger will miss it!

			return false;
		}
	}
}

bool LeCroyOscilloscope::ReadWaveformBlock(string& data)
{
	//Prefix "DESC,\n" or "DAT1,\n". Always seems to be 6 chars and start with a D.
	//Next is the length header. Looks like #9000000346. #9 followed by nine ASCII length digits.
	//Ignore that too.
	string tmp = m_transport->ReadReply();
	if(tmp.empty())
		return false;
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
	//Check enable state in the cache.
	vector<int> uncached;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelsEnabled.find(i) == m_channelsEnabled.end())
			uncached.push_back(i);
	}

	//Batched implementation
	if(m_transport->IsCommandBatchingSupported())
	{
		lock_guard<recursive_mutex> lock(m_transport->GetMutex());

		for(auto i : uncached)
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":TRACE?");
		m_transport->FlushCommandQueue();
		for(auto i : uncached)
		{
			auto reply = m_transport->ReadReply();

			lock_guard<recursive_mutex> lock2(m_cacheMutex);
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
			auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":TRACE?");

			lock_guard<recursive_mutex> lock(m_cacheMutex);
			if(reply == "OFF")
				m_channelsEnabled[i] = false;
			else
				m_channelsEnabled[i] = true;
		}
	}

	/*
	//Check digital status
	//TODO: better per-lane queries
	m_transport->SendCommandQueued("Digital1:TRACE?");

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
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

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
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":WF? DESC");
		}
	}

	m_transport->FlushCommandQueue();
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
				m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":WF? TIME");
				sent_wavetime = true;
			}

			//Ask for the data
			m_transport->SendCommandQueued(GetOscilloscopeChannel(i)->GetHwname() + ":WF? DAT1");
		}
	}

	//Ask for the digital waveforms
	if(denabled)
		m_transport->SendCommandQueued("Digital1:WF?");
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
	char format[] = "%Y-%m-%d %T";
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

	//cppcheck-suppress invalidPointerCast
	float v_gain = *reinterpret_cast<float*>(pdesc + 156);

	//cppcheck-suppress invalidPointerCast
	float v_off = *reinterpret_cast<float*>(pdesc + 160);

	//cppcheck-suppress invalidPointerCast
	float interval = *reinterpret_cast<float*>(pdesc + 176) * FS_PER_SECOND;

	//cppcheck-suppress invalidPointerCast
	double h_off = *reinterpret_cast<double*>(pdesc + 180) * FS_PER_SECOND;	//fs from start of waveform to trigger

	double h_off_frac = fmodf(h_off, interval);						//fractional sample position, in fs
	if(h_off_frac < 0)
		h_off_frac = interval + h_off_frac;		//double h_unit = *reinterpret_cast<double*>(pdesc + 244);

	//Raw waveform data
	size_t num_samples;
	if(m_highDefinition)
		num_samples = datalen/2;
	else
		num_samples = datalen;
	size_t num_per_segment = num_samples / num_sequences;
	const int16_t* wdata = reinterpret_cast<const int16_t*>(data);
	const int8_t* bdata = reinterpret_cast<const int8_t*>(data);

	for(size_t j=0; j<num_sequences; j++)
	{
		//Set up the capture we're going to store our data into
		auto cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(j)->GetHwname());
		cap->m_timescale = round(interval);
		cap->m_triggerPhase = h_off_frac;
		cap->m_startTimestamp = ttime;
		cap->PrepareForCpuAccess();

		//Parse the time
		if(wavetime)
			cap->m_startFemtoseconds = static_cast<int64_t>( (basetime + wavetime[j*2]) * FS_PER_SECOND );
		else
			cap->m_startFemtoseconds = static_cast<int64_t>(basetime * FS_PER_SECOND);

		cap->Resize(num_per_segment);

		//Convert raw ADC samples to volts
		if(m_highDefinition)
		{
			Convert16BitSamples(
				cap->m_samples.GetCpuPointer(),
				wdata + j*num_per_segment,
				v_gain,
				v_off,
				num_per_segment);
		}
		else
		{
			Convert8BitSamples(
				cap->m_samples.GetCpuPointer(),
				bdata + j*num_per_segment,
				v_gain,
				v_off,
				num_per_segment);
		}

		cap->MarkSamplesModifiedFromCpu();
		ret.push_back(cap);
	}

	return ret;
}

map<int, SparseDigitalWaveform*> LeCroyOscilloscope::ProcessDigitalWaveform(string& data, int64_t analog_hoff)
{
	map<int, SparseDigitalWaveform*> ret;

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
	float interval = atof(tmp.c_str()) * FS_PER_SECOND;
	//LogDebug("Sample interval: %.2f fs\n", interval);

	tmp = data.substr(data.find("<HorStart>") + 10);
	tmp = tmp.substr(0, tmp.find("</HorStart>"));
	float horstart = atof(tmp.c_str()) * FS_PER_SECOND;

	tmp = data.substr(data.find("<NumSamples>") + 12);
	tmp = tmp.substr(0, tmp.find("</NumSamples>"));
	size_t num_samples = atoi(tmp.c_str());
	//LogDebug("Expecting %d samples\n", num_samples);

	//Extract the raw trigger timestamp (nanoseconds since Jan 1 2000)
	tmp = data.substr(data.find("<FirstEventTime>") + 16);
	tmp = tmp.substr(0, tmp.find("</FirstEventTime>"));
	int64_t timestamp;
	if(1 != sscanf(tmp.c_str(), "%" PRId64, &timestamp))
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

	//Pull out nanoseconds from the timestamp and convert to femtoseconds since that's the scopehal fine time unit
	const int64_t ns_per_sec = 1000000000;
	int64_t start_ns = timestamp % ns_per_sec;
	int64_t start_fs = 1000000 * start_ns;
	int64_t start_sec = (timestamp - start_ns) / ns_per_sec;
	time_t start_time = epoch_stamp + start_sec;

	//Figure out delta in trigger time between analog and digital channels
	int64_t trigger_phase = 0;
	if(analog_hoff != 0)
		trigger_phase = horstart - analog_hoff;

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
			auto cap = AllocateDigitalWaveform(m_nickname + "." + GetChannel(m_digitalChannelBase + i)->GetHwname());
			cap->m_timescale = interval;
			cap->PrepareForCpuAccess();

			//Capture timestamp
			cap->m_startTimestamp = start_time;
			cap->m_startFemtoseconds = start_fs;
			cap->m_triggerPhase = trigger_phase;

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
			cap->MarkSamplesModifiedFromCpu();
			cap->MarkTimestampsModifiedFromCpu();

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
	double* pwtime = nullptr;
	string digitalWaveformData;

	ChannelsDownloadStarted();

	//Acquire the data (but don't parse it)
	{
		lock_guard<recursive_mutex> lock2(m_transport->GetMutex());

		//Get the wavedescs for all channels
		unsigned int firstEnabledChannel = UINT_MAX;
		bool any_enabled = true;
		if(!ReadWavedescs(wavedescs, enabled, firstEnabledChannel, any_enabled))
			return false;

		//Grab the WAVEDESC from the first enabled channel
		unsigned char* pdesc = NULL;
		bool useClientsideTimestamp = false;
		for(unsigned int i=0; i<m_analogChannelCount; i++)
		{
			if(enabled[i] || (!any_enabled && i==0))
			{
				//If our FIRST enabled channel (the source of the timestamp) has averaging enabled,
				//for some reason timestamps don't work.
				//https://github.com/ngscopeclient/scopehal/issues/813
				//Unless LeCroy gets us a better workaround (TLC#00342021), just use clientside clock
				if(GetNumAverages(i) > 1)
					useClientsideTimestamp = true;

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
		m_transport->FlushCommandQueue();

		if(pdesc)
		{
			//Figure out when the first trigger happened.
			//Read the timestamps if we're doing segmented capture
			ttime = ExtractTimestamp(pdesc, basetime);
			if(num_sequences > 1)
			{
				wavetime = m_transport->ReadReply();
				pwtime = reinterpret_cast<double*>(&wavetime[16]);	//skip 16-byte SCPI header
			}

			//If instrument timestamp in the WAVEDESC is not valid, use our local clock instead
			if(useClientsideTimestamp)
			{
				double t = GetTime();
				ttime = floor(t);
				basetime = t - ttime;
			}

			//Read the data from each analog waveform
			for(unsigned int i=0; i<m_analogChannelCount; i++)
			{
				if(enabled[i])
				{
					analogWaveformData[i] = m_transport->ReadReply(
						false,
						[i, this] (float progress) { ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, progress); });
					ChannelsDownloadStatusUpdate(i, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
				}
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
		m_transport->SendCommandQueued("TRIG_MODE SINGLE");
		m_transport->FlushCommandQueue();
		m_triggerArmed = true;
	}

	//Offset from start of waveform to trigger
	double analog_hoff = 0;

	//Process analog waveforms
	vector< vector<WaveformBase*> > waveforms;
	waveforms.resize(m_analogChannelCount);
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		if(enabled[i])
		{
			//Extract timestamp of waveform
			auto pdesc = (unsigned char*)(&wavedescs[i][0]);
			//cppcheck-suppress invalidPointerCast
			analog_hoff = *reinterpret_cast<double*>(pdesc + 180) * FS_PER_SECOND;

			///Handle units
			auto pvunit = reinterpret_cast<const char*>(pdesc + 196);
			int pvlen = 1;
			while( (pvlen < 48) && (pvunit[pvlen] != '\0'))
				pvlen ++;
			string vunit(pvunit, pvlen);
			if(vunit == "V")
				m_channels[i]->SetYAxisUnits(Unit(Unit::UNIT_VOLTS), 0);
			else if(vunit == "A")
				m_channels[i]->SetYAxisUnits(Unit(Unit::UNIT_AMPS), 0);
			//else unknown unit, ignore for now

			waveforms[i] = ProcessAnalogWaveform(
				&analogWaveformData[i][16],			//skip 16-byte SCPI header DATA,\n#9xxxxxxxx
				analogWaveformData[i].size() - 17,	//skip header plus \n at end
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
		map<int, SparseDigitalWaveform*> digwaves = ProcessDigitalWaveform(digitalWaveformData, analog_hoff);

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
				s[GetOscilloscopeChannel(j)] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	double dt = GetTime() - start;
	LogTrace("Waveform download and processing took %.3f ms\n", dt * 1000);

	ChannelsDownloadFinished();

	return true;
}

void LeCroyOscilloscope::Start()
{
	m_transport->SendCommandQueued("TRIG_MODE SINGLE");	//always do single captures, just re-trigger
	m_transport->FlushCommandQueue();
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void LeCroyOscilloscope::StartSingleTrigger()
{
	m_transport->SendCommandQueued("TRIG_MODE SINGLE");
	m_transport->FlushCommandQueue();
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void LeCroyOscilloscope::Stop()
{
	if(!m_triggerArmed)
		return;

	m_transport->SendCommandQueued("TRIG_MODE STOP");
	m_transport->FlushCommandQueue();
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

void LeCroyOscilloscope::ForceTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;

	m_transport->SendCommandQueued("FRTR");
	m_transport->FlushCommandQueue();
}

float LeCroyOscilloscope::GetChannelOffset(size_t i, size_t /*stream*/)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelOffsets.find(i) != m_channelOffsets.end())
			return m_channelOffsets[i];
	}

	auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":OFFSET?");

	float offset;
	sscanf(reply.c_str(), "%f", &offset);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
	return offset;
}

void LeCroyOscilloscope::SetChannelOffset(size_t i, size_t /*stream*/, float offset)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return;

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%s:OFFSET %f", GetOscilloscopeChannel(i)->GetHwname().c_str(), offset);
	m_transport->SendCommandQueued(tmp);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffsets[i] = offset;
}

float LeCroyOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelVoltageRanges.find(i) != m_channelVoltageRanges.end())
			return m_channelVoltageRanges[i];
	}

	auto reply = m_transport->SendCommandQueuedWithReply(GetOscilloscopeChannel(i)->GetHwname() + ":VOLT_DIV?");

	double volts_per_div;
	sscanf(reply.c_str(), "%lf", &volts_per_div);

	double v = volts_per_div * 8;	//plot is 8 divisions high on all MAUI scopes
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRanges[i] = v;
	return v;
}

void LeCroyOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	double vdiv = range / 8;
	m_channelVoltageRanges[i] = range;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "%s:VOLT_DIV %.4f", GetOscilloscopeChannel(i)->GetHwname().c_str(), vdiv);
	m_transport->SendCommandQueued(cmd);
}

vector<uint64_t> LeCroyOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	const int64_t k = 1000;
	const int64_t m = k*k;
	const int64_t g = k*m;

	if(GetSamplingMode() == EQUIVALENT_TIME)
	{
		//RIS is 200 Gsps on all known scopes
		ret.push_back(200 * g);
	}

	else
	{
		//Not all scopes can go this slow
		//TODO: complete list
		if(m_modelid == MODEL_WAVERUNNER_8K)
			ret.push_back(1000);

		bool wm8 =
			(m_modelid == MODEL_WAVEMASTER_8ZI_B) ||
			(m_modelid == MODEL_SDA_8ZI) ||
			(m_modelid == MODEL_SDA_8ZI_A) ||
			(m_modelid == MODEL_SDA_8ZI_B);
		bool hdo9 = (m_modelid == MODEL_HDO_9K);
		bool wr8 = (m_modelid == MODEL_WAVERUNNER_8K);

		//WaveMaster 8Zi can't go below 200 ksps in realtime mode?
		if(!wm8)
		{
			ret.push_back(2 * k);
			ret.push_back(5 * k);
			ret.push_back(10 * k);
			ret.push_back(20 * k);
			ret.push_back(50 * k);
			ret.push_back(100 * k);
		}
		ret.push_back(200 * k);
		if(wm8)
			ret.push_back(250 * k);
		ret.push_back(500 * k);

		ret.push_back(1 * m);
		if(hdo9 || wm8 || wr8)
			ret.push_back(2500 * k);
		else
			ret.push_back(2 * m);
		ret.push_back(5 * m);
		ret.push_back(10 * m);
		if(wm8)
			ret.push_back(25 * m);
		else
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

			case MODEL_SDA_8ZI:
			case MODEL_SDA_8ZI_A:
			case MODEL_SDA_8ZI_B:
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
	ret.push_back(2500);
	ret.push_back(2 * k);
	ret.push_back(5 * k);
	ret.push_back(10 * k);

	if(GetSamplingMode() == EQUIVALENT_TIME)
		ret.push_back(20 * k);

	else
	{
		ret.push_back(25 * k);
		ret.push_back(20 * k);
		ret.push_back(40 * k);			//20/40 Gsps scopes can use values other than 1/2/5.
										//TODO: available rates seems to depend on the selected sample rate??
		ret.push_back(50 * k);
		ret.push_back(80 * k);
		ret.push_back(100 * k);
		ret.push_back(200 * k);
		ret.push_back(250 * k);
		ret.push_back(400 * k);
		ret.push_back(500 * k);

		ret.push_back(1 * m);
		ret.push_back(2500 * k);
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
			//TODO: extended options
			case MODEL_WAVEMASTER_8ZI_B:
				break;

			//extended memory
			case MODEL_SDA_8ZI:
			case MODEL_SDA_8ZI_A:
			case MODEL_SDA_8ZI_B:
				ret.insert(ret.begin()+4, 4*k);

				ret.push_back(20 * m);
				ret.push_back(25 * m);
				ret.push_back(40 * m);
				ret.push_back(50 * m);
				ret.push_back(80 * m);
				ret.push_back(100 * m);
				ret.push_back(128 * m);
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
				ret.push_back(12500 * k);
				ret.push_back(16 * m);
				ret.push_back(20 * m);
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
		case MODEL_SDA_8ZI:
		case MODEL_SDA_8ZI_A:
		case MODEL_WAVEMASTER_8ZI:
		case MODEL_WAVEMASTER_8ZI_A:
		//SDA/wavemaster 8Zi-B can interleave

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
	ret.emplace(InterleaveConflict(GetOscilloscopeChannel(0), GetOscilloscopeChannel(1)));
	if(m_analogChannelCount > 2)
		ret.emplace(InterleaveConflict(GetOscilloscopeChannel(2), GetOscilloscopeChannel(3)));

	switch(m_modelid)
	{
		//Any use of 1 or 4 disqualifies interleaving in these models
		//TODO: is this true of everything but the wavesurfer 3K? so far that seems to be the only exception
		case MODEL_HDO_9K:
		case MODEL_WAVERUNNER_8K:
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(0), GetOscilloscopeChannel(0)));
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(3), GetOscilloscopeChannel(3)));
			break;

		//SDA 8Zi and 8Zi-A cannot interleave without external interleaving adapter
		//(not currently supported)
		case MODEL_WAVEMASTER_8ZI:
		case MODEL_WAVEMASTER_8ZI_A:
		case MODEL_SDA_8ZI:
		case MODEL_SDA_8ZI_A:
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(0), GetOscilloscopeChannel(0)));
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(1), GetOscilloscopeChannel(1)));
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(2), GetOscilloscopeChannel(2)));
			ret.emplace(InterleaveConflict(GetOscilloscopeChannel(3), GetOscilloscopeChannel(3)));
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
		auto reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Horizontal.SamplingRate'");
		//What's the difference between SampleRate and SamplingRate?
		//Seems like at low speed we want to use SamplingRate, not SampleRate
		sscanf(reply.c_str(), "%" PRId64, &m_sampleRate);
		m_sampleRateValid = true;
	}

	return m_sampleRate;
}

uint64_t LeCroyOscilloscope::GetSampleDepth()
{
	if(!m_memoryDepthValid)
	{
		//MSIZ? can sometimes return incorrect values! It returns the *cap* on memory depth,
		//not the *actual* memory depth.
		//This is the same as app.Acquisition.Horizontal.MaxSamples, which is also wrong.

		//What you see below is the only observed method that seems to reliably get the *actual* memory depth.
		auto reply = m_transport->SendCommandQueuedWithReply(
			"VBS? 'return = app.Acquisition.Horizontal.AcquisitionDuration'");
		int64_t capture_len_fs = Unit(Unit::UNIT_FS).ParseString(reply);
		int64_t fs_per_sample = FS_PER_SECOND / GetSampleRate();

		m_memoryDepth = capture_len_fs / fs_per_sample;
		m_memoryDepthValid = true;
	}

	return m_memoryDepth;
}

void LeCroyOscilloscope::SetSampleDepth(uint64_t depth)
{
	//Calculate the record length we need for this memory depth
	int64_t fs_per_sample = FS_PER_SECOND / GetSampleRate();
	int64_t fs_per_acquisition = depth * fs_per_sample;
	float sec_per_acquisition = fs_per_acquisition * SECONDS_PER_FS;
	float sec_per_div = sec_per_acquisition / 10;

	m_transport->SendCommandQueued(
		string("VBS? 'app.Acquisition.Horizontal.HorScale = ") +
		to_string_sci(sec_per_div) + "'");

	//Sometimes the scope won't set the exact depth we ask for.
	//Flush the cache to force a read so we know the actual depth we got.
	m_memoryDepthValid = false;

	//Trigger position can also move when setting depth, so invalidate that too
	m_triggerOffsetValid = false;
}

void LeCroyOscilloscope::SetSampleRate(uint64_t rate)
{
	m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Horizontal.SampleRate = ") + to_string(rate) + "'");

	m_sampleRate = rate;
	m_sampleRateValid = true;
	m_triggerOffsetValid = false;
}

bool LeCroyOscilloscope::CanAverage(size_t i)
{
	return (i < m_analogChannelCount);
}

size_t LeCroyOscilloscope::GetNumAverages(size_t i)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return 1;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_channelNavg.find(i) != m_channelNavg.end())
			return m_channelNavg[i];
	}

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(
		string("VBS? 'return = app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() + ".AverageSweeps'"));
	auto navg = stoi(reply);

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelNavg[i] = navg;
	return navg;
}

void LeCroyOscilloscope::SetNumAverages(size_t i, size_t navg)
{
	//not meaningful for trigger or digital channels
	if(i > m_analogChannelCount)
		return;

	m_transport->SendCommandQueued(
		string("VBS? 'app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() + ".AverageSweeps = " +
		to_string(navg) + "'");

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelNavg[i] = navg;
}

void LeCroyOscilloscope::EnableTriggerOutput()
{
	//Enable 400ns trigger-out pulse, 1V p-p
	m_transport->SendCommandQueued("VBS? 'app.Acquisition.AuxOutput.AuxMode=\"TriggerOut\"'");
	m_transport->SendCommandQueued("VBS? 'app.Acquisition.AuxOutput.Amplitude=1'");

	//Pulse width setting is not supported on older scopes
	switch(m_modelid)
	{
		case MODEL_DDA_5K:
		case MODEL_SDA_3K:
		case MODEL_SDA_8ZI:
		case MODEL_SDA_8ZI_A:
		case MODEL_SDA_8ZI_B:	//TODO: unsure if B has this, A definitely does not
			break;

		default:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.AuxOutput.TrigOutPulseWidth=4e-7'");
			break;
	}
}

void LeCroyOscilloscope::SetUseExternalRefclk(bool external)
{
	if(external)
		m_transport->SendCommandQueued("RCLK EXTERNAL");
	else
		m_transport->SendCommandQueued("RCLK INTERNAL");
}

void LeCroyOscilloscope::SetTriggerOffset(int64_t offset)
{
	//LeCroy's standard has the offset being from the midpoint of the capture.
	//Scopehal has offset from the start.
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "TRDL %e", (offset - halfwidth) * SECONDS_PER_FS);
	m_transport->SendCommandQueued(tmp);

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

	auto reply = m_transport->SendCommandQueuedWithReply("TRDL?");

	lock_guard<recursive_mutex> lock(m_cacheMutex);

	//Result comes back in scientific notation
	double sec;
	sscanf(reply.c_str(), "%le", &sec);
	m_triggerOffset = static_cast<int64_t>(round(sec * FS_PER_SECOND));

	//Convert from midpoint to start point
	int64_t rate = GetSampleRate();
	int64_t halfdepth = GetSampleDepth() / 2;
	int64_t halfwidth = static_cast<int64_t>(round(FS_PER_SECOND * halfdepth / rate));
	m_triggerOffset += halfwidth;

	m_triggerOffsetValid = true;

	return m_triggerOffset;
}

void LeCroyOscilloscope::SetDeskewForChannel(size_t channel, int64_t skew)
{
	//Cannot deskew digital/trigger channels
	if(channel >= m_analogChannelCount)
		return;

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS? 'app.Acquisition.%s.Deskew=%e'",
		m_channels[channel]->GetHwname().c_str(),
		skew * SECONDS_PER_FS
		);
	m_transport->SendCommandQueued(tmp);

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
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "VBS? 'return = app.Acquisition.%s.Deskew'", m_channels[channel]->GetHwname().c_str());
	auto reply = m_transport->SendCommandQueuedWithReply(tmp);

	//Value comes back as floating point ps
	float skew;
	sscanf(reply.c_str(), "%f", &skew);
	int64_t skew_ps = round(skew * FS_PER_SECOND);

	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	m_channelDeskew[channel] = skew_ps;

	return skew_ps;
}

bool LeCroyOscilloscope::IsInterleaving()
{
	//interleaving is automatic / not possible
	if(m_modelid == MODEL_WAVESURFER_3K)
		return false;

	//Check cache
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if(m_interleavingValid)
			return m_interleaving;
	}

	auto reply = m_transport->SendCommandQueuedWithReply("COMBINE_CHANNELS?");
	if(reply[0] == '1')
		m_interleaving = false;
	else if(reply[0] == '2')
		m_interleaving = true;

	//We don't support "auto" mode. Default to off for now
	else
	{
		m_transport->SendCommandQueued("COMBINE_CHANNELS 1");
		m_interleaving = false;
	}

	m_interleavingValid = true;
	return m_interleaving;
}

bool LeCroyOscilloscope::SetInterleaving(bool combine)
{
	//interleaving is automatic / not possible
	if(m_modelid == MODEL_WAVESURFER_3K)
		return false;

	//Setting to "off" always is possible
	if(!combine)
	{
		m_transport->SendCommandQueued("COMBINE_CHANNELS 1");

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
		m_transport->SendCommandQueued("COMBINE_CHANNELS 2");

		lock_guard<recursive_mutex> lock2(m_cacheMutex);
		m_interleaving = true;
		m_interleavingValid = true;
	}

	return m_interleaving;
}

bool LeCroyOscilloscope::IsSamplingModeAvailable(SamplingMode mode)
{
	switch(mode)
	{
		case EQUIVALENT_TIME:

			//RIS mode is only available with <20K point memory depth
			if(GetSampleDepth() > 20000)
				return false;

			else
				return true;

		case REAL_TIME:
			return true;

		default:
			return false;
	}
}

LeCroyOscilloscope::SamplingMode LeCroyOscilloscope::GetSamplingMode()
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Horizontal.SampleMode'"));

	if(reply == "RealTime")
		return REAL_TIME;
	else if(reply == "RIS")
		return EQUIVALENT_TIME;

	//sequence mode is still real time
	else
		return REAL_TIME;
}

void LeCroyOscilloscope::SetSamplingMode(SamplingMode mode)
{
	//Get the old sampling mode
	//Only apply if different
	if(GetSamplingMode() == mode)
		return;

	//Send the command to the scope
	{
		switch(mode)
		{
			case REAL_TIME:

				//Select 10ns/div
				m_transport->SendCommandQueued(
					string("VBS? 'app.Acquisition.Horizontal.HorScale = ") + to_string_sci(1e-8) + "'");

				//Select sample mode after changing scale
				m_transport->SendCommandQueued("VBS? 'app.Acquisition.Horizontal.SampleMode = \"RealTime\"'");
				break;

			case EQUIVALENT_TIME:
				m_transport->SendCommandQueued("VBS? 'app.Acquisition.Horizontal.SampleMode = \"RIS\"'");
				break;
		}
	}

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_sampleRateValid = false;
	m_memoryDepthValid = false;
	m_interleaving = false;
	m_interleavingValid = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DBI mode

bool LeCroyOscilloscope::HasDBICapability()
{
	switch(m_modelid)
	{
		//Base 8Zi: DBI added past 16 GHz
		case MODEL_SDA_8ZI:
		case MODEL_WAVEMASTER_8ZI:
			return (m_maxBandwidth > 16000);

		//8Zi-A: DBI added for 25/30/45 GHz models
		case MODEL_SDA_8ZI_A:
		case MODEL_WAVEMASTER_8ZI_A:
			return (m_maxBandwidth > 20000);

		//8Zi-B: DBI added for 25/30 GHz models
		case MODEL_SDA_8ZI_B:
		case MODEL_WAVEMASTER_8ZI_B:
			return (m_maxBandwidth > 20000);

		//LabMaster: DBI added for >36 GHz models
		case MODEL_LABMASTER_ZI_A:
			return (m_maxBandwidth > 36000);

		//All other models lack DBI
		default:
			return false;
	}
}

bool LeCroyOscilloscope::IsDBIEnabled(size_t channel)
{
	if(!HasDBICapability())
		return false;

	//TODO: LabMaster scopes can have >4 channels. How do we figure out what acquisition modules are present?

	//Ask scope for the DBI mode
	//For now, no caching since we don't expect to be touching this too often.
	//We can add caching in the future if there's performance issues.
	string reply;
	if(channel == 1)
		reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.DBI2Mode'");
	else if(channel == 2)
		reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.DBI3Mode'");

	//Can only enable DBI on center two channels
	else
		return false;

	reply = Trim(reply);
	return (reply == "DBION");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Analog bank configuration

bool LeCroyOscilloscope::IsADCModeConfigurable()
{
	//HDO9000 is the only known LeCroy model with programmable ADC resolution
	return (m_modelid == MODEL_HDO_9K);
}

vector<string> LeCroyOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	vector<string> ret;
	ret.push_back("HD Off");
	ret.push_back("HD On");
	return ret;
}

size_t LeCroyOscilloscope::GetADCMode(size_t /*channel*/)
{
	if(m_modelid != MODEL_HDO_9K)
		return 0;

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Horizontal.HiResolutionModeActive'"));

	if(reply == "HDOn")
		return 1;
	else
		return 0;
}

void LeCroyOscilloscope::SetADCMode(size_t /*channel*/, size_t mode)
{
	if(m_modelid != MODEL_HDO_9K)
		return;

	if(mode == 1)
		m_transport->SendCommandQueued("VBS 'app.Acquisition.Horizontal.HiResolutionModeActive = \"HDOn\"'");
	else
	{
		m_transport->SendCommandQueued("VBS 'app.Acquisition.Horizontal.HiResolutionModeActive = \"HDOff\"'");

		//Disable all interpolation
		for(size_t i=0; i<m_analogChannelCount; i++)
		{
			m_transport->SendCommandQueued(string("VBS 'app.Acquisition.") + GetOscilloscopeChannel(i)->GetHwname() +
				".Interpolation = \"NONE\"'");
		}
	}
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
	if( (channel < m_digitalChannelBase) || (m_digitalChannelCount == 0) )
		return 0;

	string reply;
	if(channel <= m_digitalChannels[7]->GetIndex() )
		reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.LogicAnalyzer.MSxxHysteresis0'");
	else
		reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.LogicAnalyzer.MSxxHysteresis1'");

	return atof(reply.c_str());
}

float LeCroyOscilloscope::GetDigitalThreshold(size_t channel)
{
	if( (channel < m_digitalChannelBase) || (m_digitalChannelCount == 0) )
		return 0;

	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);

		if(m_channelDigitalThresholds.find(channel) != m_channelDigitalThresholds.end())
			return m_channelDigitalThresholds[channel];
	}

	string reply;
	if(channel <= m_digitalChannels[7]->GetIndex() )
		reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.LogicAnalyzer.MSxxThreshold0'");
	else
		reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.LogicAnalyzer.MSxxThreshold1'");

	float result = atof(reply.c_str());

	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelDigitalThresholds[channel] = result;
	return result;
}

void LeCroyOscilloscope::SetDigitalHysteresis(size_t channel, float level)
{
	if( (channel < m_digitalChannelBase) || (m_digitalChannelCount == 0) )
		return;

	char tmp[128];
	if(channel <= m_digitalChannels[7]->GetIndex() )
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxHysteresis0 = %e'", level);
	else
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxHysteresis1 = %e'", level);
	m_transport->SendCommandQueued(tmp);
}

void LeCroyOscilloscope::SetDigitalThreshold(size_t channel, float level)
{
	if(channel < m_digitalChannelBase)
		return;

	char tmp[128];
	if(channel <= m_digitalChannels[7]->GetIndex() )
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxThreshold0 = %e'", level);
	else
		snprintf(tmp, sizeof(tmp), "VBS? 'app.LogicAnalyzer.MSxxThreshold1 = %e'", level);
	m_transport->SendCommandQueued(tmp);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

void LeCroyOscilloscope::PullTrigger()
{
	//Figure out what kind of trigger is active.
	//Do case insensitive comparisons because older scopes (e.g. DDA 5000 series) return all caps
	//while more modern use
	auto reply = strtolower(Trim(
		m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Type'")));
	if (reply == "c8b10b")
		Pull8b10bTrigger();
	else if(reply == "c64b66b")
		Pull64b66bTrigger();
	else if(reply == "nrzpattern")
		PullNRZTrigger();
	else if (reply == "dropout")
		PullDropoutTrigger();
	else if (reply == "edge")
		PullEdgeTrigger();
	else if (reply == "glitch")
		PullGlitchTrigger();
	else if (reply == "runt")
		PullRuntTrigger();
	else if (reply == "slewrate")
		PullSlewRateTrigger();
	else if (reply == "uart")
		PullUartTrigger();
	else if (reply == "width")
		PullPulseWidthTrigger();
	else if (reply == "window")
		PullWindowTrigger();

	//Unrecognized trigger type
	else
	{
		LogWarning("Unknown trigger type \"%s\"\n", reply.c_str());
		m_trigger = nullptr;
	}

	if(!m_trigger)
		return;

	//Pull the source (same for all types of trigger)
	PullTriggerSource(m_trigger);

	//TODO: holdoff
}

/**
	@brief Reads the source of a trigger from the instrument
 */
void LeCroyOscilloscope::PullTriggerSource(Trigger* trig)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Source'"));		//not visible in XStream Browser?

	auto chan = GetOscilloscopeChannelByHwName(reply);
	trig->SetInput(0, StreamDescriptor(chan, 0), true);
	if(!chan)
		LogWarning("Unknown trigger source \"%s\"\n", reply.c_str());
}

/**
	@brief Reads settings for an NRZ pattern trigger from the instrument
 */
void LeCroyOscilloscope::PullNRZTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != nullptr) && (dynamic_cast<CDRNRZPatternTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	auto trig = dynamic_cast<CDRNRZPatternTrigger*>(m_trigger);
	if(trig == nullptr)
	{
		trig = new CDRNRZPatternTrigger(this);
		m_trigger = trig;

		trig->signal_calculateBitRate().connect(sigc::mem_fun(*this, &LeCroyOscilloscope::OnCDRTriggerAutoBaud));
	}

	LogWarning("LeCroyOscilloscope::PullNRZTrigger unimplemented\n");
}

/**
	@brief Reads settings for a 64b66b pattern trigger from the instrument
 */
void LeCroyOscilloscope::Pull64b66bTrigger()
{
	/*
	//Clear out any triggers of the wrong type
	if( (m_trigger != nullptr) && (dynamic_cast<CDRNRZPatternTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	auto trig = dynamic_cast<CDRNRZPatternTrigger*>(m_trigger);
	if(trig == nullptr)
	{
		trig = new CDRNRZPatternTrigger(this);
		m_trigger = trig;

		trig->signal_calculateBitRate().connect(sigc::mem_fun(*this, &LeCroyOscilloscope::OnCDRTriggerAutoBaud));
	}

	//LogWarning("LeCroyOscilloscope::PullNRZTrigger unimplemented\n");
	*/
	m_trigger = nullptr;
}

/**
	@brief Reads settings for an 8B/10B trigger from the instrument
 */
void LeCroyOscilloscope::Pull8b10bTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != nullptr) && (dynamic_cast<CDR8B10BTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	auto trig = dynamic_cast<CDR8B10BTrigger*>(m_trigger);
	if(trig == nullptr)
	{
		trig = new CDR8B10BTrigger(this);
		m_trigger = trig;

		trig->signal_calculateBitRate().connect(sigc::mem_fun(*this, &LeCroyOscilloscope::OnCDRTriggerAutoBaud));
	}

	//Get the baud rate
	Unit bps(Unit::UNIT_BITRATE);
	auto reply = m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.BitRate'");
	trig->SetBitRate(bps.ParseString(reply));

	//Grab the equalizer mode (None, Low=2, Medium=5, High=9 dB)
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.EqualizerMode'"));
	if(reply == "None")
		trig->SetEqualizerMode(CDRTrigger::LECROY_EQ_NONE);
	else if(reply == "Low")
		trig->SetEqualizerMode(CDRTrigger::LECROY_EQ_LOW);
	else if(reply == "Medium")
		trig->SetEqualizerMode(CDRTrigger::LECROY_EQ_MEDIUM);
	else// if(reply == "High")
		trig->SetEqualizerMode(CDRTrigger::LECROY_EQ_HIGH);

	//Grab the trigger position (Start, End)
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.TriggerPosition'"));
	if(reply == "Start")
		trig->SetTriggerPosition(CDRTrigger::POSITION_START);
	else //if(reply == "End")
		trig->SetTriggerPosition(CDRTrigger::POSITION_END);

	//Grab the polarity (true, false)
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.Invert'"));
	if(reply == "0")
		trig->SetPolarity(CDRTrigger::POLARITY_NORMAL);
	else //if(reply == "1")
		trig->SetPolarity(CDRTrigger::POLARITY_INVERTED);

	//Grab include/exclude mode
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.Operation'"));
	if(reply ==  "Include")
		trig->SetMatchMode(CDR8B10BTrigger::MATCH_INCLUDE);
	else //if(reply ==  "Exclude")
		trig->SetMatchMode(CDR8B10BTrigger::MATCH_EXCLUDE);

	//Pattern type (can also be Primitive, ProtocolError but we don't implement this yet)
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.PatternType'"));
	CDR8B10BTrigger::PatternMode mode = CDR8B10BTrigger::PATTERN_LIST;
	if(reply == "SymbolOR")
		mode = CDR8B10BTrigger::PATTERN_LIST;
	else //if(reply == "SymbolString")
		mode = CDR8B10BTrigger::PATTERN_SEQUENCE;
	trig->SetPatternMode(mode);

	//Mode-specific stuff
	auto pattern = trig->GetPattern();
	switch(mode)
	{
		case CDR8B10BTrigger::PATTERN_LIST:
			{
				//Symbol count is only used in pattern list mode
				reply = Trim(m_transport->SendCommandQueuedWithReply(
					"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.SymbolCount'"));
				size_t patternlen = stoi(reply);
				trig->SetSymbolCount(patternlen);
				pattern.resize(8);

				for(size_t i=0; i<patternlen; i++)
				{
					auto si = to_string(i);
					reply = Trim(m_transport->SendCommandQueuedWithReply(
						string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.ORSymbol") + si + "Rd'"));
					if(reply == "Either")
						pattern[i].disparity = T8B10BSymbol::ANY;
					else if(reply == "Positive")
						pattern[i].disparity = T8B10BSymbol::POSITIVE;
					else// if(reply == "Negative")
						pattern[i].disparity = T8B10BSymbol::NEGATIVE;

					reply = Trim(m_transport->SendCommandQueuedWithReply(
						string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.ORSymbol") + si + "Type'"));
					if(reply == "KSymbol")
						pattern[i].ktype = T8B10BSymbol::KSYMBOL;
					else //if(reply == "DSymbol")
						pattern[i].ktype = T8B10BSymbol::DSYMBOL;
					//DontCare type is not valid for pattern list mode, only sequence

					if(pattern[i].ktype == T8B10BSymbol::KSYMBOL)
					{
						reply = Trim(m_transport->SendCommandQueuedWithReply(
							string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.KSymbol") + si + "ValueOR'"));
					}
					else
					{
						reply = Trim(m_transport->SendCommandQueuedWithReply(
							string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.DSymbol") + si + "EightBitsValueOR'"));
					}

					char unused;
					int code5;
					int code3;
					if(3 != sscanf(reply.c_str(), "%c%d.%d", &unused, &code5, &code3))
						continue;

					pattern[i].value = (code3 << 5) | code5;
				}
			}
			break;

		case CDR8B10BTrigger::PATTERN_SEQUENCE:
			{
				//Sequence is always 8 elements long, but we don't want to actually show all 8 elements
				//if the last N are dontcares. Start by making it 8 elements long, then remove all dontcares from the end
				size_t iLastValidSymbol = 0;
				pattern.resize(8);
				for(size_t i=0; i<8; i++)
				{
					auto si = to_string(i);
					reply = Trim(m_transport->SendCommandQueuedWithReply(
						string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") + si + "Rd'"));
					if(reply == "Either")
						pattern[i].disparity = T8B10BSymbol::ANY;
					else if(reply == "Positive")
						pattern[i].disparity = T8B10BSymbol::POSITIVE;
					else// if(reply == "Negative")
						pattern[i].disparity = T8B10BSymbol::NEGATIVE;

					reply = Trim(m_transport->SendCommandQueuedWithReply(
						string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") + si + "Type'"));
					if(reply == "KSymbol")
					{
						pattern[i].ktype = T8B10BSymbol::KSYMBOL;
						iLastValidSymbol = i;
					}
					else if(reply == "DSymbol")
					{
						pattern[i].ktype = T8B10BSymbol::DSYMBOL;
						iLastValidSymbol = i;
					}
					else// if(reply == "DontCare")
						pattern[i].ktype = T8B10BSymbol::DONTCARE;

					if(pattern[i].ktype == T8B10BSymbol::KSYMBOL)
					{
						reply = Trim(m_transport->SendCommandQueuedWithReply(
							string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.StrKSymbol") + si + "Value'"));
					}
					else
					{
						reply = Trim(m_transport->SendCommandQueuedWithReply(
							string("VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.StrDSymbol") + si + "EightBitsValue'"));
					}

					char unused;
					int code5;
					int code3;
					if(3 != sscanf(reply.c_str(), "%c%d.%d", &unused, &code5, &code3))
						continue;

					pattern[i].value = (code3 << 5) | code5;
				}
				auto nsym = iLastValidSymbol + 1;
				pattern.resize(nsym);
				trig->SetSymbolCount(nsym);
			}
			break;

		default:
			break;
	}
	trig->SetPattern(pattern);
}

/**
	@brief Automatic baud rate configuration
 */
void LeCroyOscilloscope::OnCDRTriggerAutoBaud()
{
	auto trig = dynamic_cast<CDR8B10BTrigger*>(m_trigger);
	if(trig == nullptr)
		return;

	//Request autobaud
	m_transport->SendCommandQueued(
		"VBS? 'app.Acquisition.Trigger.Serial.C8B10B.ComputeBitRate'");

	//Get the new baud rate
	Unit bps(Unit::UNIT_BITRATE);
	auto reply = m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.BitRate'");
	trig->SetBitRate(bps.ParseString(reply));
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
	auto tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Dropout.Level'");
	dt->SetLevel(stof(tmp));

	//Dropout time
	Unit fs(Unit::UNIT_FS);
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Dropout.DropoutTime'");
	dt->SetDropoutTime(fs.ParseString(tmp));

	//Edge type
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Dropout.Slope'");
	if(Trim(tmp) == "Positive")
		dt->SetType(DropoutTrigger::EDGE_RISING);
	else
		dt->SetType(DropoutTrigger::EDGE_FALLING);

	//Reset type
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Dropout.IgnoreLastEdge'");
	if(Trim(tmp) == "0")
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
	string tmp;
	if(m_modelid == MODEL_DDA_5K)
		tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.TrigLevel'");
	else
		tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Edge.Level'");
	et->SetLevel(stof(tmp));

	//TODO: OptimizeForHF (changes hysteresis for fast signals)

	//Slope
	if(m_modelid == MODEL_DDA_5K)
	{
		//Get trigger source
		auto src = Trim(m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Source'"));
		tmp = m_transport->SendCommandQueuedWithReply(
			string("VBS? 'return = app.Acquisition.Trigger.") + src + ".Slope'");
	}
	else
		tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Edge.Slope'");

	GetTriggerSlope(et, Trim(tmp));
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
	auto tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Glitch.Level'");
	gt->SetLevel(stof(tmp));

	//Slope
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Glitch.Slope'");
	GetTriggerSlope(gt, Trim(tmp));

	//Condition
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Glitch.Condition'");
	gt->SetCondition(GetCondition(tmp));

	//Min range
	Unit fs(Unit::UNIT_FS);
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Glitch.TimeLow'");
	gt->SetLowerBound(fs.ParseString(tmp));

	//Max range
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Glitch.TimeHigh'");
	gt->SetUpperBound(fs.ParseString(tmp));
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
	auto tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Width.Level'");
	pt->SetLevel(stof(tmp));

	//Condition
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Width.Condition'");
	pt->SetCondition(GetCondition(tmp));

	//Min range
	Unit fs(Unit::UNIT_FS);
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Width.TimeLow'");
	pt->SetLowerBound(fs.ParseString(tmp));

	//Max range
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Width.TimeHigh'");
	pt->SetUpperBound(fs.ParseString(tmp));

	//Slope
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Width.Slope'");
	GetTriggerSlope(pt, Trim(tmp));
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
	auto tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Runt.LowerLevel'");
	rt->SetLowerBound(v.ParseString(tmp));

	//Upper bound
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Runt.UpperLevel'");
	rt->SetUpperBound(v.ParseString(tmp));

	//Lower interval
	Unit fs(Unit::UNIT_FS);
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Runt.TimeLow'");
	rt->SetLowerInterval(fs.ParseString(tmp));

	//Upper interval
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Runt.TimeHigh'");
	rt->SetUpperInterval(fs.ParseString(tmp));

	//Slope
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Runt.Slope'");
	tmp = Trim(tmp);
	if(tmp == "Positive")
		rt->SetSlope(RuntTrigger::EDGE_RISING);
	else if(tmp == "Negative")
		rt->SetSlope(RuntTrigger::EDGE_FALLING);

	//Condition
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Runt.Condition'");
	rt->SetCondition(GetCondition(tmp));
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
	auto tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.SlewRate.LowerLevel'");
	st->SetLowerBound(v.ParseString(tmp));

	//Upper bound
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.SlewRate.UpperLevel'");
	st->SetUpperBound(v.ParseString(tmp));

	//Lower interval
	Unit fs(Unit::UNIT_FS);
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.SlewRate.TimeLow'");
	st->SetLowerInterval(fs.ParseString(tmp));

	//Upper interval
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.SlewRate.TimeHigh'");
	st->SetUpperInterval(fs.ParseString(tmp));

	//Slope
	tmp = Trim(m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.SlewRate.Slope'"));
	if(tmp == "Positive")
		st->SetSlope(SlewRateTrigger::EDGE_RISING);
	else if(tmp == "Negative")
		st->SetSlope(SlewRateTrigger::EDGE_FALLING);

	//Condition
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.SlewRate.Condition'");
	st->SetCondition(GetCondition(tmp));
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
	auto reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Serial.UART.BitRate'");
	ut->SetBitRate(stoi(reply));

	//Level
	reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Serial.LevelAbsolute'");
	ut->SetLevel(stof(reply));

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
		* PatternPosition
		* RS232Mode (how is this different from polarity inversion?)
		* SupportsDigital
		* UARTCondition
		* ViewingMode
	*/

	//Parity
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.UART.ParityType'"));
	if(reply == "None")
		ut->SetParityType(UartTrigger::PARITY_NONE);
	else if(reply == "Even")
		ut->SetParityType(UartTrigger::PARITY_EVEN);
	else if(reply == "Odd")
		ut->SetParityType(UartTrigger::PARITY_ODD);

	//Operator
	bool ignore_p2 = true;
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.UART.PatternOperator'"));
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
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.UART.Polarity'"));
	if(reply == "IdleHigh")
		ut->SetPolarity(UartTrigger::IDLE_HIGH);
	else if(reply == "IdleLow")
		ut->SetPolarity(UartTrigger::IDLE_LOW);

	//Stop bits
	reply = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Serial.UART.StopBitLength'");
	ut->SetStopBits(stof(Trim(reply)));

	//Trigger type
	reply = Trim(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.UART.TrigOnBadParity'"));
	if(reply == "-1")
		ut->SetMatchType(UartTrigger::TYPE_PARITY_ERR);
	else
		ut->SetMatchType(UartTrigger::TYPE_DATA);

	//Pattern length
	auto len = stoi(m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Serial.UART.PatternLength'"));

	//Do not attempt to pull trigger config if it's zero length
	//The scope won't reply
	if(len > 0)
	{
		//PatternValue1 / 2
		auto p1 = Trim(m_transport->SendCommandQueuedWithReply(
			"VBS? 'return = app.Acquisition.Trigger.Serial.UART.PatternValue'"));
		auto p2 = Trim(m_transport->SendCommandQueuedWithReply(
			"VBS? 'return = app.Acquisition.Trigger.Serial.UART.PatternValue2'"));
		ut->SetPatterns(p1, p2, ignore_p2);
	}
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
	auto tmp = m_transport->SendCommandQueuedWithReply(
		"VBS? 'return = app.Acquisition.Trigger.Window.LowerLevel'");
	wt->SetLowerBound(v.ParseString(tmp));

	//Upper bound
	tmp = m_transport->SendCommandQueuedWithReply("VBS? 'return = app.Acquisition.Trigger.Window.UpperLevel'");
	wt->SetUpperBound(v.ParseString(tmp));
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
	if(!m_trigger->GetInput(0))
	{
		LogWarning("LeCroyOscilloscope::PushTrigger: no input specified\n");
		return;
	}

	//Source is the same for every channel
	char tmp[128];
	snprintf(
		tmp,
		sizeof(tmp),
		"VBS? 'app.Acquisition.Trigger.Source = \"%s\"'",
		m_trigger->GetInput(0).m_channel->GetHwname().c_str());
	m_transport->SendCommandQueued(tmp);

	//The rest depends on the type
	auto c8t = dynamic_cast<CDR8B10BTrigger*>(m_trigger);
	auto cnt = dynamic_cast<CDRNRZPatternTrigger*>(m_trigger);
	auto dt = dynamic_cast<DropoutTrigger*>(m_trigger);
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	auto gt = dynamic_cast<GlitchTrigger*>(m_trigger);
	auto pt = dynamic_cast<PulseWidthTrigger*>(m_trigger);
	auto rt = dynamic_cast<RuntTrigger*>(m_trigger);
	auto st = dynamic_cast<SlewRateTrigger*>(m_trigger);
	auto ut = dynamic_cast<UartTrigger*>(m_trigger);
	auto wt = dynamic_cast<WindowTrigger*>(m_trigger);
	if(c8t)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"C8B10B\"");
		Push8b10bTrigger(c8t);
	}
	else if(cnt)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"NRZPattern\"");
		PushNRZTrigger(cnt);
	}
	else if(dt)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"Dropout\"");
		PushDropoutTrigger(dt);
	}
	else if(pt)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"Width\"");
		PushPulseWidthTrigger(pt);
	}
	else if(gt)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"Glitch\"");
		PushGlitchTrigger(gt);
	}
	else if(rt)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"Runt\"");
		PushRuntTrigger(rt);
	}
	else if(st)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"SlewRate\"");
		PushSlewRateTrigger(st);
	}
	else if(ut)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"UART\"");
		PushUartTrigger(ut);
	}
	else if(wt)
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"Window\"");
		PushWindowTrigger(wt);
	}
	else if(et)	//must be last
	{
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Type = \"Edge\"");
		PushEdgeTrigger(et, "app.Acquisition.Trigger.Edge");
	}

	else
		LogWarning("Unknown trigger type (not an edge)\n");

	m_transport->FlushCommandQueue();
}

/**
	@brief Pushes settings for a NRZ pattern trigger to the instrument
 */
void LeCroyOscilloscope::PushNRZTrigger(CDRNRZPatternTrigger* trig)
{
	//FIXME
	LogWarning("LeCroyOscilloscope::PushNRZTrigger unimplemented\n");
}

/**
	@brief Pushes settings for an 8B/10B trigger to the instrument
 */
void LeCroyOscilloscope::Push8b10bTrigger(CDR8B10BTrigger* trig)
{
	PushFloat("app.Acquisition.Trigger.Serial.Level", trig->GetLevel());

	//Bit rate
	m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.BitRate = ") +
		to_string(trig->GetBitRate()) + "'");

	//Equalizer mode
	switch(trig->GetEqualizerMode())
	{
		case CDRTrigger::LECROY_EQ_NONE:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.EqualizerMode = \"None\"");
			break;

		case CDRTrigger::LECROY_EQ_LOW:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.EqualizerMode = \"Low\"");
			break;

		case CDRTrigger::LECROY_EQ_MEDIUM:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.EqualizerMode = \"Medium\"");
			break;

		case CDRTrigger::LECROY_EQ_HIGH:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.EqualizerMode = \"High\"");
			break;
	}

	//Trigger position
	switch(trig->GetTriggerPosition())
	{
		case CDRTrigger::POSITION_START:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.TriggerPosition = \"Start\"");
			break;

		case CDRTrigger::POSITION_END:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.TriggerPosition = \"End\"");
			break;
	}

	//Inversion
	switch(trig->GetPolarity())
	{
		case CDRTrigger::POLARITY_NORMAL:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.Invert = \"0\"");
			break;

		case CDRTrigger::POLARITY_INVERTED:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.Invert = \"1\"");
			break;
	}

	//Include/exclude mode
	switch(trig->GetMatchMode())
	{
		case CDR8B10BTrigger::MATCH_INCLUDE:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.Operation = \"Include\"");
			break;

		case CDR8B10BTrigger::MATCH_EXCLUDE:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.Operation = \"Exclude\"");
			break;
	}

	//Figure out actual pattern length
	//(early on during creation we may not be fully populated)
	auto pattern = trig->GetPattern();
	size_t nsymbols = trig->GetSymbolCount();
	if(nsymbols > pattern.size())
	{
		pattern.resize(nsymbols);
		trig->SetPattern(pattern);
	}

	//Pattern mode
	//TODO: Primitive, ProtocolError
	switch(trig->GetPatternMode())
	{
		case CDR8B10BTrigger::PATTERN_LIST:
			{
				m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.PatternType = \"SymbolOR\"");

				m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.SymbolCount = ") +
					to_string(nsymbols) + "'");

				for(size_t i=0; i<nsymbols; i++)
				{
					auto si = to_string(i);

					int code5 = pattern[i].value & 0x1f;
					int code3 = pattern[i].value >> 5;
					string val = to_string(code5) + "." + to_string(code3);

					switch(pattern[i].disparity)
					{
						case T8B10BSymbol::ANY:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.ORSymbol") +
								si + "Rd = \"Either\"'");
							break;

						case T8B10BSymbol::POSITIVE:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.ORSymbol") +
								si + "Rd = \"Positive\"'");
							break;

						case T8B10BSymbol::NEGATIVE:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.ORSymbol") +
								si + "Rd = \"Negative\"'");
							break;
					}

					switch(pattern[i].ktype)
					{
						case T8B10BSymbol::KSYMBOL:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.ORSymbol") +
								si + "Type = \"KSymbol\"'");

							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.KSymbol") +
								si + "ValueOR = \"K" + val + "\"'");
							break;

						case T8B10BSymbol::DSYMBOL:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.ORSymbol") +
								si + "Type = \"DSymbol\"'");
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.DSymbol") +
								si + "EightBitsValueOR = \"D" + val + "\"'");
							break;

						//DONTCARE is not legal for list mode
						default:
							break;
					}
				}
			}
			break;

		case CDR8B10BTrigger::PATTERN_SEQUENCE:
			{
				m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.PatternType = \"SymbolString\"");

				//Sequence is always 8 elements long, but we may not actually have all 8 elements internally
				//Only push the ones we have in our parameter
				for(size_t i=0; i<nsymbols; i++)
				{
					auto si = to_string(i);

					int code5 = pattern[i].value & 0x1f;
					int code3 = pattern[i].value >> 5;
					string val = to_string(code5) + "." + to_string(code3);

					switch(pattern[i].disparity)
					{
						case T8B10BSymbol::ANY:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") +
								si + "Rd = \"Either\"'");
							break;

						case T8B10BSymbol::POSITIVE:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") +
								si + "Rd = \"Positive\"'");
							break;

						case T8B10BSymbol::NEGATIVE:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") +
								si + "Rd = \"Negative\"'");
							break;
					}

					switch(pattern[i].ktype)
					{
						case T8B10BSymbol::KSYMBOL:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") +
								si + "Type = \"KSymbol\"'");
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrKSymbol") +
								si + "Value = \"K" + val + "\"'");
							break;

						case T8B10BSymbol::DSYMBOL:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") +
								si + "Type = \"DSymbol\"'");
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrDSymbol") +
								si + "EightBitsValue = \"D" + val + "\"'");
							break;

						case T8B10BSymbol::DONTCARE:
						default:
							m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") +
								si + "Type = \"DontCare\"'");
							break;
					}
				}

				//Pad out extra space with dontcares
				for(size_t i=nsymbols; i<8; i++)
				{
					m_transport->SendCommandQueued(string("VBS? 'app.Acquisition.Trigger.Serial.C8B10B.StrSymbol") +
						to_string(i) + "Type = \"DontCare\"'");
				}

			}
			break;
	}
}

/**
	@brief Pushes settings for a dropout trigger to the instrument
 */
void LeCroyOscilloscope::PushDropoutTrigger(DropoutTrigger* trig)
{
	PushFloat("app.Acquisition.Trigger.Dropout.Level", trig->GetLevel());
	PushFloat("app.Acquisition.Trigger.Dropout.DropoutTime", trig->GetDropoutTime() * SECONDS_PER_FS);

	if(trig->GetResetType() == DropoutTrigger::RESET_OPPOSITE)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Dropout.IgnoreLastEdge = 0'");
	else
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Dropout.IgnoreLastEdge = -1'");

	if(trig->GetType() == DropoutTrigger::EDGE_RISING)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Dropout.Slope = \"Positive\"'");
	else
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Dropout.Slope = \"Negative\"'");
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 */
void LeCroyOscilloscope::PushEdgeTrigger(EdgeTrigger* trig, const string& tree)
{
	//Level
	if(m_modelid == MODEL_DDA_5K)
		PushFloat("app.Acquisition.Trigger.TrigLevel", trig->GetLevel());
	else
		PushFloat(tree + ".Level", trig->GetLevel());

	//Slope
	string slope = "Positive";
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			slope = "Positive";
			break;

		case EdgeTrigger::EDGE_FALLING:
			slope = "Negative";
			break;

		case EdgeTrigger::EDGE_ANY:
			slope = "Either";
			break;

		default:
			LogWarning("Invalid trigger type %d\n", trig->GetType());
			return;
	}

	if(m_modelid == MODEL_DDA_5K)
	{
		auto src = m_trigger->GetInput(0).m_channel->GetHwname();
		m_transport->SendCommandQueued(
			string("VBS? 'app.Acquisition.trigger.") + src + ".Slope = \"" + slope + "\"'");
	}
	else
		m_transport->SendCommandQueued(string("VBS? '") + tree + ".Slope = \"" + slope + "\"'");
}

/**
	@brief Pushes settings for a pulse width trigger to the instrument
 */
void LeCroyOscilloscope::PushPulseWidthTrigger(PulseWidthTrigger* trig)
{
	PushEdgeTrigger(trig, "app.Acquisition.Trigger.Width");
	PushCondition("app.Acquisition.Trigger.Width.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.Width.TimeHigh", trig->GetUpperBound() * SECONDS_PER_FS);
	PushFloat("app.Acquisition.Trigger.Width.TimeLow", trig->GetLowerBound() * SECONDS_PER_FS);
}

/**
	@brief Pushes settings for a glitch trigger to the instrument
 */
void LeCroyOscilloscope::PushGlitchTrigger(GlitchTrigger* trig)
{
	PushEdgeTrigger(trig, "app.Acquisition.Trigger.Glitch");
	PushCondition("app.Acquisition.Trigger.Glitch.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.Glitch.TimeHigh", trig->GetUpperBound() * SECONDS_PER_FS);
	PushFloat("app.Acquisition.Trigger.Glitch.TimeLow", trig->GetLowerBound() * SECONDS_PER_FS);
}

/**
	@brief Pushes settings for a runt trigger to the instrument
 */
void LeCroyOscilloscope::PushRuntTrigger(RuntTrigger* trig)
{
	PushCondition("app.Acquisition.Trigger.Runt.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.Runt.TimeHigh", trig->GetUpperInterval() * SECONDS_PER_FS);
	PushFloat("app.Acquisition.Trigger.Runt.TimeLow", trig->GetLowerInterval() * SECONDS_PER_FS);
	m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Runt.Mode = \"Absolute\"");
	PushFloat("app.Acquisition.Trigger.Runt.UpperLevel", trig->GetUpperBound());
	PushFloat("app.Acquisition.Trigger.Runt.LowerLevel", trig->GetLowerBound());

	if(trig->GetSlope() == RuntTrigger::EDGE_RISING)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Runt.Slope = \"Positive\"");
	else
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Runt.Slope = \"Negative\"");
}

/**
	@brief Pushes settings for a slew rate trigger to the instrument
 */
void LeCroyOscilloscope::PushSlewRateTrigger(SlewRateTrigger* trig)
{
	PushCondition("app.Acquisition.Trigger.SlewRate.Condition", trig->GetCondition());
	PushFloat("app.Acquisition.Trigger.SlewRate.TimeHigh", trig->GetUpperInterval() * SECONDS_PER_FS);
	PushFloat("app.Acquisition.Trigger.SlewRate.TimeLow", trig->GetLowerInterval() * SECONDS_PER_FS);
	m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.SlewRate.Mode = \"Absolute\"");
	PushFloat("app.Acquisition.Trigger.SlewRate.UpperLevel", trig->GetUpperBound());
	PushFloat("app.Acquisition.Trigger.SlewRate.LowerLevel", trig->GetLowerBound());

	if(trig->GetSlope() == SlewRateTrigger::EDGE_RISING)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.SlewRate.Slope = \"Positive\"");
	else
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.SlewRate.Slope = \"Negative\"");
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
	m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.ByteBitOrder = \"LSB\"");
	//DataBytesLenValue1
	//DataBytesLenValue2
	//DataCondition
	//FrameDelimiter
	//InterframeMinBits
	//NeedDualLevels
	//NeededSources
	m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.NumDataBits = \"8\"");

	switch(trig->GetParityType())
	{
		case UartTrigger::PARITY_NONE:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.ParityType = \"None\"");
			break;

		case UartTrigger::PARITY_ODD:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.ParityType = \"Odd\"");
			break;

		case UartTrigger::PARITY_EVEN:
			m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.ParityType = \"Even\"");
			break;

		case UartTrigger::PARITY_MARK:
		case UartTrigger::PARITY_SPACE:
			LogWarning("LeCroy UART trigger does not support mark or space parity\n");
			break;
	}

	//Pattern length depends on the current format.
	//Note that the pattern length is in bytes, not bits, even though patterns are in binary.
	auto pattern1 = trig->GetPattern1();
	char tmp[256];
	snprintf(tmp, sizeof(tmp),
		"VBS? 'app.Acquisition.Trigger.Serial.UART.PatternLength = \"%d\"",
		(int)pattern1.length() / 8);
	m_transport->SendCommandQueued(tmp);

	PushPatternCondition("app.Acquisition.Trigger.Serial.UART.PatternOperator", trig->GetCondition());

	//PatternPosition

	m_transport->SendCommandQueued(
		string("VBS? 'app.Acquisition.Trigger.Serial.UART.PatternValue = \"") + pattern1 + " \"'");

	//PatternValue2 only for Between/NotBetween
	switch(trig->GetCondition())
	{
		case Trigger::CONDITION_BETWEEN:
		case Trigger::CONDITION_NOT_BETWEEN:
			m_transport->SendCommandQueued(
				string("VBS? 'app.Acquisition.Trigger.Serial.UART.PatternValue2 = \"") + trig->GetPattern2() + " \"'");
			break;

		default:
			break;
	}

	//Polarity
	if(trig->GetPolarity() == UartTrigger::IDLE_HIGH)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.Polarity = \"IdleHigh\"");
	else
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.Polarity = \"IdleLow\"");

	m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.RS232Mode = \"0\" ");

	auto nstop = trig->GetStopBits();
	if(nstop == 1)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.StopBitLength = \"1bit\"");
	else if(nstop == 2)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.StopBitLength = \"2bits\"");
	else
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.StopBitLength = \"1.5bit\"");

	//Match type
	if(trig->GetMatchType() == UartTrigger::TYPE_DATA)
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.TrigOnBadParity = \"0\"");
	else
		m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Serial.UART.TrigOnBadParity = \"-1\"");

	//UARTCondition
	//ViewingMode
}

/**
	@brief Pushes settings for a window trigger to the instrument
 */
void LeCroyOscilloscope::PushWindowTrigger(WindowTrigger* trig)
{
	m_transport->SendCommandQueued("VBS? 'app.Acquisition.Trigger.Window.Mode = \"Absolute\"");
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
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"LessThan\"'");
			break;

		case Trigger::CONDITION_GREATER:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"GreaterThan\"'");
			break;

		case Trigger::CONDITION_BETWEEN:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"InRange\"'");
			break;

		case Trigger::CONDITION_NOT_BETWEEN:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"OutOfRange\"'");
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
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"Equal\"'");
			break;

		case Trigger::CONDITION_NOT_EQUAL:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"NotEqual\"'");
			break;

		case Trigger::CONDITION_LESS:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"Smaller\"'");
			break;

		case Trigger::CONDITION_LESS_OR_EQUAL:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"SmallerOrEqual\"'");
			break;

		case Trigger::CONDITION_GREATER:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"Greater\"'");
			break;

		case Trigger::CONDITION_GREATER_OR_EQUAL:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"GreaterOrEqual\"'");
			break;

		case Trigger::CONDITION_BETWEEN:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"InRange\"'");
			break;

		case Trigger::CONDITION_NOT_BETWEEN:
			m_transport->SendCommandQueued(string("VBS? '") + path + " = \"OutRange\"'");
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
	m_transport->SendCommandQueued(tmp);
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
	if(m_has8b10bTrigger)
		ret.push_back(CDR8B10BTrigger::GetTriggerName());
	if(m_hasNrzTrigger)
		ret.push_back(CDRNRZPatternTrigger::GetTriggerName());
	ret.push_back(WindowTrigger::GetTriggerName());

	//TODO m_hasI2cTrigger m_hasSpiTrigger m_hasUartTrigger
	return ret;
}

/**
	@brief Checks if the hardware CDR trigger is locked
 */
bool LeCroyOscilloscope::IsCDRLocked()
{
	auto trig8b10b = dynamic_cast<CDR8B10BTrigger*>(m_trigger);
	if(trig8b10b)
	{
		//Undocumented / hidden property not visible in XStream Browser
		//See TLC#00306073
		auto tmp = Trim(m_transport->SendCommandQueuedWithReply(
			"VBS? 'return = app.Acquisition.Trigger.Serial.C8B10B.PLLUnLocked'"));

		return (tmp == "0");
	}

	//TODO: add other triggers here

	return false;
}

/**
	@brief Forces 16-bit transfer mode on/off when connecting to a scope regardless of ADC resolution
 */
void LeCroyOscilloscope::ForceHDMode(bool mode)
{
	m_highDefinition = mode;

	if(m_highDefinition)
		m_transport->SendCommandQueued("COMM_FORMAT DEF9,WORD,BIN");
	else
		m_transport->SendCommandQueued("COMM_FORMAT DEF9,BYTE,BIN");

}
