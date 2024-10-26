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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of PcapngExportFilter
	@ingroup export
 */

#include "../scopehal/scopehal.h"
#include "PcapngExportFilter.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PcapngExportFilter::PcapngExportFilter(const string& color)
	: ExportFilter(color)
{
	m_parameters[m_fname].m_fileFilterMask = "*.pcapng";
	m_parameters[m_fname].m_fileFilterName = "PcapNG files (*.pcapng)";

	CreateInput("packets");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PcapngExportFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	//Reject invalid port indexes
	if(i > 0)
		return false;

	//Make sure the input is coming from an Ethernet decode (for now)
	if( (i == 0) && (dynamic_cast<EthernetWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PcapngExportFilter::GetProtocolName()
{
	return "PcapNG Export";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PcapngExportFilter::Export()
{
	LogTrace("Exporting\n");
	LogIndenter li;

	if(!VerifyAllInputsOK())
		return;

	//If file is not open, open it and write a section header block if necessary
	if(!m_fp)
	{
		LogTrace("File wasn't open, opening it\n");

		auto mode = static_cast<ExportMode_t>(m_parameters[m_mode].GetIntVal());

		auto fname = m_parameters[m_fname].GetFileName();
		bool append = (mode == MODE_CONTINUOUS_APPEND) || (mode == MODE_MANUAL_APPEND);
		if(append)
			m_fp = fopen(fname.c_str(), "ab");
		else
			m_fp = fopen(fname.c_str(), "wb");

		if(!m_fp)
		{
			LogError("Failed to open file %s for writing\n", fname.c_str());
			return;
		}

		//See if file is empty. If so, write header
		fseek(m_fp, 0, SEEK_END);
		if(ftell(m_fp) == 0)
		{
			LogTrace("File was empty, writing SHB\n");

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Write the SHB

			//Block type
			uint32_t blocktype = 0x0a0d0d0a;
			if(!fwrite(&blocktype, sizeof(blocktype), 1, m_fp))
				LogError("file write failure\n");

			//Length of the SHB itself
			uint32_t shblen = 28;
			if(!fwrite(&shblen, sizeof(shblen), 1, m_fp))
				LogError("file write failure\n");

			//Byte order magic
			uint32_t bom = 0x1a2b3c4d;
			if(!fwrite(&bom, sizeof(bom), 1, m_fp))
				LogError("file write failure\n");

			//File format version (1.0)
			uint16_t major = 1;
			uint16_t minor = 0;
			if(!fwrite(&major, sizeof(major), 1, m_fp))
				LogError("file write failure\n");
			if(!fwrite(&minor, sizeof(minor), 1, m_fp))
				LogError("file write failure\n");

			//Section length (unspecified since we append live as data comes in and don't know a priori)
			int64_t seclen = -1;
			if(!fwrite(&seclen, sizeof(seclen), 1, m_fp))
				LogError("file write failure\n");

			//Block total length again
			if(!fwrite(&shblen, sizeof(shblen), 1, m_fp))
				LogError("file write failure\n");

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Write the IDB

			//Block type
			blocktype = 0x1;
			if(!fwrite(&blocktype, sizeof(blocktype), 1, m_fp))
				LogError("file write failure\n");

			//Length of the IDB itself
			uint32_t idblen = 40;
			if(!fwrite(&idblen, sizeof(idblen), 1, m_fp))
				LogError("file write failure\n");

			//Link type
			uint16_t linktype = 1;
			if(!fwrite(&linktype, sizeof(linktype), 1, m_fp))
				LogError("file write failure\n");

			//Padding
			uint16_t pad = 0;
			if(!fwrite(&pad, sizeof(pad), 1, m_fp))
				LogError("file write failure\n");

			//Snapshot length
			uint32_t snaplen = 0;
			if(!fwrite(&snaplen, sizeof(snaplen), 1, m_fp))
				LogError("file write failure\n");

			//Option if_name (total 8 bytes)
			uint16_t optid = 2;
			if(!fwrite(&optid, sizeof(optid), 1, m_fp))
				LogError("file write failure\n");
			uint16_t optlen = 4;
			if(!fwrite(&optlen, sizeof(optlen), 1, m_fp))
				LogError("file write failure\n");
			const char* ifname = "eth0";
			if(!fwrite(ifname, strlen(ifname), 1, m_fp))
				LogError("file write failure\n");

			//Option it_tsresol (total 8 bytes)
			optid = 9;
			optlen = 1;
			if(!fwrite(&optid, sizeof(optid), 1, m_fp))
				LogError("file write failure\n");
			if(!fwrite(&optlen, sizeof(optlen), 1, m_fp))
				LogError("file write failure\n");
			uint8_t tsresol[4] = {9, 0, 0, 0};	//nanosecond resolution
			if(!fwrite(tsresol, sizeof(tsresol), 1, m_fp))
				LogError("file write failure\n");

			//Option endofopt (total 4 bytes)
			if(!fwrite(&pad, sizeof(pad), 1, m_fp))
				LogError("file write failure\n");
			if(!fwrite(&pad, sizeof(pad), 1, m_fp))
				LogError("file write failure\n");

			//Write the IDB length again
			if(!fwrite(&idblen, sizeof(idblen), 1, m_fp))
				LogError("file write failure\n");
		}
	}

	auto stream = GetInput(0);
	auto wfm = dynamic_cast<EthernetWaveform*>(stream.m_channel->GetData(0));
	if(wfm)
		ExportEthernet(wfm);

	fflush(m_fp);
}

/**
	@brief Export Ethernet frames to a PCAPNG file
 */
void PcapngExportFilter::ExportEthernet(EthernetWaveform* wfm)
{
	vector<uint8_t> bytes;
	int64_t offset = 0;
	for(size_t i=0; i<wfm->m_samples.size(); i++)
	{
		auto& samp = wfm->m_samples[i];
		switch(samp.m_type)
		{
			//Start a new frame, clear out anything else
			case EthernetFrameSegment::TYPE_SFD:
				bytes.clear();
				offset = wfm->m_offsets[i] * wfm->m_timescale + wfm->m_triggerPhase;
				break;

			//Frame data
			case EthernetFrameSegment::TYPE_DST_MAC:
			case EthernetFrameSegment::TYPE_SRC_MAC:
			case EthernetFrameSegment::TYPE_ETHERTYPE:
			case EthernetFrameSegment::TYPE_VLAN_TAG:
			case EthernetFrameSegment::TYPE_PAYLOAD:
				{
					for(size_t j=0; j<samp.m_data.size(); j++)
						bytes.push_back(samp.m_data[j]);
				}
				break;

			//Good checksum, save the packet to the file
			case EthernetFrameSegment::TYPE_FCS_GOOD:
				ExportPacket(bytes, wfm->m_startTimestamp, wfm->m_startFemtoseconds + offset);
				bytes.clear();
				break;

			//bad checksum, drop the packet
			case EthernetFrameSegment::TYPE_FCS_BAD:
				bytes.clear();
				break;

			//ignore anything else
			default:
				break;
		}
	}
}

void PcapngExportFilter::ExportPacket(vector<uint8_t>& packet, time_t timestamp, int64_t fs)
{
	//Canonicalize the timestamp to a single 64-bit nanosecond resolution quantity
	int64_t ns = (1e9 * timestamp) + (fs * 1e-6);

	//Block type
	uint32_t blocktype = 6;
	if(!fwrite(&blocktype, sizeof(blocktype), 1, m_fp))
		LogError("file write failure\n");

	//Block length (padded up to next 32 bit boundary)
	uint32_t blocklen = 36 + packet.size();
	uint32_t paddinglen = 4 - (blocklen % 4);
	if(paddinglen == 4)
		paddinglen = 0;
	if(!fwrite(&blocklen, sizeof(blocklen), 1, m_fp))
		LogError("file write failure\n");

	//Interface ID
	uint32_t iface = 0;
	if(!fwrite(&iface, sizeof(iface), 1, m_fp))
		LogError("file write failure\n");

	//Timestamp
	uint32_t tshi = (ns >> 32);
	uint32_t tslo = (ns & 0xffffffff);
	if(!fwrite(&tshi, sizeof(tshi), 1, m_fp))
		LogError("file write failure\n");
	if(!fwrite(&tslo, sizeof(tslo), 1, m_fp))
		LogError("file write failure\n");

	//Packet length repeated twice (original + captured, both always equal for us)
	uint32_t packetlen = packet.size();
	if(!fwrite(&packetlen, sizeof(packetlen), 1, m_fp))
		LogError("file write failure\n");
	if(!fwrite(&packetlen, sizeof(packetlen), 1, m_fp))
		LogError("file write failure\n");

	//Packet data
	if(!fwrite(&packet[0], packet.size(), 1, m_fp))
		LogError("file write failure\n");

	//Pad out to 32 bit boundary
	uint8_t padbuf[4] = {0};
	if(!fwrite(&padbuf[0], paddinglen, 1, m_fp))
		LogError("file write failure\n");

	//Option endofopt (total 4 bytes)
	uint16_t pad = 0;
	if(!fwrite(&pad, sizeof(pad), 1, m_fp))
		LogError("file write failure\n");
	if(!fwrite(&pad, sizeof(pad), 1, m_fp))
		LogError("file write failure\n");

	//Repeat block length
	if(!fwrite(&blocklen, sizeof(blocklen), 1, m_fp))
		LogError("file write failure\n");
}
