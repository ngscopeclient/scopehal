/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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

/**
	@brief Generic representation of an oscilloscope or logic analyzer.

	An Oscilloscope contains triggering logic and one or more ChannelSource's.
 */
class Oscilloscope : public virtual Instrument
{
public:
	//Construction / destruction
	Oscilloscope();
	virtual ~Oscilloscope();

	/**
		@brief Instruments are allowed to cache configuration settings to reduce round trip queries to the device.

		In order to see updates made by the user at the front panel, the cache must be flushed.

		Cache flushing is recommended every second or so during interactive operation.
	 */
	virtual void FlushConfigCache();

	//Channel information
	size_t GetChannelCount();
	OscilloscopeChannel* GetChannel(size_t i);
	OscilloscopeChannel* GetChannel(std::string name);
	virtual bool IsChannelEnabled(size_t i) =0;
	virtual void EnableChannel(size_t i) =0;
	virtual void DisableChannel(size_t i) =0;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) =0;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) =0;
	virtual double GetChannelAttenuation(size_t i) =0;
	virtual void SetChannelAttenuation(size_t i, double atten) =0;
	virtual int GetChannelBandwidthLimit(size_t i) =0;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) =0;

	/**
		@brief Returns the external trigger input channel, if we have one
	 */
	virtual OscilloscopeChannel* GetExternalTrigger() =0;

	/**
		@brief Gets the range of the current channel configuration.

		The range is the distance, in volts, between the most negative/smallest and most positive/largest
		voltage which the ADC can represent using the current vertical gain configuration.

		The range does not depend on the offset.
	 */
	virtual double GetChannelVoltageRange(size_t i) =0;
	virtual void SetChannelVoltageRange(size_t i, double range) =0;
	virtual double GetChannelOffset(size_t i) =0;
	virtual void SetChannelOffset(size_t i, double offset) =0;

	void AddChannel(OscilloscopeChannel* chan);

	//Triggering
	enum TriggerMode
	{
		///Active, waiting for a trigger event
		TRIGGER_MODE_RUN,

		///Triggered once, but not recently
		TRIGGER_MODE_STOP,

		///Just got triggered
		TRIGGER_MODE_TRIGGERED,

		///WAIT - waiting for something (not sure what this means, Rigol scopes use it)
		TRIGGER_MODE_WAIT,

		///Auto trigger - waiting for auto-trigger
		TRIGGER_MODE_AUTO,

		///Placeholder
		TRIGGER_MODE_COUNT
	};
	virtual Oscilloscope::TriggerMode PollTrigger() =0;
	bool WaitForTrigger(int timeout);

	/**
		@brief Clear out all existing trigger conditions
	 */
	virtual void ResetTriggerConditions() =0;

	enum TriggerType
	{
		TRIGGER_TYPE_LOW		= 0,
		TRIGGER_TYPE_HIGH 		= 1,
		TRIGGER_TYPE_FALLING	= 2,
		TRIGGER_TYPE_RISING		= 3,
		TRIGGER_TYPE_CHANGE		= 4,
		TRIGGER_TYPE_DONTCARE	= 5
	};

	//For simple triggering
	virtual size_t GetTriggerChannelIndex() =0;
	virtual void SetTriggerChannelIndex(size_t i) =0;
	virtual float GetTriggerVoltage() =0;
	virtual void SetTriggerVoltage(float v) =0;
	virtual Oscilloscope::TriggerType GetTriggerType() =0;
	virtual void SetTriggerType(Oscilloscope::TriggerType type) =0;

	/**
		@brief Sets the trigger condition for a single channel
	 */
	virtual void SetTriggerForChannel(OscilloscopeChannel* channel, std::vector<TriggerType> triggerbits)=0;

	virtual bool AcquireData(sigc::slot1<int, float> progress_callback) =0;

	virtual void Start() =0;
	virtual void StartSingleTrigger() =0;
	virtual void Stop() =0;

protected:

	///The channels
	std::vector<OscilloscopeChannel*> m_channels;
};

#endif
