/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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

#ifndef RohdeSchwarzHMC8012Multimeter_h
#define RohdeSchwarzHMC8012Multimeter_h

/**
	@brief A Rohde & Schwarz HMC8012 multimeter
 */
class RohdeSchwarzHMC8012Multimeter
	: public virtual SCPIMultimeter
{
public:
	RohdeSchwarzHMC8012Multimeter(SCPITransport* transport);
	virtual ~RohdeSchwarzHMC8012Multimeter();

	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	virtual unsigned int GetMeasurementTypes() override;
	virtual unsigned int GetSecondaryMeasurementTypes() override;

	//Channel info
	virtual int GetCurrentMeterChannel() override;
	virtual void SetCurrentMeterChannel(int chan) override;

	//Meter operating mode
	virtual MeasurementTypes GetMeterMode() override;
	virtual MeasurementTypes GetSecondaryMeterMode() override;
	virtual void SetMeterMode(MeasurementTypes type) override;
	virtual void SetSecondaryMeterMode(MeasurementTypes type) override;

	//Control
	virtual void SetMeterAutoRange(bool enable) override;
	virtual bool GetMeterAutoRange() override;
	virtual void StartMeter() override;
	virtual void StopMeter() override;

	virtual int GetMeterDigits() override;

	//Get readings
	virtual double GetMeterValue() override;
	virtual double GetSecondaryMeterValue() override;

protected:
	bool m_modeValid;
	bool m_secmodeValid;
	bool m_dmmAutorangeValid;
	bool m_dmmAutorange;
	MeasurementTypes m_mode;
	MeasurementTypes m_secmode;

public:
	static std::string GetDriverNameInternal();
	METER_INITPROC(RohdeSchwarzHMC8012Multimeter)
};

#endif
