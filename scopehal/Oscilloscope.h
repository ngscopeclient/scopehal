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
	@brief Declaration of Oscilloscope
 */

#ifndef Oscilloscope_h
#define Oscilloscope_h

class Instrument;

#include "SCPITransport.h"

/**
	@brief Generic representation of an oscilloscope or logic analyzer.

	An Oscilloscope contains triggering logic and one or more OscilloscopeChannel objects.
 */
class Oscilloscope : public virtual Instrument
{
public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Construction / destruction

	Oscilloscope();
	virtual ~Oscilloscope();

	/**
		@brief Returns the instrument's identification string.

		This function MUST NOT CACHE the return value and is safe to use as a barrier synchronization to ensure that
		the instrument has received and processed all previous commands.
	 */
	virtual std::string IDPing() =0;

	/**
		@brief Instruments are allowed to cache configuration settings to reduce round trip queries to the device.

		In order to see updates made by the user at the front panel, the cache must be flushed.

		Cache flushing is recommended every second or so during interactive operation.
		In scripted/ATE environments where nobody should be touching the instrument, longer intervals may be used.

		The default implementation of this function does nothing since the base class provides no caching.
		If a derived class caches configuration, it should override this function to clear any cached data.
	 */
	virtual void FlushConfigCache();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Channel information

	/**
		@brief Gets the number of channels this instrument has.

		Only hardware acquisition channels (analog or digital) are included, not math/memory.

		Note that since external trigger inputs have some of the same settings as acquisition channels, they are
		included in the channel count. Call OscilloscopeChannel::GetType() on a given channel to see what kind
		of channel it is.
	 */
	size_t GetChannelCount();

	/**
		@brief Gets a channel by index

		@param i Zero-based index of channel
	 */
	OscilloscopeChannel* GetChannel(size_t i);

	/**
		@brief Gets a channel given the display name
	 */
	OscilloscopeChannel* GetChannelByDisplayName(std::string name);

	/**
		@brief Gets a channel given the hardware name
	 */
	OscilloscopeChannel* GetChannelByHwName(std::string name);

	/**
		@brief Checks if a channel is enabled in hardware.
	 */
	virtual bool IsChannelEnabled(size_t i) =0;

	/**
		@brief Turn a channel on, given the index

		@param i Zero-based index of channel
	 */
	virtual void EnableChannel(size_t i) =0;

	/**
		@brief Turn a channel off, given the index.

		This function may optionally configure channel interleaving, if supported in hardware.

		@param i Zero-based index of channel
	 */
	virtual void DisableChannel(size_t i) =0;

	/**
		@brief Gets the coupling used for an input channel

		@param i Zero-based index of channel
	 */
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) =0;

	/**
		@brief Sets the coupling used for an input channel

		@param i Zero-based index of channel
	 */
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) =0;

	/**
		@brief Gets the probe attenuation for an input channel.

		Note that this function returns attenuation, not gain.
		For example, a 10x probe would return 10 and not 0.1.

		@param i Zero-based index of channel
	 */
	virtual double GetChannelAttenuation(size_t i) =0;

	/**
		@brief Sets the probe attenuation used for an input channel.

		@param i		Zero-based index of channel
		@param atten	Attenuation factor
	 */
	virtual void SetChannelAttenuation(size_t i, double atten) =0;

	/**
		@brief Gets the bandwidth limit for an input channel.

		@param i Zero-based index of channel

		@return Bandwidth limit, in MHz. Zero means "no bandwidth limit".
	 */
	virtual int GetChannelBandwidthLimit(size_t i) =0;

	/**
		@brief Sets the bandwidth limit for an input channel.

		@param i			Zero-based index of channel
		@param limit_mhz	Bandwidth limit, in MHz. Zero means "no bandwidth limit".
	 */
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) =0;

	/**
		@brief Returns the external trigger input channel, if we have one.

		Note that some very high end oscilloscopes have multiple external trigger inputs.
		We do not currently support this.
	 */
	virtual OscilloscopeChannel* GetExternalTrigger() =0;

	/**
		@brief Gets the range of the current channel configuration.

		The range is the distance, in volts, between the most negative/smallest and most positive/largest
		voltage which the ADC can represent using the current vertical gain configuration. This can be calculated
		as the number of vertical divisions times the number of volts per division.

		The range does not depend on the offset.

		@param i			Zero-based index of channel
	 */
	virtual double GetChannelVoltageRange(size_t i) =0;

	/**
		@brief Sets the range of the current channel configuration.

		The range is the distance, in volts, between the most negative/smallest and most positive/largest
		voltage which the ADC can represent using the current vertical gain configuration. This can be calculated
		as the number of vertical divisions times the number of volts per division.

		The range does not depend on the offset.

		@param i			Zero-based index of channel
		@param range		Voltage range
	 */
	virtual void SetChannelVoltageRange(size_t i, double range) =0;

	/**
		@brief Gets the offset, in volts, for a given channel

		@param i			Zero-based index of channel
	 */
	virtual double GetChannelOffset(size_t i) =0;

	/**
		@brief Sets the offset for a given channel

		@param i			Zero-based index of channel
		@param offset		Offset, in volts
	 */
	virtual void SetChannelOffset(size_t i, double offset) =0;

	//Triggering
	enum TriggerMode
	{
		///Active, waiting for a trigger event
		TRIGGER_MODE_RUN,

		///Triggered once, but not recently
		TRIGGER_MODE_STOP,

		///Just got triggered, data is ready to read
		TRIGGER_MODE_TRIGGERED,

		///WAIT - waiting for something (not sure what this means, some Rigol scopes use it?)
		TRIGGER_MODE_WAIT,

		///Auto trigger - waiting for auto-trigger
		TRIGGER_MODE_AUTO,

		///Placeholder
		TRIGGER_MODE_COUNT
	};

	/**
		@brief Checks the curent trigger status.
	 */
	virtual Oscilloscope::TriggerMode PollTrigger() =0;

	/**
		@brief Block until a trigger happens or a timeout elapses.

		Note that this function has no provision to dispatch any UI events etc.
		It's intended as a convenience helper for non-interactive ATE applications only.

		@param timeout	Timeout value, in milliseconds

		@return True if triggered, false if timeout
	 */
	bool WaitForTrigger(int timeout);

	/**
		@brief Sets a new trigger on the instrument and pushes changes.

		Calling SetTrigger() with the currently selected trigger is legal and is equivalent to calling PushTrigger().

		Ownership of the trigger object is transferred to the Oscilloscope.
	 */
	void SetTrigger(Trigger* trigger)
	{
		//If we have an old trigger that's not the same, free it
		if(m_trigger != trigger)
			delete m_trigger;

		//Set the new trigger and sync to hardware
		m_trigger = trigger;
		PushTrigger();
	}

	/**
		@brief Pushes changes made to m_trigger to the instrument
	 */
	virtual void PushTrigger() =0;

	/**
		@brief Gets the current trigger.

		Ownership of the trigger object is retained by the Oscilloscope. This pointer may be invalidated by any future
		call to GetTrigger() or PullTrigger().
	 */
	Trigger* GetTrigger(bool sync = true)
	{
		if(sync || (m_trigger == NULL) )
			PullTrigger();
		return m_trigger;
	}

	/**
		@brief Updates m_trigger with any changes made from the instrument side
	 */
	virtual void PullTrigger() =0;

	/**
		@brief Reads a waveform into the queue of pending waveforms
	 */
	virtual bool AcquireData() =0;

	/**
		@brief Starts the instrument in continuous trigger mode.

		This is normally not used for data-download applications, because of the risk of race conditions where the
		instrument triggers during AcquireData() leading to some channels having stale and some having new data.
	 */
	virtual void Start() =0;

	/**
		@brief Arms the trigger for a single acquistion.
	 */
	virtual void StartSingleTrigger() =0;

	/**
		@brief Stops triggering
	 */
	virtual void Stop() =0;

	/**
		@brief Checks if the trigger is currently armed
	 */
	virtual bool IsTriggerArmed() =0;

	/**
		@brief Enables the trigger output, configuring a shared auxiliary port for this purpose if needed

		The default implementation does nothing, and is intended for instruments where the trigger output is always
		enabled.
	 */
	virtual void EnableTriggerOutput();

	/**
		@brief Gets the connection string for our transport
	 */
	virtual std::string GetTransportConnectionString() =0;

	/**
		@brief Gets the name of our transport
	 */
	virtual std::string GetTransportName() =0;

public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Memory depth / sample rate control.

	/**
		@brief Get the legal sampling rates (in Hz) for this scope in all-channels mode
	 */
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() =0;

	/**
		@brief Get the legal sampling rates (in Hz) for this scope in combined-channels mode
	 */
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() =0;

	/**
		@brief Gets the current sampling rate (in Hz) of this scope
	 */
	virtual uint64_t GetSampleRate() =0;

	/**
		@brief Checks if the scope is currently combining channels
	 */
	virtual bool IsInterleaving() =0;

	/**
		@brief Configures the scope to combine channels.

		This function may fail to enable channel combining if conflicts are present, check the return value!

		@return True if channel combining is enabled, false if not
	 */
	virtual bool SetInterleaving(bool combine) =0;

	/**
		@brief Returns true if we have no interleave conflicts, false if we have conflicts
	 */
	virtual bool CanInterleave();

	/**
		@brief Get the set of conflicting channels.

		If any pair of channels in this list is enabled, channel interleaving is not possible.
	 */
	typedef std::pair<OscilloscopeChannel*, OscilloscopeChannel* > InterleaveConflict;
	virtual std::set< InterleaveConflict > GetInterleaveConflicts() =0;

	/**
		@brief Get the legal memory depths for this scope in all-channels mode
	 */
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() =0;

	/**
		@brief Get the legal memory depths for this scope in combined-channels mode
	 */
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() =0;

	/**
		@brief Gets the current sample depth of this scope
	 */
	virtual uint64_t GetSampleDepth() =0;

	/**
		@brief Sets the sample depth of the scope
	 */
	virtual void SetSampleDepth(uint64_t depth) =0;

	/**
		@brief Sets the sample rate of the scope, in Hz
	 */
	virtual void SetSampleRate(uint64_t rate) =0;

	/**
		@brief Configures the instrument's clock source

		@param external		True to use external reference
							False to use internal clock

		The default implementation prints an "unsupported operation" warning, and is suitable for lower-end
		instruments that do not support external clock inputs.
	 */
	virtual void SetUseExternalRefclk(bool external);

	/**
		@brief Sets the trigger offset

		@param offset		Picoseconds from the start of the capture to the trigger point.
							Positive values mean the trigger is within the waveform.
							Negative values mean there is a delay from the trigger point to the start of the waveform.
	 */
	virtual void SetTriggerOffset(int64_t offset)	=0;

	/**
		@brief Gets the trigger offset
	 */
	virtual int64_t GetTriggerOffset() =0;

	/**
		@brief Sets the deskew setting for a channel

		@param channel		The channel to deskew
		@param skew			Skew value, in picoseconds.
							Negative values move the channel earlier relative to the zero point.
							Positive values move the channel later.

		The default implementation does nothing, and is suitable for lower-end instruments that do not support deskew.
	 */
	virtual void SetDeskewForChannel(size_t channel, int64_t skew);

	/**
		@brief Gets the deskew setting for a channel

		The default implementation returns zero, and is suitable for lower-end instruments that do not support deskew.
	 */
	virtual int64_t GetDeskewForChannel(size_t channel);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Sequenced triggering

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Logic analyzer configuration

	typedef std::vector<OscilloscopeChannel*> DigitalBank;

	/**
		@brief Gets the digital channel banks for this instrument.

		A bank is a set of one or more channels all sharing a common threshold and hysteresis setting.
	 */
	virtual std::vector<DigitalBank> GetDigitalBanks();

	/**
		@brief Gets the bank containing a given channel
	 */
	virtual DigitalBank GetDigitalBank(size_t channel);

	/**
		@brief Checks if digital input hysteresis is configurable or fixed.

		@return true if configurable, false if fixed
	 */
	virtual bool IsDigitalHysteresisConfigurable();

	/**
		@brief Checks if digital input threshold is configurable or fixed.

		@return true if configurable, false if fixed
	 */
	virtual bool IsDigitalThresholdConfigurable();

	/**
		@brief Gets the hysteresis for a digital input
	 */
	virtual float GetDigitalHysteresis(size_t channel);

	/**
		@brief Gets the threshold for a digital input
	 */
	virtual float GetDigitalThreshold(size_t channel);

	/**
		@brief Sets the hysteresis for a digital input
	 */
	virtual void SetDigitalHysteresis(size_t channel, float level);

	/**
		@brief Gets the threshold for a digital input
	 */
	virtual void SetDigitalThreshold(size_t channel, float level);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Configuration storage

	/**
		@brief Serializes this oscilloscope's configuration to a YAML string.

		@return YAML block with this oscilloscope's configuration
	 */
	virtual std::string SerializeConfiguration(IDTable& table);

	/**
		@brief Load instrument and channel configuration from a save file
	 */
	virtual void LoadConfiguration(const YAML::Node& node, IDTable& idmap);

public:
	bool HasPendingWaveforms();
	void ClearPendingWaveforms();
	size_t GetPendingWaveformCount();
	virtual bool PopPendingWaveform();

protected:
	typedef std::map<OscilloscopeChannel*, WaveformBase*> SequenceSet;
	std::list<SequenceSet> m_pendingWaveforms;
	std::mutex m_pendingWaveformsMutex;
	std::recursive_mutex m_mutex;

protected:

	///The channels
	std::vector<OscilloscopeChannel*> m_channels;

	//The trigger
	Trigger* m_trigger;

public:
	typedef Oscilloscope* (*CreateProcType)(SCPITransport*);
	static void DoAddDriverClass(std::string name, CreateProcType proc);

	static void EnumDrivers(std::vector<std::string>& names);
	static Oscilloscope* CreateOscilloscope(std::string driver, SCPITransport* transport);

	virtual std::string GetDriverName() =0;
	//static std::string GetDriverNameInternal();

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;
};

#define OSCILLOSCOPE_INITPROC(T) \
	static Oscilloscope* CreateInstance(SCPITransport* transport) \
	{ \
		return new T(transport); \
	} \
	virtual std::string GetDriverName() \
	{ return GetDriverNameInternal(); }

#define AddDriverClass(T) Oscilloscope::DoAddDriverClass(T::GetDriverNameInternal(), T::CreateInstance)

#endif
