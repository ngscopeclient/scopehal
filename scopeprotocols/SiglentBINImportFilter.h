/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of SiglentBINImportFilter
 */
#ifndef SiglentBINImportFilter_h
#define SiglentBINImportFilter_h

class SiglentBINImportFilter : public ImportFilter
{
public:
	SiglentBINImportFilter(const std::string& color);

	static std::string GetProtocolName();

	PROTOCOL_DECODER_INITPROC(SiglentBINImportFilter)

	//Siglent binary capture structs
	#pragma pack(push, 1)
	struct FileHeader
	{
		uint32_t version;			//File format version
	};

	// V2/V4 wave header
	struct WaveHeader
	{
		int32_t ch_en[4];			//C1-C4 channel enable
		struct {					//C1-C4 vertical gain
			double value;
			char reserved[32];
		} ch_v_gain[4];
		struct {					//C1-C4 vertical offset
			double value;
			char reserved[32];
		} ch_v_offset[4];
		int32_t digital_en;			//Digital enable
		int32_t d_ch_en[16];		//D0-D15 channel enable
		double time_div;			//Time base
		char reserved9[32];
		double time_delay;			//Trigger delay
		char reserved10[32];
		uint32_t wave_length;		//Number of samples in each analog waveform
		double s_rate;				//C1-C4 sampling rate
		char reserved11[32];
		uint32_t d_wave_length;		//Number of samples in each digital waveform
		double d_s_rate;			//D0-D15 sampling rate
		char reserved12[32];
		double ch_probe[4];			//C1-C4 probe factor
		int8_t data_width;			//0:1 Byte, 1:2 Bytes
		int8_t byte_order;			//0:LSB, 1:MSB
		char reserved13[6];
		int32_t num_hori_div;		//Number of horizontal divisions
		int32_t ch_codes_per_div[4];//C1-C4 codes per division
		int32_t math_en[4];			//F1-F4 channel enable
		struct {					//F1-F4 vertical gain
			double value;
			char reserved[32];
		} math_v_gain[4];
		struct {					//F1-F2 vertical offset
			double value;
			char reserved[32];
		} math_v_offset[4];
		uint32_t math_wave_length[4];//F1-F4 number of samples
		double math_s_interval[4];	//F1-F4 sampling interval
		int32_t math_codes_per_div;	//F1-F4 codes per division
	};
	#pragma pack(pop)

protected:
	void OnFileNameChanged();
};

#endif
