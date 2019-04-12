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
	SincInterpolationDecoder.cpp
	NRZDecoder.cpp
	UARTDecoder.cpp

	scopeprotocols.cpp
	)

add_library(scopeprotocols SHARED
	${SCOPEPROTOCOLS_SOURCES})

target_include_directories(scopeprotocols
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS scopeprotocols LIBRARY DESTINATION /usr/lib)
