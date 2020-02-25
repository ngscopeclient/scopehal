include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEPROTOCOLS_SOURCES
	ACCoupleDecoder.cpp
	ClockJitterDecoder.cpp
	ClockRecoveryDecoder.cpp
	ClockRecoveryDebugDecoder.cpp
	DCOffsetDecoder.cpp
	DifferenceDecoder.cpp
	DVIDecoder.cpp
	DVIRenderer.cpp
	Ethernet10BaseTDecoder.cpp
	Ethernet100BaseTDecoder.cpp
	EthernetAutonegotiationDecoder.cpp
	EthernetAutonegotiationRenderer.cpp
	EthernetProtocolDecoder.cpp
	EthernetRenderer.cpp
	EyeDecoder2.cpp
	FFTDecoder.cpp
	I2CDecoder.cpp
	I2CRenderer.cpp
	CANDecoder.cpp
	CANRenderer.cpp
	IBM8b10bDecoder.cpp
	IBM8b10bRenderer.cpp
	JtagDecoder.cpp
	JtagRenderer.cpp
	MDIODecoder.cpp
	MDIORenderer.cpp
	MovingAverageDecoder.cpp
	PeriodMeasurementDecoder.cpp
	SincInterpolationDecoder.cpp
	ThresholdDecoder.cpp
	TMDSDecoder.cpp
	TMDSRenderer.cpp
	UARTDecoder.cpp
	UartClockRecoveryDecoder.cpp
	USB2ActivityDecoder.cpp
	USB2PacketDecoder.cpp
	USB2PacketRenderer.cpp
	USB2PCSDecoder.cpp
	USB2PCSRenderer.cpp
	USB2PMADecoder.cpp
	USB2PMARenderer.cpp
	WaterfallDecoder.cpp

	scopeprotocols.cpp
	)

add_library(scopeprotocols SHARED
	${SCOPEPROTOCOLS_SOURCES})

target_link_libraries(scopeprotocols
	${LIBFFTS_LIBRARIES})

target_include_directories(scopeprotocols
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
PRIVATE ${LIBFFTS_INCLUDE_DIR})

install(TARGETS scopeprotocols LIBRARY DESTINATION /usr/lib)
