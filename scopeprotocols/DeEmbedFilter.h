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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of DeEmbedFilter
 */
#ifndef DeEmbedFilter_h
#define DeEmbedFilter_h

#include "../scopehal/AlignedAllocator.h"
#include <ffts.h>

#ifdef HAVE_CLFFT
#include <clFFT.h>
#endif

class DeEmbedFilter : public Filter
{
public:
	DeEmbedFilter(const std::string& color);
	virtual ~DeEmbedFilter();

	virtual void Refresh();

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	PROTOCOL_DECODER_INITPROC(DeEmbedFilter)

protected:
	virtual int64_t GetGroupDelay();
	void DoRefresh(bool invert = true);
	virtual void InterpolateSparameters(float bin_hz, bool invert, size_t nouts);

	std::string m_maxGainName;
	std::string m_groupDelayTruncModeName;
	std::string m_groupDelayTruncName;

	enum TruncationMode
	{
		TRUNC_AUTO,
		TRUNC_MANUAL
	};

	float m_cachedMaxGain;

	double m_cachedBinSize;
	std::vector<float, AlignedAllocator<float, 64> > m_resampledSparamSines;
	std::vector<float, AlignedAllocator<float, 64> > m_resampledSparamCosines;

	ffts_plan_t* m_forwardPlan;
	ffts_plan_t* m_reversePlan;
	size_t m_cachedNumPoints;

	std::vector<float, AlignedAllocator<float, 64> > m_forwardInBuf;
	std::vector<float, AlignedAllocator<float, 64> > m_forwardOutBuf;
	std::vector<float, AlignedAllocator<float, 64> > m_reverseOutBuf;

	void MainLoop(size_t nouts);
	void MainLoopAVX2(size_t nouts);

	#ifdef HAVE_CLFFT
	clfftPlanHandle m_clfftForwardPlan;
	clfftPlanHandle m_clfftReversePlan;

	cl::Program* m_windowProgram;
	cl::Kernel* m_rectangularWindowKernel;

	cl::Program* m_deembedProgram;
	cl::Kernel* m_deembedKernel;

	cl::Buffer* m_sinbuf;
	cl::Buffer* m_cosbuf;
	cl::Buffer* m_windowbuf;
	cl::Buffer* m_fftoutbuf;
	#endif

	WaveformCacheKey m_magKey;
	WaveformCacheKey m_angleKey;

	SParameterVector m_cachedSparams;
};

#endif
