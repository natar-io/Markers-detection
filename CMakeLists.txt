cmake_minimum_required (VERSION 3.2)
project (markers-server VERSION 0.1)

set (CMAKE_CXX_STANDARD 11)
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/module)

find_package(ARToolKitPlus REQUIRED)
if (NOT ARToolKitPlus_FOUND)
    message(FATAL_ERROR "Could not found ARToolKitPlus library")
endif()

set(SINGLE_MARKER_SOURCES
    src/SingleMarkerDetection.cpp)

add_executable(singlemarker-detection ${SINGLE_MARKER_SOURCES})
target_link_libraries(singlemarker-detection ${ARToolKitPlus_LIBS})

set(MULTI_MARKERS_SOURCES
    src/MultiMarkersDetection.cpp)

add_executable(multimarkers-detection ${MULTI_MARKERS_SOURCES})
target_link_libraries(multimarkers-detection ${ARToolKitPlus_LIBS})

