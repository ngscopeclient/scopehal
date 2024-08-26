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
	, m_fpname("PcapNG File")
	, m_datarate("Data Rate")
	, m_linkType(LINK_TYPE_UNKNOWN)
	, m_timestampScale(1)
{
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.pcapng";
	m_parameters[m_fpname].m_fileFilterName = "PcapNG files (*.pcapng)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &PcapngImportFilter::OnFileNameChanged));

	m_parameters[m_datarate] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_datarate].SetIntVal(500 * 1000);
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

	//Default timestamp resolution is microsecond so 1e9 fs
	m_timestampScale = 1000LL * 1000LL * 1000LL;

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
	long blockstart;
	while(!feof(fp))
	{
		blockstart = ftell(fp);

		uint32_t blocktype;
		if(1 != fread(&blocktype, sizeof(blocktype), 1, fp))
			return;
		if(1 != fread(&blocklen, sizeof(blocklen), 1, fp))
			return;
		LogTrace("blocktype %d blocklen %d\n", blocktype, blocklen);

		//Interface Definition Block
		if(blocktype == 1)
		{
			if(!ReadIDB(fp))
				return;

			//read and discard trailing block size
			if(1 != fread(&blocklen, sizeof(blocklen), 1, fp))
				return;
		}

		//Enhanced Packet Block: start of data stream
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

	}
	if(!gotEPB)
	{
		LogWarning("Didn't get an Enhanced Packet Block, nothing to do\n");
		return;
	}

	//Move back to the start of the EPB
	LogTrace("Ready to start reading frame data\n");
	fseek(fp, blockstart, SEEK_SET);

	switch(m_linkType)
	{
		case LINK_TYPE_SOCKETCAN:
			LoadSocketCAN(fp);
			break;

		case LINK_TYPE_LINUX_COOKED:
			//Linux cooked encapsulation is special: we don't know the output data format initially
			//and there can be a mix of several which we don't currently implement!
			LoadLinuxCooked(fp);
			break;

		default:
			break;
	}

	fclose(fp);
}

//TODO: this shares a lot in common with LoadCANLinuxCooked, how can we share more?
bool PcapngImportFilter::LoadSocketCAN(FILE* fp)
{
	LogTrace("Loading SocketCAN packets\n");
	LogIndenter li;

	//Create output waveform
	auto cap = new CANWaveform;
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	bool first = true;
	int64_t baseTimestamp = 0;

	//Calculate length of a single bit on the bus
	int64_t baud = m_parameters[m_datarate].GetIntVal();
	int64_t ui = FS_PER_SECOND / baud;

	uint32_t blocktype;
	uint32_t blocklen;
	int64_t tend = 0;
	while(!feof(fp))
	{
		auto blockstart = ftell(fp);

		if(1 != fread(&blocktype, sizeof(blocktype), 1, fp))
			return false;
		if(1 != fread(&blocklen, sizeof(blocklen), 1, fp))
			return false;

		//Should be an EPB, ignore anything else
		switch(blocktype)
		{
			case 5:
				LogTrace("Found Block Statistics (%d bytes)\n", blocklen);
				fseek(fp, blockstart + blocklen, SEEK_SET);
				continue;

			case 6:
				//LogTrace("Found EPB (%d bytes)\n", blocklen);
				break;

			default:
				//unknown type, wut?
				LogWarning("unknown block type %d\n", blocktype);
				fseek(fp, blockstart + blocklen, SEEK_SET);
				continue;
		}
		LogIndenter li2;

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// PCAPNG EPB headers

		//For now, ignore interface number since we don't support mixed captures or multiple output streams yet
		uint32_t ifacenum;
		if(1 != fread(&ifacenum, sizeof(ifacenum), 1, fp))
			return false;
		//LogTrace("Interface %u\n", ifacenum);

		//Timestamp
		uint32_t tstamp[2];
		if(2 != fread(tstamp, sizeof(uint32_t), 2, fp))
			return false;

		//Convert from packed format in native units to a single 64-bit integer
		int64_t stamp = tstamp[0];
		stamp = (stamp << 32) | tstamp[1];

		//If this is the FIRST packet in the capture, it's the base timestamp and we measure offsets from that
		if(first)
		{
			baseTimestamp = stamp;
			stamp = 0;
			first = false;

			//Convert base timestamp to seconds and fs
			int64_t ticks_per_fs = FS_PER_SECOND / m_timestampScale;
			cap->m_startTimestamp = baseTimestamp / ticks_per_fs;
			cap->m_startFemtoseconds = m_timestampScale * (baseTimestamp % ticks_per_fs);
		}

		//Not first, use relative offset
		else
			stamp -= baseTimestamp;

		//Convert from native units to fs
		stamp *= m_timestampScale;

		//Actual as-captured packet length
		uint32_t packlen;
		if(1 != fread(&packlen, sizeof(packlen), 1, fp))
			return false;
		if(packlen < 16)
		{
			LogWarning("Invalid packet length %d (should be >= 16 to allow room for cooked headers)\n", packlen);
			fseek(fp, blockstart + blocklen, SEEK_SET);
			continue;
		}

		//Original packet length (might have been truncated, but ignore this)
		uint32_t origlen;
		if(1 != fread(&origlen, sizeof(origlen), 1, fp))
			return false;

		//Timestamps sometimes have some jitter due to USB dongles combining several into one transaction,
		//without logging actual arrival timestamps. So they can appear to be coming at too high a baud rate.
		//Fudge the timestamp if it claims to have come before the previous frame ended
		if(stamp < tend)
			stamp = tend;

		//Read CAN ID (32 bit on wire)
		uint32_t id;
		if(1 != fread(&id, sizeof(id), 1, fp))
			return false;
		id = ntohl(id);

		//Read frame length
		uint8_t nbytes;
		if(1 != fread(&nbytes, sizeof(nbytes), 1, fp))
			return false;
		if(nbytes > 8)
		{
			LogWarning("Invalid DLC %d (should be <= 8)\n", nbytes);
			fseek(fp, blockstart + blocklen, SEEK_SET);
			continue;
		}

		//Skip 3 bytes of FD flags / reserved before the payload
		fseek(fp, 3, SEEK_CUR);

		//Read payload
		uint8_t data[8];
		if(nbytes != fread(data, 1, nbytes, fp))
			return false;

		//Extract header bits (packed in with ID)
		bool ext = (id & 0x80000000);
		bool rtr = (id & 0x40000000);
		bool err = (id & 0x20000000);
		id &= 0x1fffffff;
		bool fd = false;//(proto == 0x0d);

		//Add timeline samples
		cap->m_offsets.push_back(stamp);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SOF, 0));

		cap->m_offsets.push_back(stamp + ui);
		cap->m_durations.push_back(31 * ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, id));

		cap->m_offsets.push_back(stamp + 32*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_RTR, rtr));

		cap->m_offsets.push_back(stamp + 33*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_FD, fd));

		cap->m_offsets.push_back(stamp + 34*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_R0, 0));

		cap->m_offsets.push_back(stamp + 35*ui);
		cap->m_durations.push_back(ui*4);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DLC, nbytes));

		//Data
		for(size_t i=0; i<nbytes; i++)
		{
			cap->m_offsets.push_back(stamp + 39*ui + i*8*ui);
			cap->m_durations.push_back(ui*8);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DATA, data[i]));
		}

		tend = stamp + 39*ui + nbytes*8*ui;

		//CRC TODO
		//CRC delim TODO
		//ACK TODO
		//ACK delim TODO

		//Add the packet
		//Fake the duration for now: assume 8 bytes payload, extended format, and no stuffing
		//Leave format/type/ack blank, this doesn't seem to be saved in this capture format
		auto pack = new Packet;
		if(err)
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
		else if(rtr)
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
		else
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
		pack->m_headers["Format"] = ext ? "EXT" : "BASE";
		pack->m_headers["ID"] = to_string_hex(id);
		pack->m_headers["Mode"] = fd ? "CAN-FD" : "CAN";
		pack->m_headers["Len"] = to_string(nbytes);
		if(err)
			pack->m_headers["Format"] = "ERR";
		for(size_t i=0; i<nbytes; i++)
			pack->m_data.push_back(data[i]);
		pack->m_offset = stamp;
		pack->m_len = 128 * ui;
		m_packets.push_back(pack);

		//End of the EPB, skip any unread contents
		fseek(fp, blockstart + blocklen, SEEK_SET);
	}

	return true;
}

bool PcapngImportFilter::LoadLinuxCooked(FILE* fp)
{
	LogTrace("Loading Linux cooked format packets\n");
	LogIndenter li;

	//We don't know the interface format yet!
	//Look ahead a bit to figure that out

	//Outer headers
	//uint32 block type
	//uint32_t len

	//EPB headers
	//unit32 iface_id
	//uint64 timestamp
	//uint32 packlen
	//uint32 origpacklen

	//Linux cooked packet headers
	//uint16 packet_type
	//uint16 ARPHRD_type

	//So we need to skip ahead 30 bytes and sneak a peek at the AHPHRD_type field to know
	//what kind of waveform we're dealing with.
	//TODO: support multiple interfaces and multiple encapsulations in a single packet stream
	auto orig = ftell(fp);
	fseek(fp, 30, SEEK_CUR);

	uint16_t arphrd;
	if(1 != fread(&arphrd, sizeof(arphrd), 1, fp))
		return false;
	arphrd = ntohs(arphrd);

	//Ok, back to where we started
	fseek(fp, orig, SEEK_SET);

	//So what is it?
	switch(arphrd)
	{
		case 280:
			return LoadCANLinuxCooked(fp);

		default:
			LogError("Unknown inner format %d in Linux cooked encapsulation\n", arphrd);
			break;
	}

	return true;
}

bool PcapngImportFilter::LoadCANLinuxCooked(FILE* fp)
{
	LogTrace("Loading CAN frames with Linux cooked encapsulation\n");
	LogIndenter li;

	//Create output waveform
	auto cap = new CANWaveform;
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();
	SetData(cap, 0);

	bool first = true;
	int64_t baseTimestamp = 0;

	//Calculate length of a single bit on the bus
	int64_t baud = m_parameters[m_datarate].GetIntVal();
	int64_t ui = FS_PER_SECOND / baud;

	uint32_t blocktype;
	uint32_t blocklen;
	int64_t tend = 0;
	while(!feof(fp))
	{
		auto blockstart = ftell(fp);

		if(1 != fread(&blocktype, sizeof(blocktype), 1, fp))
			return false;
		if(1 != fread(&blocklen, sizeof(blocklen), 1, fp))
			return false;

		//Should be an EPB, ignore anything else
		switch(blocktype)
		{
			case 5:
				LogTrace("Found Block Statistics (%d bytes)\n", blocklen);
				fseek(fp, blockstart + blocklen, SEEK_SET);
				continue;

			case 6:
				//LogTrace("Found EPB (%d bytes)\n", blocklen);
				break;

			default:
				//unknown type, wut?
				LogWarning("unknown block type %d\n", blocktype);
				fseek(fp, blockstart + blocklen, SEEK_SET);
				continue;
		}
		LogIndenter li2;

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// PCAPNG EPB headers

		//For now, ignore interface number since we don't support mixed captures or multiple output streams yet
		uint32_t ifacenum;
		if(1 != fread(&ifacenum, sizeof(ifacenum), 1, fp))
			return false;
		//LogTrace("Interface %u\n", ifacenum);

		//Timestamp
		uint32_t tstamp[2];
		if(2 != fread(tstamp, sizeof(uint32_t), 2, fp))
			return false;

		//Convert from packed format in native units to a single 64-bit integer
		int64_t stamp = tstamp[0];
		stamp = (stamp << 32) | tstamp[1];

		//If this is the FIRST packet in the capture, it's the base timestamp and we measure offsets from that
		if(first)
		{
			baseTimestamp = stamp;
			stamp = 0;
			first = false;

			//Convert base timestamp to seconds and fs
			int64_t ticks_per_fs = FS_PER_SECOND / m_timestampScale;
			cap->m_startTimestamp = baseTimestamp / ticks_per_fs;
			cap->m_startFemtoseconds = m_timestampScale * (baseTimestamp % ticks_per_fs);
		}

		//Not first, use relative offset
		else
			stamp -= baseTimestamp;

		//Convert from native units to fs
		stamp *= m_timestampScale;

		//Actual as-captured packet length
		uint32_t packlen;
		if(1 != fread(&packlen, sizeof(packlen), 1, fp))
			return false;
		if(packlen < 16)
		{
			LogWarning("Invalid packet length %d (should be >= 16 to allow room for cooked headers)\n", packlen);
			fseek(fp, blockstart + blocklen, SEEK_SET);
			continue;
		}

		//Original packet length (might have been truncated, but ignore this)
		uint32_t origlen;
		if(1 != fread(&origlen, sizeof(origlen), 1, fp))
			return false;

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Linux cooked packet header

		//Packet type (typically always be 0x01 broadcast, or 0x04 sent by us, for CAN)
		uint16_t packtype;
		if(1 != fread(&packtype, sizeof(packtype), 1, fp))
			return false;

		//ARPHRD type (should always be 280, CAN, if we get to this point)
		uint16_t arphrd;
		if(1 != fread(&arphrd, sizeof(arphrd), 1, fp))
			return false;
		arphrd = ntohs(arphrd);
		if(arphrd != 280)
		{
			LogWarning("Unknown ARPHRD type %d in what we expected to be a CAN capture inside Linux cooked headers\n",
				arphrd);
			fseek(fp, blockstart + blocklen, SEEK_SET);
			continue;
		}

		//Link layer address length (should always be 0 for CAN bus)
		uint16_t linklen;
		if(1 != fread(&linklen, sizeof(linklen), 1, fp))
			return false;
		linklen = ntohs(linklen);
		if(linklen != 0)
		{
			LogWarning("Invalid link layer address length %d (should be 0 for CAN)\n", linklen);
			fseek(fp, blockstart + blocklen, SEEK_SET);
			continue;
		}

		//8 bytes of padding (where link layer address would be if we had one)
		uint64_t padding;
		if(1 != fread(&padding, sizeof(padding), 1, fp))
			return false;

		//Protocol type (should be 0x0C, CAN bus or 0x0d (CAN-FD))
		uint16_t proto;
		if(1 != fread(&proto, sizeof(proto), 1, fp))
			return false;
		proto = ntohs(proto);
		if( (proto != 0x0c) && (proto != 0x0d) )
		{
			LogWarning("Invalid protocol type 0x%02x (should be 0x0c for CAN or 0x0d for CAN-FD)\n", proto);
			fseek(fp, blockstart + blocklen, SEEK_SET);
			continue;
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// CAN packet itself

		//Timestamps sometimes have some jitter due to USB dongles combining several into one transaction,
		//without logging actual arrival timestamps. So they can appear to be coming at too high a baud rate.
		//Fudge the timestamp if it claims to have come before the previous frame ended
		if(stamp < tend)
			stamp = tend;

		//Read CAN ID (32 bit on wire)
		uint32_t id;
		if(1 != fread(&id, sizeof(id), 1, fp))
			return false;

		//Read frame length
		uint32_t nbytes;
		if(1 != fread(&nbytes, sizeof(nbytes), 1, fp))
			return false;
		if(nbytes > 8)
		{
			LogWarning("Invalid DLC %d (should be <= 8)\n", nbytes);
			fseek(fp, blockstart + blocklen, SEEK_SET);
			continue;
		}

		//Read payload
		uint8_t data[8];
		if(nbytes != fread(data, 1, nbytes, fp))
			return false;

		//Extract header bits (packed in with ID)
		bool ext = (id & 0x80000000);
		bool rtr = (id & 0x40000000);
		id &= 0x1fffffff;
		bool fd = (proto == 0x0d);

		//Add timeline samples
		cap->m_offsets.push_back(stamp);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SOF, 0));

		cap->m_offsets.push_back(stamp + ui);
		cap->m_durations.push_back(31 * ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, id));

		cap->m_offsets.push_back(stamp + 32*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_RTR, rtr));

		cap->m_offsets.push_back(stamp + 33*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_FD, fd));

		cap->m_offsets.push_back(stamp + 34*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_R0, 0));

		cap->m_offsets.push_back(stamp + 35*ui);
		cap->m_durations.push_back(ui*4);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DLC, nbytes));

		//Data
		for(size_t i=0; i<nbytes; i++)
		{
			cap->m_offsets.push_back(stamp + 39*ui + i*8*ui);
			cap->m_durations.push_back(ui*8);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DATA, data[i]));
		}

		tend = stamp + 39*ui + nbytes*8*ui;

		//CRC TODO
		//CRC delim TODO
		//ACK TODO
		//ACK delim TODO

		//Add the packet
		//Fake the duration for now: assume 8 bytes payload, extended format, and no stuffing
		//Leave format/type/ack blank, this doesn't seem to be saved in this capture format
		auto pack = new Packet;
		if(rtr)
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
		else
			pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
		pack->m_headers["Format"] = ext ? "EXT" : "BASE";
		pack->m_headers["ID"] = to_string_hex(id);
		pack->m_headers["Mode"] = fd ? "CAN-FD" : "CAN";
		pack->m_headers["Len"] = to_string(nbytes);
		for(size_t i=0; i<nbytes; i++)
			pack->m_data.push_back(data[i]);
		pack->m_offset = stamp;
		pack->m_len = 128 * ui;
		m_packets.push_back(pack);

		//End of the EPB, skip any unread contents
		fseek(fp, blockstart + blocklen, SEEK_SET);
	}

	return true;
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

		case 227:
			LogTrace("SocketCAN data\n");
			m_linkType = LINK_TYPE_SOCKETCAN;
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

				//Nanosecond resolution
				if(t16 == 9)
					m_timestampScale = 1000LL * 1000LL;

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
