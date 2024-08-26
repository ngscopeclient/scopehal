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

#ifndef SwitchMatrix_h
#define SwitchMatrix_h

/**
	@brief A generic power supply
 */
class SwitchMatrix : public virtual Instrument
{
public:
	SwitchMatrix();
	virtual ~SwitchMatrix();

	virtual unsigned int GetInstrumentTypes() const override;

	/**
		@brief Sets the mux selector for an output channel
	 */
	virtual void SetMuxPath(size_t dstchan, size_t srcchan) =0;

	/**
		@brief Removes a mux path for an output channel

		Not all switch matrices or ports support this feature.
	 */
	virtual void SetMuxPathOpen(size_t dstchan) =0;

	/**
		@brief Checks if an output channel has configurable voltage level
	 */
	virtual bool MuxHasConfigurableDrive(size_t dstchan) =0;

	/**
		@brief Gets the drive level of an output channel
	 */
	virtual float GetMuxOutputDrive(size_t dstchan) =0;

	 /**
		@brief Sets the drive level of an output channel
	 */
	virtual void SetMuxOutputDrive(size_t dstchan, float v) =0;

	/**
		@brief Checks if an input channel has configurable voltage level
	 */
	virtual bool MuxHasConfigurableThreshold(size_t dstchan) =0;

	/**
		@brief Gets the threshold level of an input channel
	 */
	virtual float GetMuxInputThreshold(size_t dstchan) =0;

	 /**
		@brief Sets the threshold level of an input channel
	 */
	virtual void SetMuxInputThreshold(size_t dstchan, float v) =0;

protected:
	/**
		@brief Serializes this oscilloscope's configuration to a YAML node.
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
