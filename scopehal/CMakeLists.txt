include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEHAL_SOURCES
	base64.cpp
	scopehal.cpp

	AnalogRenderer.cpp
	AsciiRenderer.cpp
	ChannelRenderer.cpp
	DigitalRenderer.cpp
	TextRenderer.cpp
	TimescaleRenderer.cpp
	Graph.cpp

	Instrument.cpp
	Oscilloscope.cpp
	OscilloscopeChannel.cpp
	LeCroyVICPOscilloscope.cpp
	RedTinLogicAnalyzer.cpp
	RohdeSchwarzHMC8012Multimeter.cpp
	RohdeSchwarzHMC804xPowerSupply.cpp
	Multimeter.cpp
	PowerSupply.cpp

	ProtocolDecoder.cpp
	)

add_library(scopehal SHARED
	${SCOPEHAL_SOURCES})
target_link_libraries(scopehal ${SIGCXX_LIBRARIES} ${GTKMM_LIBRARIES})
target_link_libraries(scopehal xptools log)

target_include_directories(scopehal
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS scopehal LIBRARY DESTINATION /usr/lib)
