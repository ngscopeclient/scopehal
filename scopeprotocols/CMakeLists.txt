include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEPROTOCOLS_SOURCES
	ACCoupleDecoder.cpp
	ClockRecoveryDecoder.cpp
	DifferenceDecoder.cpp
	Ethernet10BaseTDecoder.cpp
	Ethernet100BaseTDecoder.cpp
	EthernetAutonegotiationDecoder.cpp
	EthernetAutonegotiationRenderer.cpp
	EthernetProtocolDecoder.cpp
	EthernetRenderer.cpp
	EyeDecoder.cpp
	EyeDecoder2.cpp
	EyeRenderer.cpp
	FFTDecoder.cpp
	IBM8b10bDecoder.cpp
	IBM8b10bRenderer.cpp
	JtagDecoder.cpp
	JtagRenderer.cpp
	SincInterpolationDecoder.cpp
	ThresholdDecoder.cpp
	UARTDecoder.cpp
	UartClockRecoveryDecoder.cpp
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
