/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of Ethernet100BaseT1Decoder
 */
#ifndef Ethernet100BaseT1Decoder_h
#define Ethernet100BaseT1Decoder_h

#include "EthernetProtocolDecoder.h"

class PAM3DecodeConstants
{
public:
	uint32_t	nsamples;
	float		cuthi;
	float		cutlo;
};

class BaseT1DescrambleConstants
{
public:
	uint32_t	len;
	uint32_t	samplesPerThread;
	uint32_t	maxOutputPerThread;
	uint8_t		masterMode;
};

class Ethernet100BaseT1Decoder : public EthernetProtocolDecoder
{
public:
	Ethernet100BaseT1Decoder(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(Ethernet100BaseT1Decoder)

	enum scrambler_t
	{
		SCRAMBLER_M_B13,
		SCRAMBLER_S_B19
	};

protected:
	FilterParameter& m_scrambler;

	FilterParameter& m_upperThresholdI;
	FilterParameter& m_upperThresholdQ;
	FilterParameter& m_lowerThresholdI;
	FilterParameter& m_lowerThresholdQ;

	AcceleratorBuffer<int8_t> m_pointsI;
	AcceleratorBuffer<int8_t> m_pointsQ;

	std::shared_ptr<ComputePipeline> m_pam3DecodeComputePipeline;
	std::shared_ptr<ComputePipeline> m_descrambleComputePipeline;
};

#endif
