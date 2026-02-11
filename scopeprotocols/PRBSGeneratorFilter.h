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
	@brief Declaration of PRBSGeneratorFilter
 */
#ifndef PRBSGeneratorFilter_h
#define PRBSGeneratorFilter_h

//Small PRBSes that run one iteration per thread block
class PRBSGeneratorConstants
{
public:
	uint32_t	count;
	uint32_t	seed;
};

//Big PRBSes that do lookahead
class PRBSGeneratorBlockConstants
{
public:
	uint32_t	count;
	uint32_t	seed;
	uint32_t	samplesPerThread;
};

class PRBSGeneratorFilter : public Filter
{
public:
	PRBSGeneratorFilter(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();
	virtual void SetDefaultName() override;

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(PRBSGeneratorFilter)

	enum Polynomials
	{
		POLY_PRBS7 = 7,
		POLY_PRBS9 = 9,
		POLY_PRBS11 = 11,
		POLY_PRBS15 = 15,
		POLY_PRBS23 = 23,
		POLY_PRBS31 = 31
	};

	static bool RunPRBS(uint32_t& state, Polynomials poly);

protected:
	FilterParameter& m_baud;
	FilterParameter& m_poly;
	FilterParameter& m_depth;

	std::shared_ptr<ComputePipeline> m_prbs7Pipeline;
	std::shared_ptr<ComputePipeline> m_prbs9Pipeline;
	std::shared_ptr<ComputePipeline> m_prbs11Pipeline;
	std::shared_ptr<ComputePipeline> m_prbs15Pipeline;
	std::shared_ptr<ComputePipeline> m_prbs23Pipeline;
	std::shared_ptr<ComputePipeline> m_prbs31Pipeline;

	///@brief LFSR lookahead table for PRBS-23 polynomial
	AcceleratorBuffer<uint32_t> m_prbs23Table;

	///@brief LFSR lookahead table for PRBS-31 polynomial
	AcceleratorBuffer<uint32_t> m_prbs31Table;
};

extern const uint32_t g_prbs23Table[23][23];
extern const uint32_t g_prbs31Table[30][31];

#endif
