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
#include "PcapngImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PcapngImportFilter::PcapngImportFilter(const string& color)
	: PacketDecoder(color, CAT_GENERATION)
	, m_linkType(LINK_TYPE_UNKNOWN)
{
	m_fpname = "PcapNG File";
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.pcapng";
	m_parameters[m_fpname].m_fileFilterName = "PcapNG files (*.pcapng)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &PcapngImportFilter::OnFileNameChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PcapngImportFilter::GetProtocolName()
{
	return "PcapNG Import";
}

vector<string> PcapngImportFilter::GetHeaders()
{
	//for now, assume canbus import
	//TODO: update based on link layer of currently loaded file
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

void PcapngImportFilter::SetDefaultName()
{
	auto fname = m_parameters[m_fpname].ToString();

	char hwname[256];
	snprintf(hwname, sizeof(hwname), "%s", BaseName(fname).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

void PcapngImportFilter::OnFileNameChanged()
{
	ClearPackets();

	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Set unit
	SetXAxisUnits(Unit(Unit::UNIT_FS));

	//Open the input file
	LogTrace("Loading PcapNG file %s\n", fname.c_str());
	LogIndenter li;
	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("Couldn't open PcapNG file \"%s\"\n", fname.c_str());
		return;
	}

	//Section Header Block
	if(!ValidateSHB(fp))
		return;

	//Read trailing block length (and discard for now)
	//TODO: verify it's correct
	uint32_t blocklen;
	if(1 != fread(&blocklen, sizeof(blocklen), 1, fp))
		return;

	//Read and process packet blocks
	bool gotEPB = false;
	while(!feof(fp))
	{
		auto blockstart = ftell(fp);

		uint32_t blocktype;
		if(1 != fread(&blocktype, sizeof(blocktype), 1, fp))
			return;
		if(1 != fread(&blocklen, sizeof(blocklen), 1, fp))
			return;

		//Interface Definition Block
		if(blocktype == 1)
		{
			if(!ReadIDB(fp))
				return;
		}

		//Enhanced Packet Block
		else if(blocktype == 6)
		{
			gotEPB = true;
			break;
		}

		else
		{
			LogWarning("Unknown block type %d\n", blocktype);
			return;
		}

		//Read and discard redundant blocklen
		uint32_t eblocklen;
		if(1 != fread(&eblocklen, sizeof(eblocklen), 1, fp))
			return;
		if(eblocklen != blocklen)
		{
			LogWarning("Redundant block length doesn't match, corrupted pcapng file?\n");
			return;
		}
	}
	if(!gotEPB)
	{
		LogWarning("Didn't get an Enhanced Packet Block, nothing to do\n");
		return;
	}

	//For now, only support a single Enhanced Packet Block
	LogTrace("Ready to start reading EPB\n");

	//Linux cooked encapsulation is special: we don't know the output data format initially
	//and there can be a mix of several which we don't currently implement

	/*
	//Create output waveform
	auto cap = new CANWaveform;
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

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

		//Add timeline samples (fake durations assuming 500 Kbps for now)
		//TODO make this configurable
		int64_t ui = 2 * 1000LL * 1000LL * 1000LL;
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
		pack->m_headers["Mode"] = "CAN";
		pack->m_headers["Len"] = to_string(nbytes);
		for(int i=0; i<nbytes; i++)
			pack->m_data.push_back(dbytes[i]);
		pack->m_offset = trel;
		pack->m_len = 128 * ui;
		m_packets.push_back(pack);
	}
	*/

	fclose(fp);
}

/**
	@brief Read Interface Definition Block
 */
bool PcapngImportFilter::ReadIDB(FILE* fp)
{
	LogTrace("Reading interface definition block\n");
	LogIndenter li;

	//Read link type
	uint16_t linktype;
	if(1 != fread(&linktype, sizeof(linktype), 1, fp))
		return false;

	switch(linktype)
	{
		case 1:
			LogWarning("PcapNG contains Ethernet data (not yet implemented)\n");
			m_linkType = LINK_TYPE_ETHERNET;
			break;

		case 113:
			LogTrace("Linux cooked packet encapsulation\n");
			m_linkType = LINK_TYPE_LINUX_COOKED;
			break;

		case 189:
			LogWarning("PcapNG contains USB data with Linux header (not yet implemented)\n");
			m_linkType = LINK_TYPE_USB;
			break;

		case 190:
			LogWarning("PcapNG contains CAN 2.0b data (not yet implemented)\n");
			m_linkType = LINK_TYPE_CAN;
			break;

		default:
			LogWarning("PcapNG contains unknown type data %d\n", linktype);
			m_linkType = LINK_TYPE_UNKNOWN;
			break;
	}

	//Read and discard two reserved bytes
	uint16_t reserved;
	if(1 != fread(&reserved, sizeof(reserved), 1, fp))
		return false;

	//Read snap length (for now, ignore it)
	uint32_t snaplen;
	if(1 != fread(&snaplen, sizeof(snaplen), 1, fp))
		return false;
	LogTrace("Snap length is %d bytes\n", snaplen);

	//Read IDB options
	uint8_t tmp;
	bool done = false;
	string str;
	uint16_t t16;
	while(!done)
	{
		//Read the option
		uint16_t optid;
		if(1 != fread(&optid, sizeof(optid), 1, fp))
			return false;

		//Read option length
		uint16_t optlen;
		if(1 != fread(&optlen, sizeof(optlen), 1, fp))
			return false;

		switch(optid)
		{
			//opt_endopt
			case 0:
				done = true;
				break;

			//if_name
			case 2:
				str = ReadFixedLengthString(optlen, fp);
				LogTrace("if_name = %s\n", str.c_str());
				break;

			//if_description
			case 3:
				str = ReadFixedLengthString(optlen, fp);
				LogTrace("if_description = %s\n", str.c_str());
				break;

			//if_tresol
			case 9:
				if(1 != fread(&t16, sizeof(t16), 1, fp))
					return false;
				LogTrace("if_tresol = %u\n", (uint32_t)t16);
				break;

			//if_filter
			case 11:
				str = ReadFixedLengthString(optlen, fp);
				LogTrace("if_filter = %s\n", str.c_str());
				break;

			//if_os
			case 12:
				str = ReadFixedLengthString(optlen, fp);
				LogTrace("if_os = %s\n", str.c_str());
				break;

			//unknown, discard it
			default:
				LogWarning("Unknown IDB option %d\n", optid);
				for(size_t i=0; i<optlen; i++)
					fread(&tmp, 1, 1, fp);
		}

		//Read and discard padding until 32-bit aligned
		while(ftell(fp) & 3)
			fread(&tmp, 1, 1, fp);
	}

	return true;
}

/**
	@brief Read Section Header Block
 */
bool PcapngImportFilter::ValidateSHB(FILE* fp)
{
	LogTrace("Loading SHB\n");
	LogIndenter li;

	//Magic number
	uint32_t blocktype;
	if(1 != fread(&blocktype, sizeof(blocktype), 1, fp))
		return false;
	if(blocktype != 0x0a0d0d0a)
	{
		LogError("Invalid block type %08x\n", blocktype);
		return false;
	}

	//Block length
	uint32_t blocklen;
	if(1 != fread(&blocklen, sizeof(blocklen), 1, fp))
		return false;
	LogTrace("SHB is %d bytes long\n", blocklen);

	//Byte order (for now, only implement little endian)
	uint32_t bom;
	if(1 != fread(&bom, sizeof(bom), 1, fp))
		return false;
	if(bom != 0x1a2b3c4d)
	{
		LogError("Expected a little endian pcap file, got something else (big endian or corrupted)\n");
		return false;
	}

	//Major and minor version numbers
	uint16_t versions[2];
	if(2 != fread(versions, sizeof(uint16_t), 2, fp))
		return false;
	LogTrace("PcapNG file format %d.%d\n", versions[0], versions[1]);

	//Read and discard section length (can't have any content)
	uint64_t ignoredLen;
	if(1 != fread(&ignoredLen, sizeof(ignoredLen), 1, fp))
		return false;

	//Read options
	uint8_t tmp;
	bool done = false;
	string str;
	while(!done)
	{
		//Read the option
		uint16_t optid;
		if(1 != fread(&optid, sizeof(optid), 1, fp))
			return false;

		//Read option length
		uint16_t optlen;
		if(1 != fread(&optlen, sizeof(optlen), 1, fp))
			return false;

		switch(optid)
		{
			//opt_endopt
			case 0:
				done = true;
				break;

			//shb_hardware
			case 2:
				str = ReadFixedLengthString(optlen, fp);
				LogTrace("shb_hardware = %s\n", str.c_str());
				break;

			//shb_os
			case 3:
				str = ReadFixedLengthString(optlen, fp);
				LogTrace("shb_os = %s\n", str.c_str());
				break;

			//shb_userappl
			case 4:
				str = ReadFixedLengthString(optlen, fp);
				LogTrace("shb_userappl = %s\n", str.c_str());
				break;

			//unknown, discard it
			default:
				LogWarning("Unknown SHB option %d\n", optid);
				for(size_t i=0; i<optlen; i++)
					fread(&tmp, 1, 1, fp);
		}

		//Read and discard padding until 32-bit aligned
		while(ftell(fp) & 3)
			fread(&tmp, 1, 1, fp);
	}

	return true;
}

string PcapngImportFilter::ReadFixedLengthString(uint16_t len, FILE* fp)
{
	//TODO: make this more efficient
	string ret;
	char tmp;
	for(size_t i=0; i<len; i++)
	{
		fread(&tmp, 1, 1, fp);
		ret += tmp;
	}
	return ret;
}

bool PcapngImportFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs allowed
	return false;
}

void PcapngImportFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, std::shared_ptr<QueueHandle> /*queue*/)
{
	//no-op, everything happens in OnFileNameChanged
}
