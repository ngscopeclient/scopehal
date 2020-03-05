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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of Measurement
 */

#ifndef Measurement_h
#define Measurement_h

class Measurement
{
public:
	Measurement();
	virtual ~Measurement();

	virtual bool Refresh() =0;

	virtual std::string GetValueAsString() =0;

	//Channels
	size_t GetInputCount();
	std::string GetInputName(size_t i);
	void SetInput(size_t i, OscilloscopeChannel* channel);
	void SetInput(std::string name, OscilloscopeChannel* channel);

	OscilloscopeChannel* GetInput(size_t i);

	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel) =0;

	//Type of measurement (used to determine the submenu to display it under)
	enum MeasurementType
	{
		MEAS_VERT,	//basic vertical axis
		MEAS_HORZ	//basic horizontal axis
	};

	virtual MeasurementType GetMeasurementType() =0;

	/**
		@brief Gets the display name of this protocol (for use in menus, save files, etc). Must be unique.
	 */
	virtual std::string GetMeasurementDisplayName() =0;

	/**
		@brief Serialize this measurement's configuration to a string
	 */
	virtual std::string SerializeConfiguration(std::map<void*, int>& idmap, int& nextID, std::string nick);

protected:

	///Names of signals we take as input
	std::vector<std::string> m_signalNames;

	///The channels corresponding to our signals
	std::vector<OscilloscopeChannel*> m_channels;

public:
	//Helpers for superresolution
	static float InterpolateTime(AnalogCapture* cap, size_t a, float voltage);

	//Enumeration / factory
public:
	typedef Measurement* (*CreateProcType)();
	static void AddMeasurementClass(std::string name, CreateProcType proc);

	static void EnumMeasurements(std::vector<std::string>& names);
	static Measurement* CreateMeasurement(std::string measurement);

protected:
	//Helpers for more complex measurements
	//TODO: create some process for caching this so we don't waste CPU time
	float GetMinVoltage(AnalogCapture* cap);
	float GetMaxVoltage(AnalogCapture* cap);
	float GetBaseVoltage(AnalogCapture* cap);
	float GetTopVoltage(AnalogCapture* cap);
	float GetAvgVoltage(AnalogCapture* cap);
	float GetPeriod(AnalogCapture* cap);
	float GetRiseTime(AnalogCapture* cap, float low, float high);
	float GetFallTime(AnalogCapture* cap, float low, float high);
	std::vector<size_t> MakeHistogram(AnalogCapture* cap, float low, float high, size_t bins);

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;
};

//Helper class for floating point measurements
class FloatMeasurement : public Measurement
{
public:
	//The type of quantity we're measuring
	enum FloatMeasurementType
	{
		TYPE_VOLTAGE,
		TYPE_TIME,
		TYPE_FREQUENCY,
		TYPE_BAUD,
		TYPE_PERCENTAGE
	};

	FloatMeasurement(FloatMeasurementType type);
	virtual ~FloatMeasurement();

	float GetValue()
	{ return m_value; }

	virtual std::string GetValueAsString();

	FloatMeasurementType GetFloatMeasurementType()
	{ return m_type; }

protected:
	float m_value;

	FloatMeasurementType m_type;
};

#define MEASUREMENT_INITPROC(T) \
	static Measurement* CreateInstance() \
	{ return new T; } \
	virtual std::string GetMeasurementDisplayName() \
	{ return GetMeasurementName(); }

#endif
