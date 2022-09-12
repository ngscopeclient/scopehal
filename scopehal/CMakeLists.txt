include_directories(SYSTEM ${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

find_library(LXI_LIB lxi)
find_library(LINUXGPIB_LIB gpib)
find_package(glfw3 REQUIRED)


# Additional Windows/Linux libraries
if(WIN32)
	set(WIN_LIBS shlwapi)
	# The packaging for yaml-cpp on mingw64 seems to be broken, not providing a
	# libyaml-cpp.dll.a to be found by find_library inside of find_package(YAML)
	# when using FindYAML.cmake, just set the library directly
	set(YAML_LIBRARIES yaml-cpp)
else()
	set(LIN_LIBS dl)
	find_package(YAML REQUIRED)
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

set(SCOPEHAL_SOURCES
	base64.cpp
	scopehal.cpp
	avx_mathfun.cpp
	VulkanInit.cpp

	FileSystem.cpp
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
	SCPIFunctionGenerator.cpp
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

	StandardColors.cpp
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

	ComputePipeline.cpp
	ExportWizard.cpp
	FilterGraphExecutor.cpp
	PipelineCacheManager.cpp
	VulkanFFTPlan.cpp
	)

configure_file(config.h.in config.h)

add_library(scopehal SHARED
	${SCOPEHAL_SOURCES})

if(WIN32)
# mingw64 build using
# https://sdk.lunarg.com/sdk/download/1.3.224.1/windows/VulkanSDK-1.3.224.1-Installer.exe
# https://github.com/KhronosGroup/glslang.git (tags/sdk-1.3.224.1) static lib (SPIRV* glslang OGLCompiler GenericCodeGen MachineIndependent OSDependent)
target_link_libraries(scopehal
	${SIGCXX_LIBRARIES}
	${GTKMM_LIBRARIES}
	xptools
	log
	graphwidget
	${YAML_LIBRARIES}
	${LXI_LIBRARIES}
	${LINUXGPIB_LIBRARIES}
	${WIN_LIBS}
	${LIN_LIBS}
	${LIBFFTS_LIBRARIES}
	${OpenMP_CXX_LIBRARIES}
	${Vulkan_LIBRARIES}
	SPIRV
	SPIRV-Tools-opt
	SPIRV-Tools
	glslang
	Vulkan::Vulkan
	-Wl,--start-group
	OGLCompiler
	GenericCodeGen
	MachineIndependent
	-Wl,--end-group
	OSDependent
	glfw
	)
else()
target_link_libraries(scopehal
	${SIGCXX_LIBRARIES}
	${GTKMM_LIBRARIES}
	xptools
	log
	graphwidget
	${YAML_LIBRARIES}
	${LXI_LIBRARIES}
	${LINUXGPIB_LIBRARIES}
	${WIN_LIBS}
	${LIN_LIBS}
	${LIBFFTS_LIBRARIES}
	${OpenMP_CXX_LIBRARIES}
	${Vulkan_LIBRARIES}
	Vulkan::Vulkan
	Vulkan::glslang
	Vulkan::shaderc_combined
	glfw
	)
endif()

target_include_directories(scopehal
PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${LIBFFTS_INCLUDE_DIR}
	${YAML_INCLUDES}

	# TODO: this needs to come from FindPackage etc
	/usr/include/glslang/Include
	# for macOS:
	/usr/local/include/glslang/Include
	)

target_include_directories(scopehal
PUBLIC SYSTEM
	${CMAKE_CURRENT_SOURCE_DIR}/../VkFFT/vkFFT
	)

if(${HAS_LXI})
	target_compile_definitions(scopehal PUBLIC HAS_LXI)
endif()

if(${HAS_LINUXGPIB})
	target_compile_definitions(scopehal PUBLIC HAS_LINUXGPIB)
endif()

install(TARGETS scopehal LIBRARY)
