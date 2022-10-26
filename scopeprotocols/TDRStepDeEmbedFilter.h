/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef _APPLE_SILICON

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of TDRStepDeEmbedFilter
 */
#ifndef TDRStepDeEmbedFilter_h
#define TDRStepDeEmbedFilter_h

#include <ffts.h>

class TDRStepDeEmbedFilter : public Filter
{
public:
	TDRStepDeEmbedFilter(const std::string& color);
	virtual ~TDRStepDeEmbedFilter();

	virtual void Refresh();

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	virtual void ClearSweeps();

	PROTOCOL_DECODER_INITPROC(TDRStepDeEmbedFilter)

protected:

	//Input buffer for averaging
	std::vector<float> m_inputSums;
	size_t m_numAverages;

	ffts_plan_t* m_plan;
	size_t m_cachedPlanSize;

	std::vector<float, AlignedAllocator<float, 64> > m_signalinbuf;
	std::vector<float, AlignedAllocator<float, 64> > m_signaloutbuf;

	std::vector<float, AlignedAllocator<float, 64> > m_stepinbuf;
	std::vector<float, AlignedAllocator<float, 64> > m_stepoutbuf;
};

#endif

#endif
