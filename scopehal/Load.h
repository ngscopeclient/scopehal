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

#ifndef Load_h
#define Load_h

/**
	@brief A generic electronic load
 */
class Load : public virtual Instrument
{
public:
	Load();
	virtual ~Load();

	virtual unsigned int GetInstrumentTypes();

	//TODO: This should become a virtual that's used by Oscilloscope etc too?
	void AcquireData();

	//New object model does not have explicit query methods for channel properties.
	//Instead, call AcquireData() then read scalar channel state

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Operating modes

	enum LoadMode
	{
		MODE_CONSTANT_CURRENT,
		MODE_CONSTANT_VOLTAGE,
		MODE_CONSTANT_RESISTANCE,
		MODE_CONSTANT_POWER
	};

	/**
		@brief Returns the operating mode of the load
	 */
	virtual LoadMode GetLoadMode(size_t channel) =0;

	/**
		@brief Sets the operating mode of the load
	 */
	virtual void SetLoadMode(size_t channel, LoadMode mode) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Range selection

	/**
		@brief Returns a sorted list of operating ranges for the load's current scale, in amps

		For example, returning [1, 10] means the load supports one mode with 1A full scale range and one with 10A range.
	 */
	virtual std::vector<float> GetLoadCurrentRanges(size_t channel) =0;

	/**
		@brief Returns the index of the load's selected current range, as returned by GetLoadCurrentRanges()
	 */
	virtual size_t GetLoadCurrentRange(size_t channel) =0;

	/**
		@brief Returns a sorted list of operating ranges for the load's voltage scale, in volts

		For example, returning [10, 250] means the load supports one mode with 10V full scale range and one with
		250V range.
	 */
	virtual std::vector<float> GetLoadVoltageRanges(size_t channel) =0;

	/**
		@brief Returns the index of the load's selected voltage range, as returned by GetLoadVoltageRanges()
	 */
	virtual size_t GetLoadVoltageRange(size_t channel) =0;

	//TODO: setters for range

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Channel control

	/**
		@brief Returns true if the load is enabled (sinking power) and false if disabled (no load)
	 */
	virtual bool GetLoadActive(size_t channel) =0;

	/**
		@brief Turns the load on or off
	 */
	virtual void SetLoadActive(size_t channel, bool active) =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Readback
	// Typically called by AcquireData() and cached in the channel object, not used directly by applications

protected:

	/**
		@brief Get the measured voltage of the load
	 */
	virtual float GetLoadVoltageActual(size_t channel) =0;

	/**
		@brief Get the measured current of the load
	 */
	virtual float GetLoadCurrentActual(size_t channel) =0;
};

#endif
