/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
#ifndef PowerSupply_h
#define PowerSupply_h

/**
	@brief A generic power supply
 */
class PowerSupply : public virtual Instrument
{
public:
	PowerSupply();
	virtual ~PowerSupply();

	//Channel info
	virtual int GetPowerChannelCount() =0;
	virtual std::string GetPowerChannelName(int chan) =0;

	//Read sensors
	virtual double GetPowerVoltageActual(int chan) =0;				//actual voltage after current limiting
	virtual double GetPowerVoltageNominal(int chan) =0;				//set point
	virtual double GetPowerCurrentActual(int chan) =0;				//actual current drawn by the load
	virtual double GetPowerCurrentNominal(int chan) =0;				//current limit
	virtual bool GetPowerChannelActive(int chan) =0;

	//Configuration
	virtual bool GetPowerOvercurrentShutdownEnabled(int chan) =0;	//shut channel off entirely on overload,
																	//rather than current limiting
	virtual void SetPowerOvercurrentShutdownEnabled(int chan, bool enable) =0;
	virtual bool GetPowerOvercurrentShutdownTripped(int chan) =0;
	virtual void SetPowerVoltage(int chan, double volts) =0;
	virtual void SetPowerCurrent(int chan, double amps) =0;
	virtual void SetPowerChannelActive(int chan, bool on) =0;

	virtual bool IsPowerConstantCurrent(int chan) =0;				//true = CC, false = CV

	virtual bool GetMasterPowerEnable() =0;
	virtual void SetMasterPowerEnable(bool enable) = 0;
};

#endif
