cmake_minimum_required(VERSION 3.20)
project(football_auto_broadcast VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/lib)

if(NOT DEFINED OpenCV_DIR)
    if(EXISTS ${PROJECT_SOURCE_DIR}/lib/opencv)
        set(OpenCV_DIR ${PROJECT_SOURCE_DIR}/lib/opencv)
    elseif(EXISTS "E:/opencv/build")
        set(OpenCV_DIR "E:/opencv/build")
    endif()
endif()

find_package(OpenCV REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(DLib_DIR ${PROJECT_SOURCE_DIR}/lib/dlib)
set(FFmpeg_DIR ${PROJECT_SOURCE_DIR}/lib/ffmpeg)

if(EXISTS ${OpenCV_DIR}/x64/vc16/bin)
    set(OpenCV_BIN_DIR ${OpenCV_DIR}/x64/vc16/bin)
elseif(EXISTS ${OpenCV_DIR}/bin)
    set(OpenCV_BIN_DIR ${OpenCV_DIR}/bin)
endif()

include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/common
)

if(EXISTS ${DLib_DIR}/include)
    include_directories(${DLib_DIR}/include)
endif()

if(EXISTS ${FFmpeg_DIR}/include)
    include_directories(${FFmpeg_DIR}/include)
endif()

if(EXISTS ${DLib_DIR}/lib)
    link_directories(${DLib_DIR}/lib)
endif()

if(EXISTS ${FFmpeg_DIR}/lib)
    link_directories(${FFmpeg_DIR}/lib)
endif()

file(GLOB VIDEO_STREAM_SRC ${PROJECT_SOURCE_DIR}/src/video_stream/*.h ${PROJECT_SOURCE_DIR}/src/video_stream/*.cpp)
file(GLOB DETECTION_SRC ${PROJECT_SOURCE_DIR}/src/detection/*.h ${PROJECT_SOURCE_DIR}/src/detection/*.cpp)
file(GLOB FACE_CAPTURE_SRC ${PROJECT_SOURCE_DIR}/src/face_capture/*.h ${PROJECT_SOURCE_DIR}/src/face_capture/*.cpp)
file(GLOB EDITOR_SRC ${PROJECT_SOURCE_DIR}/src/editor/*.h ${PROJECT_SOURCE_DIR}/src/editor/*.cpp)
file(GLOB UI_SRC ${PROJECT_SOURCE_DIR}/src/ui/*.h ${PROJECT_SOURCE_DIR}/src/ui/*.cpp)

add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    ${PROJECT_SOURCE_DIR}/src/common/common.h
    ${PROJECT_SOURCE_DIR}/src/common/common.cpp
    ${VIDEO_STREAM_SRC}
    ${DETECTION_SRC}
    ${FACE_CAPTURE_SRC}
    ${EDITOR_SRC}
    ${UI_SRC}
)

target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    Qt6::Widgets
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    FINAL_OUTPUT_VIDEO_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/output video"
)

if(EXISTS ${DLib_DIR}/lib/dlib.lib)
    target_link_libraries(${PROJECT_NAME} dlib.lib)
endif()

if(EXISTS ${FFmpeg_DIR}/lib/avcodec.lib)
    target_link_libraries(${PROJECT_NAME} avcodec.lib avformat.lib avutil.lib swscale.lib)
endif()

if(WIN32)
    if(DEFINED OpenCV_BIN_DIR)
        file(GLOB OpenCV_DLL ${OpenCV_BIN_DIR}/*.dll)
        file(COPY ${OpenCV_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    endif()

    set(OpenCV_FACE_CASCADE ${OpenCV_DIR}/etc/haarcascades/haarcascade_frontalface_default.xml)
    if(EXISTS ${OpenCV_FACE_CASCADE})
        file(COPY ${OpenCV_FACE_CASCADE} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    endif()

    file(GLOB DLib_DLL ${DLib_DIR}/bin/*.dll)
    file(COPY ${DLib_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

    file(GLOB FFmpeg_DLL ${FFmpeg_DIR}/bin/*.dll)
    file(COPY ${FFmpeg_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

    if(OpenCV_DLL)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OpenCV_DLL}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()

    if(EXISTS ${OpenCV_FACE_CASCADE})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OpenCV_FACE_CASCADE}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
endif()
