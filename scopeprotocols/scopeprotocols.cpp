/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Scope protocol initialization
 */

#include "scopeprotocols.h"

/**
	@brief Static initialization for protocol list
 */
void ScopeProtocolStaticInit()
{
	AddDecoderClass(ACCoupleFilter);
	AddDecoderClass(ACRMSMeasurement);
	AddDecoderClass(AddFilter);
	AddDecoderClass(ADL5205Decoder);
	AddDecoderClass(AreaMeasurement);
	AddDecoderClass(AutocorrelationFilter);
	AddDecoderClass(AverageFilter);
	AddDecoderClass(BandwidthMeasurement);
	AddDecoderClass(BaseMeasurement);
	AddDecoderClass(BINImportFilter);
	AddDecoderClass(BurstWidthMeasurement);
	AddDecoderClass(BusHeatmapFilter);
	AddDecoderClass(CANAnalyzerFilter);
	AddDecoderClass(CANBitmaskFilter);
	AddDecoderClass(CANDecoder);
	AddDecoderClass(CandumpImportFilter);
	AddDecoderClass(ChannelEmulationFilter);
	AddDecoderClass(ClipFilter);
	AddDecoderClass(ClockRecoveryFilter);
	AddDecoderClass(ComplexImportFilter);
	AddDecoderClass(ComplexSpectrogramFilter);
	AddDecoderClass(ConstantFilter);
	AddDecoderClass(CSVExportFilter);
	AddDecoderClass(CSVImportFilter);
	AddDecoderClass(CTLEFilter);
	AddDecoderClass(CurrentShuntFilter);
	AddDecoderClass(DCDMeasurement);
	AddDecoderClass(DDJMeasurement);
	AddDecoderClass(DDR1Decoder);
	AddDecoderClass(DDR3Decoder);
	AddDecoderClass(DeEmbedFilter);
	AddDecoderClass(DeskewFilter);
	AddDecoderClass(DigitalToPAM4Filter);
	AddDecoderClass(DigitalToNRZFilter);
	AddDecoderClass(DivideFilter);
	AddDecoderClass(DownconvertFilter);
	AddDecoderClass(DownsampleFilter);
	AddDecoderClass(DPAuxChannelDecoder);
	AddDecoderClass(DPhyDataDecoder);
	AddDecoderClass(DPhyEscapeModeDecoder);
	AddDecoderClass(DPhyHSClockRecoveryFilter);
	AddDecoderClass(DPhySymbolDecoder);
	AddDecoderClass(DramClockFilter);
	AddDecoderClass(DramRefreshActivateMeasurement);
	AddDecoderClass(DramRowColumnLatencyMeasurement);
	AddDecoderClass(DSIFrameDecoder);
	AddDecoderClass(DSIPacketDecoder);
	AddDecoderClass(DutyCycleMeasurement);
	AddDecoderClass(DVIDecoder);
	AddDecoderClass(EmphasisFilter);
	AddDecoderClass(EmphasisRemovalFilter);
	AddDecoderClass(EnhancedResolutionFilter);
	AddDecoderClass(EnvelopeFilter);
	AddDecoderClass(ESPIDecoder);
	AddDecoderClass(Ethernet10BaseTDecoder);
	AddDecoderClass(Ethernet100BaseTXDecoder);
	AddDecoderClass(Ethernet1000BaseXDecoder);
	AddDecoderClass(Ethernet10GBaseRDecoder);
	AddDecoderClass(Ethernet64b66bDecoder);
	AddDecoderClass(EthernetGMIIDecoder);
	AddDecoderClass(EthernetRGMIIDecoder);
	AddDecoderClass(EthernetRMIIDecoder);
	AddDecoderClass(EthernetSGMIIDecoder);
	AddDecoderClass(EthernetAutonegotiationDecoder);
	AddDecoderClass(EthernetAutonegotiationPageDecoder);
	AddDecoderClass(EthernetBaseXAutonegotiationDecoder);
	AddDecoderClass(ExponentialMovingAverageFilter);
	AddDecoderClass(EyeBitRateMeasurement);
	AddDecoderClass(EyePattern);
	AddDecoderClass(EyeHeightMeasurement);
	AddDecoderClass(EyeJitterMeasurement);
	AddDecoderClass(EyePeriodMeasurement);
	AddDecoderClass(EyeWidthMeasurement);
	AddDecoderClass(FallMeasurement);
	AddDecoderClass(FFTFilter);
	AddDecoderClass(FIRFilter);
	AddDecoderClass(FrequencyMeasurement);
	AddDecoderClass(FSKDecoder);
	AddDecoderClass(FullWidthHalfMax);
	AddDecoderClass(GateFilter);
	AddDecoderClass(GlitchRemovalFilter);
	AddDecoderClass(GroupDelayFilter);
	AddDecoderClass(HistogramFilter);
	AddDecoderClass(HorizontalBathtub);
	AddDecoderClass(HyperRAMDecoder);
	AddDecoderClass(I2CDecoder);
	AddDecoderClass(I2CEepromDecoder);
	AddDecoderClass(I2CRegisterDecoder);
	AddDecoderClass(IBISDriverFilter);
	AddDecoderClass(IBM8b10bDecoder);
	AddDecoderClass(InvertFilter);
	AddDecoderClass(IPv4Decoder);
	AddDecoderClass(IQSquelchFilter);
	AddDecoderClass(ISIMeasurement);
	AddDecoderClass(JitterFilter);
	AddDecoderClass(JitterSpectrumFilter);
	AddDecoderClass(JtagDecoder);
	AddDecoderClass(MagnitudeFilter);
	AddDecoderClass(MaximumFilter);
	AddDecoderClass(MDIODecoder);
	AddDecoderClass(MemoryFilter);
	AddDecoderClass(MilStd1553Decoder);
	AddDecoderClass(MinimumFilter);
	AddDecoderClass(MovingAverageFilter);
	AddDecoderClass(MultiplyFilter);
	AddDecoderClass(NoiseFilter);
	AddDecoderClass(OneWireDecoder);
	AddDecoderClass(OvershootMeasurement);
	AddDecoderClass(PAM4DemodulatorFilter);
	AddDecoderClass(ParallelBus);
	AddDecoderClass(PcapngImportFilter);
	AddDecoderClass(PCIe128b130bDecoder);
	AddDecoderClass(PCIeDataLinkDecoder);
	AddDecoderClass(PCIeGen2LogicalDecoder);
	AddDecoderClass(PCIeGen3LogicalDecoder);
	AddDecoderClass(PCIeLinkTrainingDecoder);
	AddDecoderClass(PCIeTransportDecoder);
	AddDecoderClass(PeakHoldFilter);
	AddDecoderClass(PeaksFilter);
	AddDecoderClass(PeriodMeasurement);
	AddDecoderClass(PhaseMeasurement);
	AddDecoderClass(PhaseNonlinearityFilter);
	AddDecoderClass(PkPkMeasurement);
	AddDecoderClass(PRBSCheckerFilter);
	AddDecoderClass(PRBSGeneratorFilter);
	AddDecoderClass(PulseWidthMeasurement);
	AddDecoderClass(ReferencePlaneExtensionFilter);
	AddDecoderClass(QSGMIIDecoder);
	AddDecoderClass(QSPIDecoder);
	AddDecoderClass(QuadratureDecoder);
	AddDecoderClass(RiseMeasurement);
	AddDecoderClass(RMSMeasurement);
	AddDecoderClass(RjBUjFilter);
	AddDecoderClass(ScalarPulseDelayFilter);
	AddDecoderClass(ScalarStairstepFilter);
	AddDecoderClass(ScaleFilter);
	AddDecoderClass(SDCmdDecoder);
	AddDecoderClass(SDDataDecoder);
	AddDecoderClass(SNRFilter);
	AddDecoderClass(SParameterCascadeFilter);
	AddDecoderClass(SParameterDeEmbedFilter);
	AddDecoderClass(SpectrogramFilter);
	AddDecoderClass(SPIDecoder);
	AddDecoderClass(SPIFlashDecoder);
	AddDecoderClass(SquelchFilter);
	AddDecoderClass(StepGeneratorFilter);
	AddDecoderClass(SubtractFilter);
	AddDecoderClass(SWDDecoder);
	AddDecoderClass(SWDMemAPDecoder);
	AddDecoderClass(TachometerFilter);
	AddDecoderClass(TappedDelayLineFilter);
	AddDecoderClass(TCPDecoder);
	AddDecoderClass(TDRFilter);
	AddDecoderClass(ThermalDiodeFilter);
	AddDecoderClass(ThresholdFilter);
	AddDecoderClass(TIEMeasurement);
	AddDecoderClass(TimeOutsideLevelMeasurement);
	AddDecoderClass(TMDSDecoder);
	AddDecoderClass(ToneGeneratorFilter);
	AddDecoderClass(TopMeasurement);
	AddDecoderClass(TouchstoneExportFilter);
	AddDecoderClass(TouchstoneImportFilter);
	AddDecoderClass(TRCImportFilter);
	AddDecoderClass(TrendFilter);
	AddDecoderClass(UARTDecoder);
	AddDecoderClass(UartClockRecoveryFilter);
	AddDecoderClass(UndershootMeasurement);
	AddDecoderClass(UnwrappedPhaseFilter);
	AddDecoderClass(UpsampleFilter);
	AddDecoderClass(USB2ActivityDecoder);
	AddDecoderClass(USB2PacketDecoder);
	AddDecoderClass(USB2PCSDecoder);
	AddDecoderClass(USB2PMADecoder);
	AddDecoderClass(VCDImportFilter);
	AddDecoderClass(VectorFrequencyFilter);
	AddDecoderClass(VectorPhaseFilter);
	AddDecoderClass(VerticalBathtub);
	AddDecoderClass(VICPDecoder);
	AddDecoderClass(Waterfall);
	AddDecoderClass(WAVImportFilter);
	AddDecoderClass(WFMImportFilter);
	AddDecoderClass(WindowedAutocorrelationFilter);
	AddDecoderClass(WindowFilter);
	AddDecoderClass(XYSweepFilter);
}
