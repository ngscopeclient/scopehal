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
	@brief Declaration of BINImportFilter
 */
#ifndef BINImportFilter_h
#define BINImportFilter_h

class BINImportFilter : public ImportFilter
{
public:
	BINImportFilter(const std::string& color);

	static std::string GetProtocolName();

	PROTOCOL_DECODER_INITPROC(BINImportFilter)

	//Agilent/Keysight/Rigol binary capture structs
	#pragma pack(push, 1)
	struct FileHeader
	{
		char magic[2];		//File magic string ("AG" / "RG")
		char version[2];	//File format version
		uint32_t length;	//Length of file in bytes
		uint32_t count;		//Number of waveforms
	};

	struct WaveHeader
	{
		uint32_t size;		//Waveform header length (0x8C)
		uint32_t type;		//Waveform type
		uint32_t buffers;	//Number of buffers
		uint32_t samples;	//Number of samples
		uint32_t averaging;	//Averaging count
		float duration;		//Capture duration
		double start;		//Display start time
		double interval;	//Sample time interval
		double origin;		//Capture origin time
		uint32_t x;			//X axis unit
		uint32_t y;			//Y axis unit
		char date[16];		//Capture date
		char time[16];		//Capture time
		char hardware[24];	//Model and serial
		char label[16];		//Waveform label
		double holdoff;		//Trigger holdoff
		uint32_t segment;	//Segment number
	};

	struct DataHeader
	{
		uint32_t size;		//Waveform data header length
		short type;			//Sample data type
		short depth;		//Sample bit depth
		uint32_t length;	//Data buffer length
	};
	#pragma pack(pop)

protected:
	void OnFileNameChanged();
};

#endif
