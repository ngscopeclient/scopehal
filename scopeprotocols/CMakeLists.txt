include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEPROTOCOLS_SOURCES
	ACCoupleFilter.cpp
	ADL5205Decoder.cpp
	AutocorrelationFilter.cpp
	BaseMeasurement.cpp
	CANDecoder.cpp
	ChannelEmulationFilter.cpp
	ClockRecoveryFilter.cpp
	CTLEFilter.cpp
	CurrentShuntFilter.cpp
	DCOffsetFilter.cpp
	DDJMeasurement.cpp
	DDR3Decoder.cpp
	DeEmbedFilter.cpp
	DeskewFilter.cpp
	DownconvertFilter.cpp
	DownsampleFilter.cpp
	DPhyDataDecoder.cpp
	DPhyHSClockRecoveryFilter.cpp
	DPhySymbolDecoder.cpp
	DramRefreshActivateMeasurement.cpp
	DramRowColumnLatencyMeasurement.cpp
	DSIFrameDecoder.cpp
	DSIPacketDecoder.cpp
	DutyCycleMeasurement.cpp
	DVIDecoder.cpp
	EmphasisFilter.cpp
	EmphasisRemovalFilter.cpp
	Ethernet10BaseTDecoder.cpp
	Ethernet100BaseTDecoder.cpp
	Ethernet1000BaseXDecoder.cpp
	Ethernet10GBaseRDecoder.cpp
	Ethernet64b66bDecoder.cpp
	EthernetAutonegotiationDecoder.cpp
	EthernetGMIIDecoder.cpp
	EthernetRGMIIDecoder.cpp
	EthernetProtocolDecoder.cpp
	EyeBitRateMeasurement.cpp
	EyePattern.cpp
	EyeMask.cpp
	EyeHeightMeasurement.cpp
	EyeJitterMeasurement.cpp
	EyePeriodMeasurement.cpp
	EyeWidthMeasurement.cpp
	FallMeasurement.cpp
	FFTFilter.cpp
	FrequencyMeasurement.cpp
	HistogramFilter.cpp
	HorizontalBathtub.cpp
	I2CDecoder.cpp
	I2CEepromDecoder.cpp
	IBM8b10bDecoder.cpp
	IPv4Decoder.cpp
	JitterSpectrumFilter.cpp
	JtagDecoder.cpp
	MagnitudeFilter.cpp
	MDIODecoder.cpp
	MovingAverageFilter.cpp
	MultiplyFilter.cpp
	OFDMDemodulator.cpp
	OneWireDecoder.cpp
	OvershootMeasurement.cpp
	ParallelBus.cpp
	PCIeDataLinkDecoder.cpp
	PCIeGen2LogicalDecoder.cpp
	PCIeTransportDecoder.cpp
	PeakHoldFilter.cpp
	PeriodMeasurement.cpp
	PkPkMeasurement.cpp
	QuadratureDecoder.cpp
	QSPIDecoder.cpp
	RiseMeasurement.cpp
	SDCmdDecoder.cpp
	SDDataDecoder.cpp
	SPIDecoder.cpp
	SPIFlashDecoder.cpp
	SubtractFilter.cpp
	SWDDecoder.cpp
	SWDMemAPDecoder.cpp
	TachometerFilter.cpp
	TappedDelayLineFilter.cpp
	ThresholdFilter.cpp
	TIEMeasurement.cpp
	TMDSDecoder.cpp
	TopMeasurement.cpp
	UARTDecoder.cpp
	UartClockRecoveryFilter.cpp
	UndershootMeasurement.cpp
	UpsampleFilter.cpp
	USB2ActivityDecoder.cpp
	USB2PacketDecoder.cpp
	USB2PCSDecoder.cpp
	USB2PMADecoder.cpp
	VerticalBathtub.cpp
	Waterfall.cpp
	WindowedAutocorrelationFilter.cpp

	AverageStatistic.cpp
	MaximumStatistic.cpp
	MinimumStatistic.cpp

	scopeprotocols.cpp
	)

add_library(scopeprotocols SHARED
	${SCOPEPROTOCOLS_SOURCES})

target_link_libraries(scopeprotocols
	scopehal
	${LIBFFTS_LIBRARIES})

target_include_directories(scopeprotocols
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
       ${LIBFFTS_INCLUDE_DIR})

install(TARGETS scopeprotocols LIBRARY DESTINATION /usr/lib)
