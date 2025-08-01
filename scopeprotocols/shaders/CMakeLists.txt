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
		BlackmanHarrisWindow.glsl
		ComplexBlackmanHarrisWindow.glsl
		ComplexCosineSumWindow.glsl
		ComplexRectangularWindow.glsl
		ComplexSpectrogramPostprocess.glsl
		ComplexToLogMagnitude.glsl
		ComplexToMagnitude.glsl
		CosineSumWindow.glsl
		DeEmbedOutOfPlace.glsl
		DeEmbedNormalization.glsl
		FIRFilter.glsl
		SpectrogramPostprocess.glsl
		SubtractFilter.glsl
		SubtractInPlace.glsl
		SubtractOutOfPlace.glsl
		Threshold.glsl
		UpsampleFilter.glsl
		WaterfallFilter.glsl
	)

add_dependencies(scopeprotocols protocolshaders)
