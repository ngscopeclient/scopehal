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
	@brief Declaration of Ethernet100BaseTXDecoder
 */
#ifndef Ethernet100BaseTXDecoder_h
#define Ethernet100BaseTXDecoder_h

class Ethernet100BaseTXDescramblerConstants
{
public:
	uint32_t	len;
	uint32_t	samplesPerThread;
	uint32_t	startOffset;
	uint32_t	initialLfsrState;
};

class Ethernet100BaseTXDecoder : public EthernetProtocolDecoder
{
public:
	Ethernet100BaseTXDecoder(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;
	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(Ethernet100BaseTXDecoder)

protected:
	int GetState(float voltage)
	{
		if(voltage > 0.5)
			return 1;
		else if(voltage < -0.5)
			return -1;
		else
			return 0;
	}

	void DecodeStates(
		vk::raii::CommandBuffer& cmdBuf,
		std::shared_ptr<QueueHandle> queue,
		SparseAnalogWaveform* samples);

	bool TrySync(size_t idle_offset);

	void Descramble(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue, size_t idle_offset);

	///@brief Raw scrambled serial bit stream after MLT-3 decoding
	AcceleratorBuffer<uint8_t> m_phyBits;

	///@brief descrambled serial bit stream after LFSR
	AcceleratorBuffer<uint8_t> m_descrambledBits;

	///@brief LFSR lookahead table
	AcceleratorBuffer<uint32_t> m_lfsrTable;

	///@brief Compute pipeline for MLT-3 decoding
	std::shared_ptr<ComputePipeline> m_mlt3DecodeComputePipeline;

	///@brief Compute pipeline for descrambling
	std::shared_ptr<ComputePipeline> m_descrambleComputePipeline;

	///@brief Pool of command buffers
	std::unique_ptr<vk::raii::CommandPool> m_cmdPool;

	///@brief Command buffer for transfers
	std::unique_ptr<vk::raii::CommandBuffer> m_transferCmdBuf;

	//@brief Queue for transfers
	std::shared_ptr<QueueHandle> m_transferQueue;
};

#endif
