include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEHAL_SOURCES
	base64.cpp
	scopehal.cpp

	Unit.cpp

	AnalogRenderer.cpp
	AsciiRenderer.cpp
	ChannelRenderer.cpp
	DigitalRenderer.cpp
	TextRenderer.cpp

	SCPITransport.cpp
	SCPISocketTransport.cpp
	SCPIDevice.cpp

	Instrument.cpp
	FunctionGenerator.cpp
	Oscilloscope.cpp
	SCPIOscilloscope.cpp
	OscilloscopeChannel.cpp
	AgilentOscilloscope.cpp
	AntikernelLogicAnalyzer.cpp
	LeCroyOscilloscope.cpp
	LeCroyVICPOscilloscope.cpp
	SiglentSCPIOscilloscope.cpp
	RedTinLogicAnalyzer.cpp
	RigolOscilloscope.cpp
	RohdeSchwarzOscilloscope.cpp
	RohdeSchwarzHMC8012Multimeter.cpp
	RohdeSchwarzHMC804xPowerSupply.cpp
	Multimeter.cpp
	PowerSupply.cpp

	ProtocolDecoder.cpp
	PacketDecoder.cpp
	Measurement.cpp
	)

add_library(scopehal SHARED
	${SCOPEHAL_SOURCES})
target_link_libraries(scopehal ${SIGCXX_LIBRARIES} ${GTKMM_LIBRARIES} xptools log graphwidget)

target_include_directories(scopehal
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS scopehal LIBRARY DESTINATION /usr/lib)
