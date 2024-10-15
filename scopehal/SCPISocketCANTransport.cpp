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
	@brief Implementation of SCPISocketCANTransport
	@ingroup transports
 */

#include "scopehal.h"

#ifdef __linux

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <linux/errqueue.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPISocketCANTransport::SCPISocketCANTransport(const string& args)
	: m_devname(args)
{
	m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if(m_socket < 0)
	{
		perror("failed to open socket\n");
		LogError("Failed to open CAN interface\n");
		return;
	}

	ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, args.c_str(), sizeof(ifr.ifr_name) - 1);
	if(0 != ioctl(m_socket, SIOCGIFINDEX, &ifr))
	{
		perror("SIOCGIFINDEX failed\n");
		LogError("Failed to open CAN interface\n");
		return;
	}
	LogTrace("Found CAN interface %s at index %d\n", args.c_str(), ifr.ifr_ifindex);

	sockaddr_can addr;
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if(0 != bind(m_socket, (sockaddr*)&addr, sizeof(addr)))
	{
		perror("bind failed\n");
		LogError("Failed to open CAN interface\n");
		return;
	}

	//set 1ms timeout if no packets
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000;
	if(0 != setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
	{
		perror("setsockopt SO_RCVTIMEO\n");
		LogError("Failed to set RX timeout\n");
		return;
	}

	//request hardware timestamping requires root
	//alternatively do hwstamp_ctl -i can0 -r 1
	hwtstamp_config cfg;
	/*cfg.flags = 0;
	cfg.tx_type = HWTSTAMP_TX_OFF;
	cfg.rx_filter = HWTSTAMP_FILTER_ALL;*/
	ifr.ifr_data = (char*)&cfg;
	if(0 != ioctl(m_socket, SIOCGHWTSTAMP, &ifr))
		perror("SIOCGHWTSTAMP failed\n");

	if(cfg.rx_filter == HWTSTAMP_FILTER_ALL)
		LogDebug("hardware timestamp enabled\n");
	else
		LogDebug("hardware timestamp state %d\n", cfg.rx_filter);

	//Enable hardware timestamping
	int enable = 1;
	if(0 != setsockopt(m_socket, SOL_SOCKET, SO_TIMESTAMPNS, &enable, sizeof(enable)))
	{
		perror("setsockopt SO_TIMESTAMPNS\n");
		LogError("Failed to enable timestamping\n");
		return;
	}
}

SCPISocketCANTransport::~SCPISocketCANTransport()
{
	close(m_socket);
}

bool SCPISocketCANTransport::IsConnected()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual transport code

string SCPISocketCANTransport::GetTransportName()
{
	return "socketcan";
}

string SCPISocketCANTransport::GetConnectionString()
{
	return m_devname;
}

bool SCPISocketCANTransport::SendCommand(const string& /*cmd*/)
{
	//read only
	return false;
}

string SCPISocketCANTransport::ReadReply(bool /*endOnSemicolon*/, [[maybe_unused]] function<void(float)> progress)
{
	return "";
}

void SCPISocketCANTransport::FlushRXBuffer(void)
{
}

void SCPISocketCANTransport::SendRawData(size_t /*len*/, const unsigned char* /*buf*/)
{
}

/**
	@brief Recommended interface w/ hardware timestamping
 */
size_t SCPISocketCANTransport::ReadPacket(can_frame* frame, int64_t& sec, int64_t& ns)
{
	iovec iov;
	iov.iov_base = frame;
	iov.iov_len = sizeof(can_frame);

	char ctrl[1536];

	msghdr msg;
	msg.msg_name = nullptr;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);
	msg.msg_flags = 0;

	auto rlen = recvmsg(m_socket, &msg, 0);

	//failed
	if(rlen < 0)
	{
		//normal timeout
		if( (errno == EAGAIN) || (errno == EWOULDBLOCK) )
			return 0;

		perror("ReadRawData failed\n");
		return 0;
	}

	//no data
	else if(rlen == 0)
		return 0;

	//extract timestamp
	for(auto pmsg = CMSG_FIRSTHDR(&msg); pmsg != nullptr; pmsg = CMSG_NXTHDR(&msg, pmsg) )
	{
		if(pmsg->cmsg_level != SOL_SOCKET)
			continue;
		if(pmsg->cmsg_type != SCM_TIMESTAMPNS)
			continue;

		scm_timestamping64 data;
		memcpy(&data, CMSG_DATA(pmsg), sizeof(data));

		//LogDebug("got valid timestamp\n");
		//for(int i=0; i<3; i++)
		//	LogDebug("[%d] %lld.%lld\n", i, data.ts[i].tv_sec, data.ts[i].tv_nsec);

		//got a timestamp, for now only use the first
		//(there can be up to 3 and its not clear which to use, but the first looks to make the most sense)
		sec = data.ts[0].tv_sec;
		ns = data.ts[0].tv_nsec;

		break;
	}

	return rlen;
}

/**
	@brief For backward compatibility, doesn't provide timestamps
 */
size_t SCPISocketCANTransport::ReadRawData(size_t len, unsigned char* buf, std::function<void(float)> /*progress*/)
{
	iovec iov;
	iov.iov_base = buf;
	iov.iov_len = len;

	char ctrl[1536];

	msghdr msg;
	msg.msg_name = nullptr;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);
	msg.msg_flags = 0;

	auto rlen = recvmsg(m_socket, &msg, 0);

	//failed
	if(rlen < 0)
	{
		//normal timeout
		if( (errno == EAGAIN) || (errno == EWOULDBLOCK) )
			return 0;

		perror("ReadRawData failed\n");
		return 0;
	}

	//no data
	else if(rlen == 0)
		return 0;

	return rlen;
}

bool SCPISocketCANTransport::IsCommandBatchingSupported()
{
	return true;
}

#endif
