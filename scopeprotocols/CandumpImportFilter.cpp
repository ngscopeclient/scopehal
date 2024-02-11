/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "../scopehal/scopehal.h"
#include "CANDecoder.h"
#include "CandumpImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CandumpImportFilter::CandumpImportFilter(const string& color)
	: PacketDecoder(color, CAT_GENERATION)
	, m_datarate("Data Rate")
{
	m_fpname = "Log File";
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.log";
	m_parameters[m_fpname].m_fileFilterName = "Candump log files (*.log)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &CandumpImportFilter::OnFileNameChanged));

	m_parameters[m_datarate] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_datarate].SetIntVal(500 * 1000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CandumpImportFilter::GetProtocolName()
{
	return "Can-Utils Import";
}

vector<string> CandumpImportFilter::GetHeaders()
{
	vector<string> ret;
	ret.push_back("ID");
	ret.push_back("Mode");
	ret.push_back("Format");
	ret.push_back("Type");
	ret.push_back("Ack");
	ret.push_back("Len");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CandumpImportFilter::SetDefaultName()
{
	auto fname = m_parameters[m_fpname].ToString();

	char hwname[256];
	snprintf(hwname, sizeof(hwname), "%s", BaseName(fname).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

void CandumpImportFilter::OnFileNameChanged()
{
	ClearPackets();

	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Set unit
	SetXAxisUnits(Unit(Unit::UNIT_FS));

	//Open the input file
	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("Couldn't open candump file \"%s\"\n", fname.c_str());
		return;
	}

	//Create output waveform
	auto cap = new CANWaveform;
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	//Calculate length of a single bit on the bus
	int64_t baud = m_parameters[m_datarate].GetIntVal();
	int64_t ui = FS_PER_SECOND / baud;

	//Read the file and process line by line
	bool first = true;
	double timestamp;
	char sinterface[128];
	unsigned int id;
	unsigned int dbytes[8];
	double tstart = 0;
	char line[1024] = {0};
	int64_t tend = 0;
	while(!feof(fp))
	{
		if(!fgets(line, sizeof(line), fp))
			break;

		//Read and skip malformed lines
		int nfields = sscanf(
			line,
			"(%lf) %127s %x#%02x%02x%02x%02x%02x%02x%02x%02x",
			&timestamp,
			sinterface,
			&id,
			dbytes + 0,
			dbytes + 1,
			dbytes + 2,
			dbytes + 3,
			dbytes + 4,
			dbytes + 5,
			dbytes + 6,
			dbytes + 7);
		if(nfields < 3)
			continue;

		//See how many data bytes we have
		int nbytes = nfields - 3;

		//Calculate relative timestamp
		int64_t trel = 0;
		if(first)
		{
			trel = 0;
			tstart = timestamp;
			cap->m_startTimestamp = floor(timestamp);
			cap->m_startFemtoseconds = (timestamp - floor(timestamp)) * FS_PER_SECOND;
			first = false;
		}
		else
			trel = (timestamp - tstart) * FS_PER_SECOND;

		//Timestamps sometimes have some jitter due to USB dongles combining several into one transaction,
		//without logging actual arrival timestamps. So they can appear to be coming at too high a baud rate.
		//Fudge the timestamp if it claims to have come before the previous frame ended
		if(trel < tend)
			trel = tend;

		bool ext = (id > 2047);

		//Add timeline samples (fake durations based on user provided baud rate)
		cap->m_offsets.push_back(trel);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SOF, 0));

		cap->m_offsets.push_back(trel + ui);
		cap->m_durations.push_back(31 * ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, id));

		cap->m_offsets.push_back(trel + 32*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_RTR, (nbytes > 0)));

		cap->m_offsets.push_back(trel + 33*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_FD, 0));

		cap->m_offsets.push_back(trel + 34*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_R0, 0));

		cap->m_offsets.push_back(trel + 35*ui);
		cap->m_durations.push_back(ui*4);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DLC, nbytes));

		//Data
		for(int i=0; i<nbytes; i++)
		{
			cap->m_offsets.push_back(trel + 39*ui + i*8*ui);
			cap->m_durations.push_back(ui*8);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DATA, dbytes[i]));
		}

		tend = trel + 39*ui + nbytes*8*ui;

		//CRC TODO
		//CRC delim TODO
		//ACK TODO
		//ACK delim TODO

		//Add the packet
		//Fake the duration for now: assume 8 bytes payload, extended format, and no stuffing
		//Leave format/type/ack blank, this doesn't seem to be saved in this capture format
		auto pack = new Packet;
		if(nbytes == 0)
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
		else
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
		pack->m_headers["ID"] = to_string_hex(id);
		pack->m_headers["Format"] = ext ? "EXT" : "BASE";
		pack->m_headers["Mode"] = "CAN";
		pack->m_headers["Len"] = to_string(nbytes);
		for(int i=0; i<nbytes; i++)
			pack->m_data.push_back(dbytes[i]);
		pack->m_offset = trel;
		pack->m_len = 128 * ui;
		m_packets.push_back(pack);
	}

	fclose(fp);
}

bool CandumpImportFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs allowed
	return false;
}

void CandumpImportFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, std::shared_ptr<QueueHandle> /*queue*/)
{
	//no-op, everything happens in OnFileNameChanged
}
