include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

find_library(LXI_LIB lxi)

set(SCOPEHAL_SOURCES
	base64.cpp
	scopehal.cpp

	Unit.cpp

	SCPITransport.cpp
	SCPISocketTransport.cpp
	VICPSocketTransport.cpp
	SCPILxiTransport.cpp
	SCPIDevice.cpp

	Instrument.cpp
	FunctionGenerator.cpp
	Oscilloscope.cpp
	OscilloscopeChannel.cpp
	SCPIOscilloscope.cpp
	AgilentOscilloscope.cpp
	AntikernelLabsOscilloscope.cpp
	AntikernelLogicAnalyzer.cpp
	LeCroyOscilloscope.cpp
	MockOscilloscope.cpp
	SiglentSCPIOscilloscope.cpp
	RigolOscilloscope.cpp
	RohdeSchwarzOscilloscope.cpp
	RohdeSchwarzHMC8012Multimeter.cpp
	RohdeSchwarzHMC804xPowerSupply.cpp
	Multimeter.cpp
	PowerSupply.cpp

	ProtocolDecoder.cpp
	PacketDecoder.cpp
	Measurement.cpp
	Statistic.cpp
	)

add_library(scopehal SHARED
	${SCOPEHAL_SOURCES})
target_link_libraries(scopehal ${SIGCXX_LIBRARIES} ${GTKMM_LIBRARIES} xptools log graphwidget yaml-cpp ${LXI_LIB})

target_include_directories(scopehal
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS scopehal LIBRARY DESTINATION /usr/lib)
