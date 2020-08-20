include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEPROTOCOLS_SOURCES
	ACCoupleDecoder.cpp
	ADL5205Decoder.cpp
	BaseMeasurementDecoder.cpp
	CANDecoder.cpp
	ChannelEmulationDecoder.cpp
	ClockJitterDecoder.cpp
	ClockRecoveryDecoder.cpp
	CTLEDecoder.cpp
	CurrentShuntDecoder.cpp
	DCOffsetDecoder.cpp
	DDR3Decoder.cpp
	DeEmbedDecoder.cpp
	DeskewDecoder.cpp
	DifferenceDecoder.cpp
	DownsampleDecoder.cpp
	DramRefreshActivateMeasurementDecoder.cpp
	DramRowColumnLatencyMeasurementDecoder.cpp
	DVIDecoder.cpp
	Ethernet10BaseTDecoder.cpp
	Ethernet100BaseTDecoder.cpp
	Ethernet1000BaseXDecoder.cpp
	Ethernet10GBaseRDecoder.cpp
	Ethernet64b66bDecoder.cpp
	EthernetAutonegotiationDecoder.cpp
	EthernetGMIIDecoder.cpp
	EthernetRGMIIDecoder.cpp
	EthernetProtocolDecoder.cpp
	EyeBitRateMeasurementDecoder.cpp
	EyeDecoder2.cpp
	EyeMask.cpp
	EyeHeightMeasurementDecoder.cpp
	EyeJitterMeasurementDecoder.cpp
	EyePeriodMeasurementDecoder.cpp
	EyeWidthMeasurementDecoder.cpp
	FallMeasurementDecoder.cpp
	FFTDecoder.cpp
	FrequencyMeasurementDecoder.cpp
	HorizontalBathtubDecoder.cpp
	I2CDecoder.cpp
	IBM8b10bDecoder.cpp
	IPv4Decoder.cpp
	JtagDecoder.cpp
	LOMixDecoder.cpp
	MDIODecoder.cpp
	MovingAverageDecoder.cpp
	MultiplyDecoder.cpp
	OvershootMeasurementDecoder.cpp
	ParallelBusDecoder.cpp
	PeriodMeasurementDecoder.cpp
	PkPkMeasurementDecoder.cpp
	RiseMeasurementDecoder.cpp
	SincInterpolationDecoder.cpp
	SPIDecoder.cpp
	ThresholdDecoder.cpp
	TMDSDecoder.cpp
	TopMeasurementDecoder.cpp
	UARTDecoder.cpp
	UartClockRecoveryDecoder.cpp
	UndershootMeasurementDecoder.cpp
	USB2ActivityDecoder.cpp
	USB2PacketDecoder.cpp
	USB2PCSDecoder.cpp
	USB2PMADecoder.cpp
	WaterfallDecoder.cpp

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
PRIVATE ${LIBFFTS_INCLUDE_DIR})

install(TARGETS scopeprotocols LIBRARY DESTINATION /usr/lib)
