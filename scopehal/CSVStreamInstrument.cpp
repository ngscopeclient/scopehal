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
#include "CSVStreamInstrument.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CSVStreamInstrument::CSVStreamInstrument(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
{
	m_vendor = "Antikernel Labs";
	m_model = "CSV Stream";
	m_serial = "N/A";
	m_fwVersion = "1.0";

	//Create initial stream
	m_channels.push_back(new InstrumentChannel(
		"CH1",
		"#808080",
		Unit(Unit::UNIT_COUNTS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_ANALOG_SCALAR,
		0));

	//needs to run *before* the Oscilloscope class implementation
	m_preloaders.push_front(sigc::mem_fun(*this, &CSVStreamInstrument::DoPreLoadConfiguration));
}

CSVStreamInstrument::~CSVStreamInstrument()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instantiation

uint32_t CSVStreamInstrument::GetInstrumentTypes() const
{
	return INST_MISC;
}

uint32_t CSVStreamInstrument::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return INST_MISC;
}

string CSVStreamInstrument::GetDriverNameInternal()
{
	return "csvstream";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void CSVStreamInstrument::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& node,
	IDTable& idmap,
	ConfigWarningList& /*list*/)
{
	m_channels.clear();

	auto& chans = node["channels"];
	for(auto it : chans)
	{
		auto& cnode = it.second;
		auto index = cnode["index"].as<int>();

		//If we don't have the channel yet, create it
		while(m_channels.size() <= (size_t)index)
		{
			m_channels.push_back(new InstrumentChannel(
				string("CH") + to_string(index),
				"#808080",
				Unit(Unit::UNIT_COUNTS),
				Unit(Unit::UNIT_VOLTS),
				Stream::STREAM_TYPE_ANALOG_SCALAR,
				m_channels.size()));
		}

		//Channel exists, register its ID
		auto chan = m_channels[index];
		idmap.emplace(cnode["id"].as<int>(), chan);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acquisition

bool CSVStreamInstrument::AcquireData()
{
	//Read a line of input (may or may not be relevant to us)
	auto line = Trim(m_transport->ReadReply(false));

	//Trim off anything before "CSV" prefix and discard mismatched lines
	auto start = line.find("CSV-");
	if(start == string::npos)
		return true;
	line = line.substr(start);

	//Split up at commas
	auto fields = explode(line, ',');

	if(fields[0] == "CSV-NAME")
	{
		//Name all of our channels
		for(size_t i=1; i<fields.size(); i++)
		{
			//Add a new channel
			if(m_channels.size() < i)
			{
				m_channels.push_back(new InstrumentChannel(
					fields[i],
					"#808080",
					Unit(Unit::UNIT_COUNTS),
					Unit(Unit::UNIT_VOLTS),
					Stream::STREAM_TYPE_ANALOG_SCALAR,
					i-1));
			}

			//Rename an existing channel
			else
				m_channels[i-1]->SetDisplayName(fields[i]);
		}
	}

	else if(fields[0] == "CSV-UNIT")
	{
		//Update units, creating new channels if needed
		for(size_t i=1; i<fields.size(); i++)
		{
			Unit yunit(fields[i]);

			//Add a new channel
			if(m_channels.size() < i)
			{
				m_channels.push_back(new InstrumentChannel(
					string("CH") + to_string(i),
					"#808080",
					Unit(Unit::UNIT_COUNTS),
					yunit,
					Stream::STREAM_TYPE_ANALOG_SCALAR,
					i-1));
			}

			//Rename an existing channel
			else
				m_channels[i-1]->SetYAxisUnits(yunit, 0);
		}
	}

	else if(fields[0] == "CSV-DATA")
	{
		//Update data, creating new channels if needed
		for(size_t i=1; i<fields.size(); i++)
		{
			//Add a new channel if it doesn't exist
			if(m_channels.size() < i)
			{
				m_channels.push_back(new InstrumentChannel(
					string("CH") + to_string(i),
					"#808080",
					Unit(Unit::UNIT_COUNTS),
					Unit(Unit::UNIT_VOLTS),
					Stream::STREAM_TYPE_ANALOG_SCALAR,
					i-1));
			}

			auto value = m_channels[i-1]->GetYAxisUnits(0).ParseString(fields[i]);
			m_channels[i-1]->SetScalarValue(0, value);
		}
	}

	else
	{
		//Nothing to do, it's probably stdout data or something irrelevant
	}

	return true;
}
