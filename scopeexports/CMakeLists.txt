include_directories(SYSTEM ${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEEXPORTS_SOURCES
	ExportWizard.cpp
	CSVExportWizard.cpp
	TouchstoneExportWizard.cpp
	VCDExportWizard.cpp

	scopeexports.cpp
	)

add_library(scopeexports SHARED
	${SCOPEEXPORTS_SOURCES})

if(APPLE_SILICON)
	target_link_libraries(scopeexports
		scopehal)
	target_include_directories(scopeexports
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
else()
	target_link_libraries(scopeexports
		scopehal
		${LIBFFTS_LIBRARIES})
	target_include_directories(scopeexports
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
		${LIBFFTS_INCLUDE_DIR})
endif()

install(TARGETS scopeexports LIBRARY)
