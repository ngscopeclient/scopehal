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
	@brief Declaration of BaseMeasurement
 */
#ifndef BaseMeasurement_h
#define BaseMeasurement_h

class BasePushConstants
{
public:
	int64_t		timescale;
	int64_t		triggerPhase;
	uint32_t	bufferPerThread;
	uint32_t	len;
	float		vmin;
	float		mid;
	float		range;
	float		global_base;
};

class BaseMeasurement : public Filter
{
public:
	BaseMeasurement(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(BaseMeasurement)

protected:

	//Minmax calculation
	ComputePipeline m_minmaxPipeline;
	AcceleratorBuffer<float> m_minbuf;
	AcceleratorBuffer<float> m_maxbuf;

	//Histogram calculation
	std::shared_ptr<ComputePipeline> m_histogramPipeline;
	AcceleratorBuffer<uint64_t> m_histogramBuf;

	//Base calculation
	std::shared_ptr<ComputePipeline> m_firstPassComputePipeline;
	AcceleratorBuffer<int64_t> m_firstPassOffsets;
	AcceleratorBuffer<float> m_firstPassSamples;
	AcceleratorBuffer<int64_t> m_finalSampleCount;
	std::shared_ptr<ComputePipeline> m_finalPassComputePipeline;
	AcceleratorBuffer<float> m_partialSums;

	template<class T>
	void InnerLoop(
		T* din,
		SparseAnalogWaveform* cap,
		size_t len,
		float vmin,
		float vmax,
		float fbin)
	{
		cap->PrepareForCpuAccess();
		din->PrepareForCpuAccess();

		//Set temporary midpoint and range
		float range = (vmax - vmin);
		float mid = range/2 + vmin;
		float global_base = fbin*range + vmin;

		std::vector<float> samples;
		bool first = true;
		float delta = range * 0.1;
		int64_t tfall = 0;
		float last = vmin;

		for(size_t i=0; i < len; i++)
		{
			//Wait for a rising edge (end of the low period)
			auto cur = din->m_samples[i];
			auto tnow = GetOffsetScaled(din, i);

			//Find falling edge
			if( (cur < mid) && (last >= mid) )
				tfall = tnow;

			//Find rising edge
			if( (cur > mid) && (last <= mid) )
			{
				//Done, add the sample
				if(!samples.empty())
				{
					if(first)
						first = false;

					else
					{
						//Average the middle 50% of the samples.
						//Discard beginning and end as they include parts of the edge
						float sum = 0;
						int64_t count = 0;
						size_t start = samples.size()/4;
						size_t end = samples.size() - start;
						for(size_t j=start; j<=end; j++)
						{
							sum += samples[j];
							count ++;
						}

						float vavg = sum / count;

						int64_t tmid = (tnow + tfall) / 2;

						//Update duration for last sample
						size_t n = cap->m_samples.size();
						if(n)
							cap->m_durations[n-1] = tmid - cap->m_offsets[n-1];

						cap->m_offsets.push_back(tmid);
						cap->m_durations.push_back(1);
						cap->m_samples.push_back(vavg);
					}

					samples.clear();
				}
			}

			//If the value is fairly close to the calculated base, average it
			if(fabs(cur - global_base) < delta)
				samples.push_back(cur);

			last = cur;
		}

		cap->MarkModifiedFromCpu();
	}
};

#endif
