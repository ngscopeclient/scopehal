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
			COMMAND glslc --target-env=vulkan1.0 -c ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${outfile})

		# TODO: install it

	endforeach()

	add_custom_target(${target}
		COMMAND ${CMAKE_COMMAND} -E true
		SOURCES ${spvfiles}
	)

endfunction()

add_compute_shaders(
	protocolshaders
	SOURCES
		Convert8BitSamples.glsl
		Convert16BitSamples.glsl
		SubtractFilter.glsl
	)

add_dependencies(scopeprotocols protocolshaders)
