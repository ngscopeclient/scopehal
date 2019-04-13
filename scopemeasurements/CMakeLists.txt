include_directories(${GTKMM_INCLUDE_DIRS} ${SIGCXX_INCLUDE_DIRS})
link_directories(${GTKMM_LIBRARY_DIRS} ${SIGCXX_LIBRARY_DIRS})

set(SCOPEMEASUREMENTS_SOURCES
	AvgVoltageMeasurement.cpp
	BaseMeasurement.cpp
	EyeBitRateMeasurement.cpp
	EyeHeightMeasurement.cpp
	EyePeriodMeasurement.cpp
	Fall1090Measurement.cpp
	Fall2080Measurement.cpp
	FrequencyMeasurement.cpp
	MaxVoltageMeasurement.cpp
	MinVoltageMeasurement.cpp
	PeriodMeasurement.cpp
	PkPkVoltageMeasurement.cpp
	Rise1090Measurement.cpp
	Rise2080Measurement.cpp
	TopMeasurement.cpp

	scopemeasurements.cpp
	)

add_library(scopemeasurements SHARED
	${SCOPEMEASUREMENTS_SOURCES})

target_link_libraries(scopemeasurements
	scopeprotocols)

target_include_directories(scopemeasurements
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS scopemeasurements LIBRARY DESTINATION /usr/lib)
