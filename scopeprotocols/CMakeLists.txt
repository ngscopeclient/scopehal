set(SCOPEPROTOCOLS_SOURCES
	ACCoupleFilter.cpp
	ACRMSMeasurement.cpp
	AddFilter.cpp
	ADL5205Decoder.cpp
	AreaMeasurement.cpp
	AutocorrelationFilter.cpp
	AverageFilter.cpp
	BandwidthMeasurement.cpp
	BaseMeasurement.cpp
	BINImportFilter.cpp
	BurstWidthMeasurement.cpp
	BusHeatmapFilter.cpp
	CANAnalyzerFilter.cpp
	CANBitmaskFilter.cpp
	CANDecoder.cpp
	CandumpImportFilter.cpp
	ChannelEmulationFilter.cpp
	ClipFilter.cpp
	ClockRecoveryFilter.cpp
	ComplexImportFilter.cpp
	ComplexSpectrogramFilter.cpp
	ConstellationFilter.cpp
	ConstantFilter.cpp
	CouplerDeEmbedFilter.cpp
	CSVExportFilter.cpp
	CSVImportFilter.cpp
	CTLEFilter.cpp
	CurrentShuntFilter.cpp
	DCDMeasurement.cpp
	DDJMeasurement.cpp
	DDR1Decoder.cpp
	DDR3Decoder.cpp
	DeEmbedFilter.cpp
	DeskewFilter.cpp
	DigitalToPAM4Filter.cpp
	DigitalToNRZFilter.cpp
	DivideFilter.cpp
	DownconvertFilter.cpp
	DownsampleFilter.cpp
	DPAuxChannelDecoder.cpp
	DPhyDataDecoder.cpp
	DPhyEscapeModeDecoder.cpp
	DPhyHSClockRecoveryFilter.cpp
	DPhySymbolDecoder.cpp
	DramClockFilter.cpp
	DramRefreshActivateMeasurement.cpp
	DramRowColumnLatencyMeasurement.cpp
	DSIFrameDecoder.cpp
	DSIPacketDecoder.cpp
	DutyCycleMeasurement.cpp
	DVIDecoder.cpp
	EmphasisFilter.cpp
	EmphasisRemovalFilter.cpp
	EnhancedResolutionFilter.cpp
	EnvelopeFilter.cpp
	ESPIDecoder.cpp
	Ethernet10BaseTDecoder.cpp
	Ethernet100BaseTXDecoder.cpp
	Ethernet100BaseT1Decoder.cpp
	Ethernet100BaseT1LinkTrainingDecoder.cpp
	Ethernet1000BaseXDecoder.cpp
	Ethernet10GBaseRDecoder.cpp
	Ethernet64b66bDecoder.cpp
	EthernetAutonegotiationDecoder.cpp
	EthernetAutonegotiationPageDecoder.cpp
	EthernetBaseXAutonegotiationDecoder.cpp
	EthernetGMIIDecoder.cpp
	EthernetRGMIIDecoder.cpp
	EthernetRMIIDecoder.cpp
	EthernetSGMIIDecoder.cpp
	EthernetProtocolDecoder.cpp
	ExponentialMovingAverageFilter.cpp
	ExportFilter.cpp
	EyeBitRateMeasurement.cpp
	EyePattern.cpp
	EyeHeightMeasurement.cpp
	EyeJitterMeasurement.cpp
	EyePeriodMeasurement.cpp
	EyeWidthMeasurement.cpp
	FallMeasurement.cpp
	FFTFilter.cpp
	FIRFilter.cpp
	FrequencyMeasurement.cpp
	FSKDecoder.cpp
	FullWidthHalfMax.cpp
	GateFilter.cpp
	GlitchRemovalFilter.cpp
	GroupDelayFilter.cpp
	HistogramFilter.cpp
	HorizontalBathtub.cpp
	HyperRAMDecoder.cpp
	I2CDecoder.cpp
	I2CEepromDecoder.cpp
	I2CRegisterDecoder.cpp
	IBISDriverFilter.cpp
	IBM8b10bDecoder.cpp
	IQDemuxFilter.cpp
	InvertFilter.cpp
	IPv4Decoder.cpp
	IQSquelchFilter.cpp
	ISIMeasurement.cpp
	J1939AnalogDecoder.cpp
	J1939BitmaskDecoder.cpp
	J1939PDUDecoder.cpp
	J1939SourceMatchFilter.cpp
	J1939TransportDecoder.cpp
	JitterFilter.cpp
	JitterSpectrumFilter.cpp
	JtagDecoder.cpp
	MagnitudeFilter.cpp
	MaximumFilter.cpp
	MDIODecoder.cpp
	MemoryFilter.cpp
	MilStd1553Decoder.cpp
	MinimumFilter.cpp
	MovingAverageFilter.cpp
	MultiplyFilter.cpp
	NCOFilter.cpp
	NoiseFilter.cpp
	OneWireDecoder.cpp
	OvershootMeasurement.cpp
	PAM4DemodulatorFilter.cpp
	PAMEdgeDetectorFilter.cpp
	ParallelBus.cpp
	PcapngExportFilter.cpp
	PcapngImportFilter.cpp
	PCIe128b130bDecoder.cpp
	PCIeDataLinkDecoder.cpp
	PCIeGen2LogicalDecoder.cpp
	PCIeGen3LogicalDecoder.cpp
	PCIeLinkTrainingDecoder.cpp
	PCIeTransportDecoder.cpp
	PeakHoldFilter.cpp
	PeaksFilter.cpp
	PeriodMeasurement.cpp
	PhaseMeasurement.cpp
	PhaseNonlinearityFilter.cpp
	PkPkMeasurement.cpp
	PointSampleFilter.cpp
	PRBSCheckerFilter.cpp
	PRBSGeneratorFilter.cpp
	PulseWidthMeasurement.cpp
	QSGMIIDecoder.cpp
	QSPIDecoder.cpp
	QuadratureDecoder.cpp
	ReferencePlaneExtensionFilter.cpp
	RGBLEDDecoder.cpp
	RISFilter.cpp
	RiseMeasurement.cpp
	RjBUjFilter.cpp
	RMSMeasurement.cpp
	SawtoothGeneratorFilter.cpp
	ScalarPulseDelayFilter.cpp
	ScalarStairstepFilter.cpp
	SDCmdDecoder.cpp
	SDDataDecoder.cpp
	SDRAMDecoderBase.cpp
	SetupHoldMeasurement.cpp
	SNRFilter.cpp
	SParameterCascadeFilter.cpp
	SParameterDeEmbedFilter.cpp
	SpectrogramFilter.cpp
	SPIDecoder.cpp
	SPIFlashDecoder.cpp
	SquelchFilter.cpp
	StepGeneratorFilter.cpp
	SubtractFilter.cpp
	SWDDecoder.cpp
	SWDMemAPDecoder.cpp
	TachometerFilter.cpp
	TappedDelayLineFilter.cpp
	TCPDecoder.cpp
	TDRFilter.cpp
	ThermalDiodeFilter.cpp
	ThresholdFilter.cpp
	TIEMeasurement.cpp
	TimeOutsideLevelMeasurement.cpp
	TMDSDecoder.cpp
	ToneGeneratorFilter.cpp
	TopMeasurement.cpp
	TouchstoneExportFilter.cpp
	TouchstoneImportFilter.cpp
	TRCImportFilter.cpp
	TrendFilter.cpp
	TwoPortShuntThroughFilter.cpp
	UartClockRecoveryFilter.cpp
	UARTDecoder.cpp
	UndershootMeasurement.cpp
	UnwrappedPhaseFilter.cpp
	UpsampleFilter.cpp
	USB2ActivityDecoder.cpp
	USB2PacketDecoder.cpp
	USB2PCSDecoder.cpp
	USB2PMADecoder.cpp
	VCDImportFilter.cpp
	VectorFrequencyFilter.cpp
	VectorPhaseFilter.cpp
	VerticalBathtub.cpp
	VICPDecoder.cpp
	Waterfall.cpp
	WaveformGenerationFilter.cpp
	WAVImportFilter.cpp
	WFMImportFilter.cpp
	WindowedAutocorrelationFilter.cpp
	WindowFilter.cpp
	XYSweepFilter.cpp

	scopeprotocols.cpp
	)

add_library(scopeprotocols SHARED
	${SCOPEPROTOCOLS_SOURCES})

target_link_libraries(scopeprotocols
	scopehal
	)

target_include_directories(scopeprotocols
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${glslang_INCLUDE_DIR}/glslang/Include
	)

# Setup for vkfft
target_compile_definitions(scopeprotocols PUBLIC -DVK_API_VERSION=10 -DVKFFT_BACKEND=0)

install(TARGETS scopeprotocols LIBRARY)

add_subdirectory(shaders)
