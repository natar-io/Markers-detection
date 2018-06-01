cmake_minimum_required (VERSION 3.2)
project (artkmarkers VERSION 0.1)

set (CMAKE_CXX_STANDARD 11)
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/module)

find_package(ARToolKitPlus REQUIRED)
if (NOT ARToolKitPlus_FOUND)
    message(FATAL_ERROR "Could not found ARToolKitPlus library")
endif()

find_package(OpenCV REQUIRED core opencv_imgcodecs opencv_highgui opencv_imgproc opencv_videoio)
if (NOT OpenCV_FOUND)
    message(FATAL_ERROR "Could not find OpenCV")
endif()

find_package(redisimagehelper)
find_package(hiredis)
find_package(RapidJSON)

set(SINGLE_MARKER_SOURCES
    src/singlemarker.cpp)

add_executable(singlemarker ${SINGLE_MARKER_SOURCES})
target_link_libraries(singlemarker ${ARToolKitPlus_LIBS})

set(MULTI_MARKERS_SOURCES
    src/multimarkers.cpp)

add_executable(multimarkers ${MULTI_MARKERS_SOURCES})
target_link_libraries(multimarkers ${ARToolKitPlus_LIBS})

if (HIREDIS_FOUND AND REDISIMAGEHELPER_FOUND AND RapidJSON_FOUND)
    set(CAMERA_MARKERS_SERVER_SOURCES
        src/cameramarkers_server.cpp)

    add_executable(artkmarkers_camera_server ${CAMERA_MARKERS_SERVER_SOURCES})
    target_link_libraries(artkmarkers_camera_server ${REDISIMAGEHELPER_LIBS} ${ARToolKitPlus_LIBS} ${HIREDIS_LIBS} ${OpenCV_LIBS})

    set(CAMERA_MARKERS_CLIENT_SOURCES
        src/cameramarkers_client.cpp)

    add_executable(artkmarkers_camera_client ${CAMERA_MARKERS_CLIENT_SOURCES})
    target_link_libraries(artkmarkers_camera_client ${REDISIMAGEHELPER_LIBS} ${ARToolKitPlus_LIBS} ${HIREDIS_LIBS} ${OpenCV_LIBS})
endif()
