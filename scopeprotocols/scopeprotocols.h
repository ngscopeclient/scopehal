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
	@brief Main library include file
 */

#ifndef scopeprotocols_h
#define scopeprotocols_h

#include "../scopehal/scopehal.h"
#include "../scopehal/ProtocolDecoder.h"
//#include "../scopehal/StateDecoder.h"

#include "ACCoupleDecoder.h"
#include "ADL5205Decoder.h"
#include "BaseMeasurementDecoder.h"
#include "CANDecoder.h"
#include "ClockJitterDecoder.h"
#include "ClockRecoveryDecoder.h"
#include "DCOffsetDecoder.h"
#include "DDR3Decoder.h"
#include "DifferenceDecoder.h"
#include "DramRefreshActivateMeasurementDecoder.h"
#include "DramRowColumnLatencyMeasurementDecoder.h"
#include "DVIDecoder.h"
#include "EthernetProtocolDecoder.h"		//must be before all other ethernet decodes
#include "EthernetAutonegotiationDecoder.h"
#include "EthernetGMIIDecoder.h"
#include "Ethernet10BaseTDecoder.h"
#include "Ethernet100BaseTDecoder.h"
#include "EyeDecoder2.h"
#include "FallMeasurementDecoder.h"
#include "FFTDecoder.h"
#include "FrequencyMeasurementDecoder.h"
#include "HorizontalBathtubDecoder.h"
#include "IBM8b10bDecoder.h"
#include "I2CDecoder.h"
#include "JtagDecoder.h"
#include "MDIODecoder.h"
#include "MovingAverageDecoder.h"
#include "ParallelBusDecoder.h"
#include "PeriodMeasurementDecoder.h"
#include "RiseMeasurementDecoder.h"
#include "SincInterpolationDecoder.h"
#include "SPIDecoder.h"
#include "ThresholdDecoder.h"
#include "TMDSDecoder.h"
#include "TopMeasurementDecoder.h"
#include "UARTDecoder.h"
#include "UartClockRecoveryDecoder.h"
#include "USB2ActivityDecoder.h"
#include "USB2PacketDecoder.h"
#include "USB2PCSDecoder.h"
#include "USB2PMADecoder.h"
#include "WaterfallDecoder.h"

/*
#include "DigitalToAnalogDecoder.h"
#include "DMADecoder.h"
#include "RPCDecoder.h"
#include "RPCNameserverDecoder.h"
#include "SchmittTriggerDecoder.h"
*/

#include "AverageStatistic.h"
#include "MaximumStatistic.h"
#include "MinimumStatistic.h"

void ScopeProtocolStaticInit();

#endif
