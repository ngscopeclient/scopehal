/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef Multimeter_h
#define Multimeter_h

/**
	@brief A multimeter

	The distinction between multimeters and oscilloscopes can be blurry at times. For the moment, libscopehal considers
	an instrument a meter if it outputs a scalar, and an oscilloscope if it outputs a vector, regardless of sample rate
	or resolution.
 */
class Multimeter : public virtual Instrument
{
public:
	Multimeter();
	virtual ~Multimeter();

	enum MeasurementTypes
	{
		NONE				= 0x00,

		DC_VOLTAGE			= 0x01,
		DC_RMS_AMPLITUDE	= 0x02,
		AC_RMS_AMPLITUDE	= 0x04,
		FREQUENCY			= 0x08,
		DC_CURRENT			= 0x10,
		AC_CURRENT			= 0x20,
		TEMPERATURE			= 0x40

		//TODO: other types
	};

	virtual unsigned int GetMeasurementTypes() =0;
	virtual unsigned int GetSecondaryMeasurementTypes();

	//Channel info
	virtual int GetCurrentMeterChannel() =0;
	virtual void SetCurrentMeterChannel(int chan) =0;

	//Meter operating mode
	virtual MeasurementTypes GetMeterMode() =0;
	virtual MeasurementTypes GetSecondaryMeterMode();
	virtual std::string ModeToText(MeasurementTypes type);
	MeasurementTypes TextToMode(const std::string& mode);
	virtual void SetMeterMode(MeasurementTypes type) =0;
	virtual void SetSecondaryMeterMode(MeasurementTypes type);

	//Control
	virtual void SetMeterAutoRange(bool enable) =0;
	virtual bool GetMeterAutoRange() =0;
	virtual void StartMeter() =0;
	virtual void StopMeter() =0;

	/**
		@brief Get the current primary measurement unit
	 */
	virtual Unit GetMeterUnit();

	/**
		@brief Get the current secondary measurement unit
	 */
	virtual Unit GetSecondaryMeterUnit();

	/**
		@brief Get the value of the primary measurement
	 */
	virtual double GetMeterValue() =0;

	/**
		@brief Get the value of the secondary measurement
	 */
	virtual double GetSecondaryMeterValue();

	/**
		@brief Returns the digit resolution of the meter

		Values are rounded up for display, for example a 5 3/4 digit meter should return 6 here.
	 */
	virtual int GetMeterDigits() =0;

	virtual bool AcquireData() override;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Configuration storage

protected:
	/**
		@brief Serializes this multimeter's configuration to a YAML node.
	 */
	void DoSerializeConfiguration(YAML::Node& node, IDTable& table);

	/**
		@brief Load instrument and channel configuration from a save file
	 */
	void DoLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap);

	/**
		@brief Validate instrument and channel configuration from a save file
	 */
	void DoPreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& list);
};

#endif
