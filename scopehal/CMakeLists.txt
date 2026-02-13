# At the moment, this is meant to be built as part of scopehal-apps.
# Standalone build/install is a potential long-term goal.
#If there is interest in a standalone build, please reach out.

# Libraries specific to scopehal library not provided by root CMakeLists

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	pkg_check_modules(LXI liblxi QUIET IMPORTED_TARGET)
	#WORKAROUND LXI on Debian Bullseye lacks a pkgconfig file
	if(NOT LXI_FOUND)
		find_library(LXI_LINK_LIBRARIES lxi)
	endif()
	if(LXI_LINK_LIBRARIES)
		message("-- Found LXI: ${LXI_LINK_LIBRARIES}")
		# LXI requires TIRPC but does not link it automatically
		pkg_check_modules(TIRPC libtirpc QUIET IMPORTED_TARGET)
		if(TIRPC_FOUND)
			message("-- Found TIRPC: ${TIRPC_LINK_LIBRARIES}")
		else()
			message(FATAL_ERROR "TIRPC library is required for LXI to function. The LXI library was found, but TIRPC is missing.")
		endif()
	else()
		message("-- LXI library not found, LXI (VXI-11/TCP and RAW/TCP ) instrument support will not be available.")
		unset(LXI_LINK_LIBRARIES CACHE)
	endif()
endif()

# This is needed for the precompiled header
get_target_property(Vulkan_INCLUDE_DIR Vulkan::Headers INTERFACE_INCLUDE_DIRECTORIES)


#Set up versioning (with a dummy string for now if Git isn't present)
if (Git_FOUND OR SCOPEHAL_PACKAGE_VERSION)
    if(NOT SCOPEHAL_PACKAGE_VERSION)
        execute_process(
                    COMMAND ${GIT_EXECUTABLE} describe --always --tags
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE SCOPEHAL_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    else ()
        set(SCOPEHAL_VERSION "${SCOPEHAL_PACKAGE_VERSION}")
    endif ()
else()
	set(SCOPEHAL_VERSION "unknown")
endif()

configure_file(scopehal-version.h.in scopehal-version.h)

# Additional Windows/Linux libraries
if(WIN32)
	set(WIN_LIBS shlwapi)
endif()

# Apple has their own idea about stat structs
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	add_compile_options(-Dst_mtim=st_mtimespec)
endif()

set(SCOPEHAL_SOURCES
	base64.cpp
	scopehal.cpp
	avx_mathfun.cpp
	VulkanInit.cpp

	FileSystem.cpp
	ScratchBufferManager.cpp
	Unit.cpp
	Waveform.cpp
	DensityFunctionWaveform.cpp
	ConstellationWaveform.cpp
	EyeMask.cpp
	EyeWaveform.cpp

	Averager.cpp
	LevelCrossingDetector.cpp

	SCPITransport.cpp
	SCPISocketTransport.cpp
	SCPITwinLanTransport.cpp
	VICPSocketTransport.cpp
	SCPILxiTransport.cpp
	SCPINullTransport.cpp
	SCPISocketCANTransport.cpp
	SCPIUARTTransport.cpp
	SCPIHIDTransport.cpp
	SCPIDevice.cpp

	IBISParser.cpp
	SParameters.cpp
	TouchstoneParser.cpp

	FlowGraphNode.cpp
	SinkNode.cpp
	Trigger.cpp
	CDRTrigger.cpp
	CDR8B10BTrigger.cpp
	CDRNRZPatternTrigger.cpp
	DCAEdgeTrigger.cpp
	DropoutTrigger.cpp
	EdgeTrigger.cpp
	GlitchTrigger.cpp
	LineTrigger.cpp
	NthEdgeBurstTrigger.cpp
	PulseWidthTrigger.cpp
	RuntTrigger.cpp
	SerialTrigger.cpp
	SlewRateTrigger.cpp
	TwoLevelTrigger.cpp
	UartTrigger.cpp
	WindowTrigger.cpp

	RSRTB2kRiseTimeTrigger.cpp
	RSRTB2kRuntTrigger.cpp
	RSRTB2kTimeoutTrigger.cpp
	RSRTB2kVideoTrigger.cpp
	RSRTB2kWidthTrigger.cpp

	Instrument.cpp
	InstrumentChannel.cpp
	BERT.cpp
	BERTInputChannel.cpp
	BERTInputChannelWithDataCapture.cpp
	BERTOutputChannel.cpp
	BinaryDriver.cpp
	BufferedSwitchMatrixInputChannel.cpp
	BufferedSwitchMatrixIOChannel.cpp
	BufferedSwitchMatrixOutputChannel.cpp
	CANChannel.cpp
	CommandLineDriver.cpp
	DigitalIOChannel.cpp
	DigitalInputChannel.cpp
	DigitalOutputChannel.cpp
	FunctionGenerator.cpp
	FunctionGeneratorChannel.cpp
	Load.cpp
	LoadChannel.cpp
	MockInstrument.cpp
	Multimeter.cpp
	MultimeterChannel.cpp
	Oscilloscope.cpp
	OscilloscopeChannel.cpp
	PowerSupply.cpp
	PowerSupplyChannel.cpp
	RFSignalGenerator.cpp
	RFSignalGeneratorChannel.cpp
	SCPIBERT.cpp
	SCPIFunctionGenerator.cpp
	SCPIInstrument.cpp
	HIDInstrument.cpp
	ModbusInstrument.cpp
	SCPILoad.cpp
	SCPIMiscInstrument.cpp
	SCPIMultimeter.cpp
	SCPIOscilloscope.cpp
	SCPIPowerSupply.cpp
	SCPIRFSignalGenerator.cpp
	SCPISA.cpp
	SCPISDR.cpp
	SCPISpectrometer.cpp
	SCPIVNA.cpp
	SocketCANAnalyzer.cpp
	SpectrometerDarkFrameChannel.cpp
	SwitchMatrix.cpp
	RemoteBridgeOscilloscope.cpp

	AgilentOscilloscope.cpp
	AlientekPowerSupply.cpp
	AntikernelLabsOscilloscope.cpp
	AntikernelLabsTriggerCrossbar.cpp
	AntikernelLogicAnalyzer.cpp
	AseqSpectrometer.cpp
	CopperMountainVNA.cpp
	CSVStreamInstrument.cpp
	DemoOscilloscope.cpp
	DemoPowerSupply.cpp
	DigilentOscilloscope.cpp
	DSLabsOscilloscope.cpp
	GWInstekGPDX303SPowerSupply.cpp
	HaasoscopePro.cpp
	KeysightDCA.cpp
	LeCroyOscilloscope.cpp
	LeCroyFWPOscilloscope.cpp
	MagnovaOscilloscope.cpp
	MockOscilloscope.cpp
	MockPowerSupply.cpp
	MultiLaneBERT.cpp
	NanoVNA.cpp
	OwonXDGFunctionGenerator.cpp
	OwonXDMMultimeter.cpp
	PicoOscilloscope.cpp
	PicoVNA.cpp
	RigolFunctionGenerator.cpp
	RigolOscilloscope.cpp
	RSRTB2kOscilloscope.cpp
	SiglentLoad.cpp
	SiglentFunctionGenerator.cpp
	SiglentPowerSupply.cpp
	SiglentSCPIOscilloscope.cpp
	SiglentVectorSignalGenerator.cpp
	TektronixOscilloscope.cpp
	TektronixHSIOscilloscope.cpp
	ThunderScopeOscilloscope.cpp
	TinySA.cpp
	UHDBridgeSDR.cpp
	HP662xAPowerSupply.cpp
	RigolDP8xxPowerSupply.cpp
	RohdeSchwarzOscilloscope.cpp
	RSRTO6Oscilloscope.cpp
	RohdeSchwarzHMC8012Multimeter.cpp
	RohdeSchwarzHMC804xPowerSupply.cpp
	RidenPowerSupply.cpp
	SinilinkPowerSupply.cpp
	KuaiquPowerSupply.cpp

	StandardColors.cpp
	Filter.cpp
	ActionProvider.cpp
	FilterParameter.cpp
	ImportFilter.cpp
	PacketDecoder.cpp
	PausableFilter.cpp
	PeakDetectionFilter.cpp
	SpectrumChannel.cpp
	SParameterSourceFilter.cpp
	SParameterFilter.cpp

	TestWaveformSource.cpp

	ComputePipeline.cpp
	FilterGraphExecutor.cpp
	PipelineCacheManager.cpp
	VulkanFFTPlan.cpp
	QueueManager.cpp
	)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND SCOPEHAL_SOURCES
		# TMC is only supported on Linux for now.
		# https://github.com/glscopeclient/scopehal/issues/519
		SCPITMCTransport.cpp
	)
endif()

configure_file(config.h.in config.h)

add_library(scopehal SHARED
	${SCOPEHAL_SOURCES})

# mingw64 build using
# https://sdk.lunarg.com/sdk/download/1.3.224.1/windows/VulkanSDK-1.3.224.1-Installer.exe
# https://github.com/KhronosGroup/glslang.git (tags/sdk-1.3.224.1) static lib (SPIRV* glslang OGLCompiler GenericCodeGen MachineIndependent OSDependent)

target_link_libraries(scopehal
	xptools
	log
	PkgConfig::SIGCXX
	yaml-cpp::yaml-cpp
	OpenMP::OpenMP_CXX
	Vulkan::Loader
	Vulkan::Headers
	glslang::glslang
	glfw
	${WIN_LIBS}
	)

if(TARGET glslang::SPIRV)
	target_link_libraries(scopehal
		glslang::SPIRV
	)
endif()

if(LXI_FOUND)
	target_link_libraries(scopehal
		PkgConfig::LXI
		PkgConfig::TIRPC
	)
	target_compile_definitions(scopehal PUBLIC HAS_LXI)
endif()

target_include_directories(scopehal
PRIVATE
	${glslang_INCLUDE_DIR}/glslang/Include
PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
)

target_include_directories(scopehal
SYSTEM PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/../VkFFT/vkFFT
)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	target_compile_definitions(scopehal
		PRIVATE
		VULKAN_HPP_TYPESAFE_CONVERSION=1
	)
endif()

target_precompile_headers(scopehal
PUBLIC

	# C++ library headers used by vulkan and/or very widely throughout the project
	${CMAKE_CURRENT_SOURCE_DIR}/scopehal-pch.h

	${Vulkan_INCLUDE_DIR}/vulkan/vulkan_raii.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/../VkFFT/vkFFT/vkFFT.h
	${YAML_PCH_PATH}
	)

install(TARGETS scopehal LIBRARY)

install(FILES channels/300mm-s2000m.s2p DESTINATION share/ngscopeclient/channels)

add_subdirectory(shaders)
