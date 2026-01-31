function(add_compute_shaders target)
	cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES")

	set(spvfiles "")

	foreach(source ${arg_SOURCES})
		get_filename_component(base ${source} NAME_WE)

		set(outfile ${CMAKE_CURRENT_BINARY_DIR}/${base}.spv)
		set(spvfiles ${spvfiles} ${outfile})

		add_custom_command(
			OUTPUT ${outfile}
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
			COMMENT "Compile shader ${base}"
			COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.0 -c ${CMAKE_CURRENT_SOURCE_DIR}/${source} -g -o ${outfile})

		install(FILES ${outfile} DESTINATION share/ngscopeclient/shaders)

	endforeach()

	add_custom_target(${target}
		COMMAND ${CMAKE_COMMAND} -E true
		SOURCES ${spvfiles}
	)

endfunction()

add_compute_shaders(
	protocolshaders
	SOURCES
		AddFilter.glsl
		ACRMS.glsl
		ACRMS_Trend.glsl
		BaseMeasurement_FirstPass.glsl
		BaseMeasurement_FinalPass.glsl
		BlackmanHarrisWindow.glsl
		ClipFilter.glsl
		ClockRecoveryPLL_FirstPass.glsl
		ClockRecoveryPLL_SecondPass.glsl
		ClockRecoveryPLL_FinalPass.glsl
		ComplexBlackmanHarrisWindow.glsl
		ComplexCosineSumWindow.glsl
		ComplexRectangularWindow.glsl
		ComplexSpectrogramPostprocess.glsl
		ComplexToLogMagnitude.glsl
		ComplexToMagnitude.glsl
		CosineSumWindow.glsl
		DDJMeasurement.glsl
		DeEmbedOutOfPlace.glsl
		DeEmbedNormalization.glsl
		Downconvert.glsl
		DownsampleNoAAFilter.glsl
		DownsampleWithAAFilter.glsl
		DutyCycleMeasurement.glsl
		EmphasisFilter.glsl
		EnvelopeFilter.glsl
		Ethernet100BaseTX_4b5bDecode.glsl
		Ethernet100BaseTX_Descrambler.glsl
		Ethernet100BaseTX_FindSSD.glsl
		Ethernet100BaseTX_MLT3Decoder.glsl
		Ethernet100BaseTX_TrySync.glsl
		ExponentialMovingAverage.glsl
		EyePattern.glsl
		EyePattern_IndexSearch.glsl
		FallMeasurement_Uniform.glsl
		FallMeasurement_FinalPass.glsl
		FillSquarewaveAndDurations.glsl
		FIRFilter.glsl
		FrequencyMeasurement.glsl
		IQDemuxFilter.glsl
		IQDemuxFilterAlignment.glsl
		PAMEdgeDetector_InitialMerge.glsl
		PAMEdgeDetector_LevelCrossings.glsl
		PAMEdgeDetector_MergeCrossings.glsl
		PAMEdgeDetector_FinalMerge.glsl
		SpectrogramPostprocess.glsl
		SubtractFilter.glsl
		SubtractInPlace.glsl
		SubtractOutOfPlace.glsl
		SubtractVectorScalar.glsl
		TIEMeasurement_FirstPass.glsl
		TIEMeasurement_SecondPass.glsl
		Threshold.glsl
		UpsampleFilter.glsl
		VectorFrequency.glsl
		VectorPhase.glsl
		WaterfallFilter.glsl
	)

add_dependencies(scopeprotocols protocolshaders)
