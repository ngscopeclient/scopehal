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

#include "../scopehal/scopehal.h"
#include "WFMImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WFMImportFilter::WFMImportFilter(const string& color)
	: ImportFilter(color)
{
	m_fpname = "WFM File";
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.wfm";
	m_parameters[m_fpname].m_fileFilterName = "Tektronix WFM files (*.wfm)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &WFMImportFilter::OnFileNameChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string WFMImportFilter::GetProtocolName()
{
	return "WFM Import";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void WFMImportFilter::OnFileNameChanged()
{
	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	LogDebug("Reading WFM file %s\n", fname.c_str());
	LogIndenter li;

	//Set waveform timestamp to file timestamp
	time_t timestamp = 0;
	int64_t fs = 0;
	GetTimestampOfFile(fname, timestamp, fs);

	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("Couldn't open WFM file \"%s\"\n", fname.c_str());
		return;
	}

	//Byte order check (expect 0x0f0f)
	uint16_t byteswap;
	if(1 != fread(&byteswap, sizeof(byteswap), 1, fp))
	{
		LogError("Fail to read byte order mark\n");
		fclose(fp);
		return;
	}
	if(byteswap == 0xf0f0)
	{
		LogError("Byteswapped files not supported\n");
		fclose(fp);
		return;
	}
	if(byteswap != 0x0f0f)
	{
		LogError("Invalid magic number\n");
		fclose(fp);
		return;
	}

	//Version number (expect ":WFM#003" file format version for now)
	char version[9] = {0};
	if(8 != fread(version, 1, 8, fp))
	{
		LogError("Fail to read version number\n");
		fclose(fp);
		return;
	}
	LogDebug("Waveform version:     \"%s\"\n", version);
	if(strcmp(version, ":WFM#003") != 0)
	{
		LogError("Don't know what to do with file format \"%s\", expected version 3\n", version);
		fclose(fp);
		return;
	}

	//Number of digits in ascii byte counts? not entirely sure what this is for
	uint8_t ndigits;
	if(1 != fread(&ndigits, 1, 1, fp))
	{
		LogError("Fail to read digit count\n");
		fclose(fp);
		return;
	}
	LogDebug("Digit count:          %d\n", ndigits);

	//Get file size (from this point)
	uint32_t filesize;
	if(1 != fread(&filesize, sizeof(filesize), 1, fp))
	{
		LogError("Fail to read file size\n");
		fclose(fp);
		return;
	}
	LogDebug("File size:            %d bytes\n", filesize);

	uint8_t bytesperpoint;
	if(1 != fread(&bytesperpoint, 1, 1, fp))
	{
		LogError("Fail to read bytes per point\n");
		fclose(fp);
		return;
	}
	LogDebug("Bytes per point:      %d\n", bytesperpoint);
	if(bytesperpoint != 2)
	{
		LogError("Only 2 bytes per point supported for now\n");
		fclose(fp);
		return;
	}

	//Offset to start of curve buffer (from start of file)
	uint32_t curveoffset;
	if(1 != fread(&curveoffset, sizeof(curveoffset), 1, fp))
	{
		LogError("Fail to read curve offset\n");
		fclose(fp);
		return;
	}
	LogDebug("Curve data offset:    %d bytes\n", curveoffset);

	//Skip some fields we don't care about, not interesting
	//int32			Horizontal zoom scale
	//float32		Horizontal zoom position
	//float64		Vertical zoom scale
	//float32		Vertical zoom position
	fseek(fp, 20, SEEK_CUR);

	//Waveform label (may be blank)
	char wfmLabel[33] = {0};
	if(32 != fread(wfmLabel, 1, 32, fp))
	{
		LogError("Fail to read waveform label\n");
		fclose(fp);
		return;
	}
	LogDebug("Waveform label:       %s\n", wfmLabel);

	//Number of curve objects
	int32_t numFrames;
	if(1 != fread(&numFrames, sizeof(numFrames), 1, fp))
	{
		LogError("Fail to read num frames\n");
		fclose(fp);
		return;
	}
	LogDebug("Curve objects:        %d\n", numFrames);

	//Size of waveform header
	int16_t wfmHeaderSize;
	if(1 != fread(&wfmHeaderSize, sizeof(wfmHeaderSize), 1, fp))
	{
		LogError("Fail to read waveform header size\n");
		fclose(fp);
		return;
	}
	LogDebug("Waveform header size: %d\n", wfmHeaderSize);

	//Waveform dataset type
	int32_t datasetType;
	if(1 != fread(&datasetType, sizeof(datasetType), 1, fp))
	{
		LogError("Fail to read waveform header size\n");
		fclose(fp);
		return;
	}
	if(datasetType == 1)
	{
		LogDebug("Dataset type:         FastFrame\n");
		LogError("FastFrame dataset type not supported\n");
		fclose(fp);
		return;
	}
	else if(datasetType == 0)
		LogDebug("Dataset type:         Normal\n");
	else
	{
		LogError("Unrecognized dataset type %d\n", datasetType);
		fclose(fp);
		return;
	}

	//Number of waveforms in the dataset
	int32_t wfmCnt;
	if(1 != fread(&wfmCnt, sizeof(wfmCnt), 1, fp))
	{
		LogError("Fail to read waveform count\n");
		fclose(fp);
		return;
	}
	LogDebug("Waveform count:       %d\n", wfmCnt);

	//Skip some fields we don't care about
	//int64		acquisitionCount
	//int64		transactionCount
	//int32		SlotID
	//int32		StaticFlag
	fseek(fp, 24, SEEK_CUR);

	//Update spec count
	int32_t updateSpecCount;
	if(1 != fread(&updateSpecCount, sizeof(updateSpecCount), 1, fp))
	{
		LogError("Fail to read update spec count\n");
		fclose(fp);
		return;
	}
	LogDebug("Update spec count:    %d\n", updateSpecCount);

	//Implicit dimension count
	int32_t implicitDimensionCount;
	if(1 != fread(&implicitDimensionCount, sizeof(implicitDimensionCount), 1, fp))
	{
		LogError("Fail to read implicit dimension count\n");
		fclose(fp);
		return;
	}
	LogDebug("Implicit dim count:   %d\n", implicitDimensionCount);
	if(implicitDimensionCount != 1)
	{
		LogError("Expected 1 implicit dimension (for waveform dataset\n");
		fclose(fp);
		return;
	}

	//Explicit dimension count
	int32_t explicitDimensionCount;
	if(1 != fread(&explicitDimensionCount, sizeof(explicitDimensionCount), 1, fp))
	{
		LogError("Fail to read explicit dimension count\n");
		fclose(fp);
		return;
	}
	LogDebug("Explicit dim count:   %d\n", explicitDimensionCount);
	if(explicitDimensionCount != 1)
	{
		LogError("Expected 1 explicit dimension (for waveform dataset\n");
		fclose(fp);
		return;
	}

	//Waveform data type
	int32_t dataType;
	if(1 != fread(&dataType, sizeof(dataType), 1, fp))
	{
		LogError("Fail to read data type\n");
		fclose(fp);
		return;
	}
	if(dataType == 2)
		LogDebug("Data type:            vector\n");
	else
	{
		LogError("Unknown waveform data type %d\n", dataType);
		fclose(fp);
		return;
	}

	//Skip fields we don't care about
	//int64		counter
	//int32		accumcount
	//int32		targetcount
	fseek(fp, 16, SEEK_CUR);

	//Number of curve objects
	int32_t curveCount;
	if(1 != fread(&curveCount, sizeof(curveCount), 1, fp))
	{
		LogError("Fail to read curve count\n");
		fclose(fp);
		return;
	}
	if(curveCount != 1)
	{
		LogError("Invalid curve count %d\n", curveCount);
		fclose(fp);
		return;
	}

	//Skip fields we don't care about
	//int32		requestedFastFrames
	//int32		acquiredFastFrames
	//int16		summary
	//int32		pixmapFormat
	//int64		pixmapMax
	fseek(fp, 22, SEEK_CUR);

	//Explicit dimensions
	//(assume only one is present for now)
	double yscale;
	if(1 != fread(&yscale, sizeof(yscale), 1, fp))
	{
		LogError("Fail to read Y axis scale\n");
		fclose(fp);
		return;
	}
	LogDebug("Y axis scale:         %f\n", yscale);
	double yoff;
	if(1 != fread(&yoff, sizeof(yscale), 1, fp))
	{
		LogError("Fail to read Y axis scale\n");
		fclose(fp);
		return;
	}
	LogDebug("Y axis offset:        %f\n", yoff);
	int32_t yDataRange;
	if(1 != fread(&yDataRange, sizeof(yDataRange), 1, fp))
	{
		LogError("Fail to read Y axis range\n");
		fclose(fp);
		return;
	}
	LogDebug("Y axis range:         %d\n", yDataRange);
	char yunits[21] = {0};
	if(20 != fread(yunits, 1, 20, fp))
	{
		LogError("Fail to read Y axis units\n");
		fclose(fp);
		return;
	}
	LogDebug("Y axis units:         %s\n", yunits);

	//Skip fields we don't care about
	//float64	minPossibleValue
	//float64	maxPossibleValue
	//float64	resolution
	//float64	refPoint
	fseek(fp, 32, SEEK_CUR);

	//Format
	int32_t format;
	if(1 != fread(&format, sizeof(format), 1, fp))
	{
		LogError("Fail to read data format\n");
		fclose(fp);
		return;
	}
	if(format == 0)
		LogDebug("Data format:          int16_t\n");
	else
	{
		LogError("Data format:          %d (unimplemented)\n", format);
		fclose(fp);
		return;
	}

	//Data layout
	int32_t layout;
	if(1 != fread(&layout, sizeof(layout), 1, fp))
	{
		LogError("Fail to read data layout\n");
		fclose(fp);
		return;
	}
	if(layout == 0)
		LogDebug("Data layout:          sample\n");
	else
	{
		LogError("Data layout:          %d (unimplemented)\n", layout);
		fclose(fp);
		return;
	}

	//Skip fields we don't care about
	//int32		nanval
	//int32		overrange
	//int32		underange
	//int32		highrange
	//int32		lowrange
	//float64	viewscale
	//char[20]	viewscaleUnits
	//float64	userOffset
	//float64	pointDensity
	//float64	triggerPositionPercent
	//float64	triggerDelay
	fseek(fp, 80, SEEK_CUR);

	//Skip over the second explicit dimension
	//(space is reserved in the file format even if the dimension is not present)
	fseek(fp, 160, SEEK_CUR);

	//Implicit dimensions
	//(assume only one is present for now)
	double xscale;
	if(1 != fread(&xscale, sizeof(xscale), 1, fp))
	{
		LogError("Fail to read X axis scale\n");
		fclose(fp);
		return;
	}
	LogDebug("X axis scale:         %e\n", xscale);
	double xoff;
	if(1 != fread(&xoff, sizeof(xscale), 1, fp))
	{
		LogError("Fail to read X axis scale\n");
		fclose(fp);
		return;
	}
	LogDebug("X axis offset:        %f\n", xoff);
	int32_t numPoints;
	if(1 != fread(&numPoints, sizeof(numPoints), 1, fp))
	{
		LogError("Fail to read record length\n");
		fclose(fp);
		return;
	}
	LogDebug("Record length:        %d points\n", numPoints);
	char xunits[21] = {0};
	if(20 != fread(xunits, 1, 20, fp))
	{
		LogError("Fail to read X axis units\n");
		fclose(fp);
		return;
	}
	LogDebug("X axis units:         %s\n", xunits);

	//Skip fields we don't care about
	//float64	extent min
	//float64	extent max
	//float64	resolution
	//float64	refpoint
	fseek(fp, 32, SEEK_CUR);

	int32_t spacing;
	if(1 != fread(&spacing, sizeof(spacing), 1, fp))
	{
		LogError("Fail to read sample spacing\n");
		fclose(fp);
		return;
	}
	LogDebug("X axis spacing:       %d\n", spacing);

	//Skip fields we don't care about
	//float64	user scale
	//char[20]	user units
	//float64	user offset
	//float64	point density
	//float64	href
	//float64	trigdelay
	fseek(fp, 60, SEEK_CUR);

	//Skip over the second implicit dimension
	//(space is reserved in the file format even if the dimension is not present)
	fseek(fp, 136, SEEK_CUR);

	//Timebase information
	int32_t realSpacing;
	if(1 != fread(&realSpacing, sizeof(realSpacing), 1, fp))
	{
		LogError("Fail to read real spacing\n");
		fclose(fp);
		return;
	}
	LogDebug("Real point spacing:   %d\n", realSpacing);

	int32_t acqType;
	if(1 != fread(&acqType, sizeof(acqType), 1, fp))
	{
		LogError("Fail to read acquisition type\n");
		fclose(fp);
		return;
	}
	LogDebug("Acq type:             %d\n", acqType);

	int32_t baseType;
	if(1 != fread(&baseType, sizeof(baseType), 1, fp))
	{
		LogError("Fail to read timebase type\n");
		fclose(fp);
		return;
	}
	LogDebug("Timebase type:        %d\n", baseType);

	//Skip second timebase type
	fseek(fp, 12, SEEK_CUR);

	//Waveform update spec
	//TODO: there can be more than one so we need to loop
	int32_t realPointOffset;
	if(1 != fread(&realPointOffset, sizeof(realPointOffset), 1, fp))
	{
		LogError("Fail to read real point offset\n");
		fclose(fp);
		return;
	}
	LogDebug("Real point offset:    %d\n", realPointOffset);

	double triggerPhase;
	if(1 != fread(&triggerPhase, sizeof(triggerPhase), 1, fp))
	{
		LogError("Fail to read trigger phase\n");
		fclose(fp);
		return;
	}
	LogDebug("Trigger phase:        %f\n", triggerPhase);

	double fracSec;
	if(1 != fread(&fracSec, sizeof(fracSec), 1, fp))
	{
		LogError("Fail to read fractional seconds\n");
		fclose(fp);
		return;
	}
	uint32_t gmtSec;
	if(1 != fread(&gmtSec, sizeof(gmtSec), 1, fp))
	{
		LogError("Fail to read GMT seconds\n");
		fclose(fp);
		return;
	}

	//Waveform curve information
	//Skip fields we don't care about
	//int32 stateFlags
	//int32 checksumType
	//int16 curveChecksum
	fseek(fp, 10, SEEK_CUR);

	uint32_t prechargeStart;
	if(1 != fread(&prechargeStart, sizeof(prechargeStart), 1, fp))
	{
		LogError("Fail to read precharge start\n");
		fclose(fp);
		return;
	}
	LogDebug("Precharge start:      %d\n", prechargeStart);

	uint32_t dataStart;
	if(1 != fread(&dataStart, sizeof(dataStart), 1, fp))
	{
		LogError("Fail to read data start\n");
		fclose(fp);
		return;
	}
	LogDebug("Data start:           %d\n", dataStart);

	uint32_t postchargeStart;
	if(1 != fread(&postchargeStart, sizeof(postchargeStart), 1, fp))
	{
		LogError("Fail to read postcharge start\n");
		fclose(fp);
		return;
	}
	LogDebug("Postcharge start:     %d\n", postchargeStart);

	uint32_t postchargeStop;
	if(1 != fread(&postchargeStop, sizeof(postchargeStop), 1, fp))
	{
		LogError("Fail to read postcharge stop\n");
		fclose(fp);
		return;
	}
	LogDebug("Postcharge stop:      %d\n", postchargeStop);

	//Skip roll mode data
	fseek(fp, 4, SEEK_CUR);

	//Calculate actual sample data size
	size_t numBytes = (postchargeStop - prechargeStart);
	size_t numRealSamples = numBytes / bytesperpoint;
	LogDebug("Actual sample count:  %zu\n", numRealSamples);
	LogDebug("Actual byte count:    %zu\n", numBytes);

	//Create output waveform and stream
	//TODO: handle multi channel etc
	ClearStreams();
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	auto wfm = new UniformAnalogWaveform;
	wfm->m_timescale = FS_PER_SECOND * (spacing+1) * xscale;
	wfm->Resize(numRealSamples);
	wfm->m_startTimestamp = gmtSec;
	wfm->m_startFemtoseconds = fracSec * FS_PER_SECOND;
	wfm->m_triggerPhase = triggerPhase * wfm->m_timescale;
	wfm->PrepareForCpuAccess();
	SetData(wfm, 0);

	//Read sample data
	int16_t* rawdata = new int16_t[numRealSamples];
	fseek(fp, curveoffset, SEEK_SET);
	if(numRealSamples != fread(rawdata, sizeof(int16_t), numRealSamples, fp))
	{
		LogError("Fail to read waveform data\n");
		delete[] rawdata;
		return;
	}

	//Read sample data
	for(size_t i=0; i<numRealSamples; i++)
		wfm->m_samples[i] = (rawdata[i] * yscale) + yoff;

	//Done, set scale
	wfm->MarkModifiedFromCpu();
	AutoscaleVertical(0);

	//Clean up
	delete[] rawdata;
	fclose(fp);
}
