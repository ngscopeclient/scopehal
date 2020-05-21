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

	enum TriggerType
	{
		TRIGGER_TYPE_LOW		= 0,
		TRIGGER_TYPE_HIGH 		= 1,
		TRIGGER_TYPE_FALLING	= 2,
		TRIGGER_TYPE_RISING		= 3,
		TRIGGER_TYPE_CHANGE		= 4,
		TRIGGER_TYPE_DONTCARE	= 5,
		TRIGGER_TYPE_COMPLEX	= 6	//complex pattern/protocol trigger
	};

	/**
		@brief Gets the index of the channel currently selected for trigger
	 */
	virtual size_t GetTriggerChannelIndex() =0;

	/**
		@brief Sets the scope to trigger on the selected channel

		@param i	Zero-based index of channel to use as trigger
	 */
	virtual void SetTriggerChannelIndex(size_t i) =0;

	/**
		@brief Gets the threshold of the current trigger, in volts
	 */
	virtual float GetTriggerVoltage() =0;

	 /**
		@brief Sets the threshold of the current trigger

		@param v	Trigger threshold, in volts
	 */
	virtual void SetTriggerVoltage(float v) =0;

	/**
		@brief Gets the type of trigger configured for the instrument.

		Complex pattern, dropout, etc triggers are not yet supported.
	 */
	virtual Oscilloscope::TriggerType GetTriggerType() =0;

	/**
		@brief Sets the type of trigger configured for the instrument.

		Complex pattern, dropout, etc triggers are not yet supported.
	 */
	virtual void SetTriggerType(Oscilloscope::TriggerType type) =0;

	/**
		@brief Clear out all existing trigger conditions

		This function is used for complex LA triggers and isn't yet supported by glscopeclient.
		Simple instruments should just override with an empty function.
	 */
	virtual void ResetTriggerConditions() =0;

	/**
		@brief Sets the trigger condition for a single channel

		This function is used for complex LA triggers and isn't yet supported by glscopeclient.
		Simple instruments should just override with an empty function.
	 */
	virtual void SetTriggerForChannel(OscilloscopeChannel* channel, std::vector<TriggerType> triggerbits)=0;

	/**
		@brief Reads data for all enabled channels from the instrument.

		@parameter toQueue	If true, acquire the waveform into the pending-waveform queue for future analysis.
							If false, the waveform (or first segment in sequenced captures), is acquired into
							the current channel state and any additional segments are queued.
	 */
	virtual bool AcquireData(bool toQueue = false) =0;

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

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Sequenced triggering

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
	size_t GetPendingWaveformCount();
	virtual Oscilloscope::TriggerMode PollTriggerFifo();
	virtual bool AcquireDataFifo();

protected:
	typedef std::map<OscilloscopeChannel*, WaveformBase*> SequenceSet;
	std::list<SequenceSet> m_pendingWaveforms;
	std::mutex m_pendingWaveformsMutex;

protected:

	///The channels
	std::vector<OscilloscopeChannel*> m_channels;

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
