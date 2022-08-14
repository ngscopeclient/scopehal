include_directories(SYSTEM ${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

find_library(LXI_LIB lxi)
find_library(LINUXGPIB_LIB gpib)


# Additional Windows/Linux libraries
if(WIN32)
	set(WIN_LIBS shlwapi)
	# The packaging for yaml-cpp on mingw64 seems to be broken, not providing a
	# libyaml-cpp.dll.a to be found by find_library inside of find_package(YAML)
	# when using FindYAML.cmake, just set the library directly
	set(YAML_LIBRARIES yaml-cpp)
else()
	set(LIN_LIBS dl)
	find_package(Yaml REQUIRED)
endif()

# Apple has their own idea about stat structs
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dst_mtim=st_mtimespec")
endif()

if(LXI_LIB)
	set(HAS_LXI true)
	set(LXI_LIBRARIES ${LXI_LIB})
else()
	set(HAS_LXI false)
	set(LXI_LIBRARIES "")
endif()

if(LINUXGPIB_LIB)
	set(HAS_LINUXGPIB true)
	set(LINUXGPIB_LIBRARIES ${LINUXGPIB_LIB})
else()
	set(HAS_LINUXGPIB false)
	set(LINUXGPIB_LIBRARIES "")
endif()

if(OpenCL_FOUND)
	set(HAVE_OPENCL 1)
else()
	set(HAVE_OPENCL 0)
	set(OpenCL_LIBRARIES "")
	set(OpenCL_INCLUDE_DIR "")
endif()

if(CLFFT_FOUND)
	set(HAVE_CLFFT 1)
else()
	set(HAVE_CLFFT 0)
	set(CLFFT_LIBRARIES "")
	set(CLFFT_INCLUDE_DIR "")
endif()

set(SCOPEHAL_SOURCES
	base64.cpp
	scopehal.cpp
	avx_mathfun.cpp

	Unit.cpp

	SCPITransport.cpp
	SCPISocketTransport.cpp
	SCPITwinLanTransport.cpp
	VICPSocketTransport.cpp
	SCPILinuxGPIBTransport.cpp
	SCPILxiTransport.cpp
	SCPINullTransport.cpp
	SCPITMCTransport.cpp
	SCPIUARTTransport.cpp
	SCPIDevice.cpp

	IBISParser.cpp
	SParameters.cpp
	TouchstoneParser.cpp

	FlowGraphNode.cpp
	Trigger.cpp
	CDRTrigger.cpp
	CDR8B10BTrigger.cpp
	CDRNRZPatternTrigger.cpp
	DCAEdgeTrigger.cpp
	DropoutTrigger.cpp
	EdgeTrigger.cpp
	GlitchTrigger.cpp
	NthEdgeBurstTrigger.cpp
	PulseWidthTrigger.cpp
	RuntTrigger.cpp
	SerialTrigger.cpp
	SlewRateTrigger.cpp
	TwoLevelTrigger.cpp
	UartTrigger.cpp
	WindowTrigger.cpp

	Instrument.cpp
	FunctionGenerator.cpp
	Multimeter.cpp
	Oscilloscope.cpp
	OscilloscopeChannel.cpp
	PowerSupply.cpp
	RFSignalGenerator.cpp
	SCPIInstrument.cpp
	SCPIMultimeter.cpp
	SCPIOscilloscope.cpp
	SCPIPowerSupply.cpp
	SCPIRFSignalGenerator.cpp
	RemoteBridgeOscilloscope.cpp

	AgilentOscilloscope.cpp
	AntikernelLabsOscilloscope.cpp
	AntikernelLogicAnalyzer.cpp
	DemoOscilloscope.cpp
	DigilentOscilloscope.cpp
	DSLabsOscilloscope.cpp
	KeysightDCA.cpp
	LeCroyOscilloscope.cpp
	MockOscilloscope.cpp
	PicoOscilloscope.cpp
	RigolOscilloscope.cpp
	SiglentSCPIOscilloscope.cpp
	SiglentVectorSignalGenerator.cpp
	TektronixOscilloscope.cpp
	RohdeSchwarzOscilloscope.cpp
	RohdeSchwarzHMC8012Multimeter.cpp
	RohdeSchwarzHMC804xPowerSupply.cpp

	Filter.cpp
	FilterParameter.cpp
	ImportFilter.cpp
	PacketDecoder.cpp
	PeakDetectionFilter.cpp
	Statistic.cpp
	SpectrumChannel.cpp
	SParameterSourceFilter.cpp
	SParameterFilter.cpp

	TestWaveformSource.cpp

	ExportWizard.cpp
	)

configure_file(config.h.in config.h)

add_library(scopehal SHARED
	${SCOPEHAL_SOURCES})
target_link_libraries(scopehal ${SIGCXX_LIBRARIES} ${GTKMM_LIBRARIES} xptools log graphwidget ${YAML_LIBRARIES}
	${LXI_LIBRARIES} ${LINUXGPIB_LIBRARIES} ${WIN_LIBS} ${LIN_LIBS} ${LIBFFTS_LIBRARIES} ${OpenCL_LIBRARIES}
	${CLFFT_LIBRARIES} ${OpenMP_CXX_LIBRARIES} ${Vulkan_LIBRARIES})

target_include_directories(scopehal
PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${LIBFFTS_INCLUDE_DIR}
	${OpenCL_INCLUDE_DIR}
	${CLFFT_INCLUDE_DIR}
	${YAML_INCLUDES}
	${Vuklkan_INCLUDE_DIRS}
	)

target_include_directories(scopehal
PUBLIC SYSTEM
	${CMAKE_CURRENT_SOURCE_DIR}/../OpenCL-CLHPP/include)

if(${HAS_LXI})
	target_compile_definitions(scopehal PUBLIC HAS_LXI)
endif()

if(${HAS_LINUXGPIB})
	target_compile_definitions(scopehal PUBLIC HAS_LINUXGPIB)
endif()

install(TARGETS scopehal LIBRARY)
