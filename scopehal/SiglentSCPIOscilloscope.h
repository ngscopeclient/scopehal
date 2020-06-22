/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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

#ifndef SiglentSCPIOscilloscope_h
#define SiglentSCPIOscilloscope_h

#include "../xptools/Socket.h"

#include "LeCroyOscilloscope.h"

// temp forward declaration
struct SiglentWaveformDesc_t;

/**
	@brief A Siglent SCPI (SCPI/TCP) oscilloscope

	Protocol layer is based on Siglent's reference manual
	Implementation here modeled off of the LeCroy support in LeCroyVICPOscilloscope.cpp
 */
class SiglentSCPIOscilloscope
	: public LeCroyOscilloscope
{
public:
	SiglentSCPIOscilloscope(SCPITransport* transport);
	virtual ~SiglentSCPIOscilloscope();

	virtual void SetChannelVoltageRange(size_t i, double range);

	virtual bool AcquireData(bool toQueue);

protected:

	void ReadWaveDescriptorBlock(SiglentWaveformDesc_t *descriptor, unsigned int channel);
	int ReadWaveHeader(char *header);

	bool m_acquiredDataIsSigned;
	bool m_hasVdivAttnBug;

public:
	static std::string GetDriverNameInternal();

	OSCILLOSCOPE_INITPROC(SiglentSCPIOscilloscope)

};

#pragma pack(1)

struct SignalWaveformTimestamp_t
{
	double Seconds;
	uint8_t Minutes;
	uint8_t Hours;
	uint8_t Days;
	uint8_t Months;
	uint16_t Years;
	uint16_t Unused;
};

#pragma pack(1)
struct SiglentWaveformDesc_t
{
	// nominally always "WAVEDESC"
	char DescName[16];
	// nominally always "WAVEACE"
	char TemplateName[16];
	// 0: byte, 1: word (error if != 0...)
	uint16_t CommType;
	// 0: big endian, 1: little endian
	uint16_t CommOrder;
	// length of wave descriptor (this block)
	uint32_t WaveDescLen;
	// length of user text block
	uint32_t UserTextLen;
	// length of whatever ResDesc1 is
	uint32_t ResDesc1Len;
	// length of TRIGTIME array
	uint32_t TriggerTimeArrayLen;
	// length of RIS_TIME array
	uint32_t RISTimeArrayLen;
	// weird reserved array
	uint32_t ReservedArrayLen;
	// length of the actual sample data
	uint32_t WaveformArrayLen;
	// length of the second waveform (?)
	uint32_t Waveform2ArrayLen;
	// two reserved entries
	uint32_t ReservedLen1;
	uint32_t ReservedLen2;
	// Instrument name
	char InstrumentName[16];
	uint32_t InstrumentNumber;
	// seems to be garbage
	char TraceLabel[16];
	uint16_t ReservedWord1;
	uint16_t ReservedWord2;
	// Num. points in data array (not bytes!)
	uint32_t WaveArrayCount;
	uint32_t PointsPerScreen;
	uint32_t FirstValidPoint;
	uint32_t LastValidPoint;
	uint32_t FirstPoint;
	uint32_t SparsingFactor;
	uint32_t SegmentIndex;
	uint32_t SubarrayCount;
	uint32_t SweepsPerAcquisition;
	// Apparently used for peak detect
	uint16_t PointsPerPair;
	uint16_t PairOffset;
	float VerticalGain;
	float VerticalOffset;
	float MaximumValue;
	float MinumumValue;
	// scope makes a guess as to bitness...
	uint16_t NominalBits;
	uint16_t NominalSubarrayCount;
	float HorizontalInterval;
	double HorizontalOffset;
	double PixelOffset;
	char VerticalUnit[48];
	char HorizontalUnit[48];
	// jitter between acquisitions
	float HorizontalUncertainty;
	struct SignalWaveformTimestamp_t Timestamp;
	float AcquisitionDuration;
	/*
		0: single sweep
		1: interleaved
		2: histogram
		3: graph
		4: filter coefficient
		5: complex
		6: extrema
		7: sequence (obsolete?)
		8: centered RIS
		9: peak detect
	*/
	uint16_t RecordType;
	/*
		0: no processing
		1: fir filter
		2: interpolated
		3: sparsed
		4: autoscaled
		5: no result (?)
		6: rolling
		7: cumulative
	*/
	uint16_t ProcessingDone;
	uint16_t ReservedWord5;
	uint16_t RISSweeps;
	// enum from 0..35 for 200ps..100s
	// 100 -> external
	uint16_t Timebase;
	/*
		0: DC
		1: AC
		2: GND
	*/
	uint16_t VerticalCoupling;
	float ProbeAttenuation;
	uint16_t FixedVerticalGain;
	/*
		0: off
		1: 20M
		2: 200M
	*/
	uint16_t BandwidthLimit;
	float VerticalVernier;
	float AcquisitionVerticalOffset;
	/*
		0: Chan 1
		1: Chan 2
		2: Chan 3
		3: Chan 4
		9: Unknown
	*/
	uint16_t WaveformSource;
};

#endif
