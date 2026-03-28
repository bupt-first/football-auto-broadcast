cmake_minimum_required(VERSION 3.20)
project(football_auto_broadcast VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build/lib)

# 依赖库路径
set(OpenCV_DIR ${PROJECT_SOURCE_DIR}/lib/opencv)
find_package(OpenCV REQUIRED PATHS ${OpenCV_DIR} NO_DEFAULT_PATH)
include_directories(${OpenCV_INCLUDE_DIRS})

set(DLib_DIR ${PROJECT_SOURCE_DIR}/lib/dlib)
include_directories(${DLib_DIR}/include)
link_directories(${DLib_DIR}/lib)

set(FFmpeg_DIR ${PROJECT_SOURCE_DIR}/lib/ffmpeg)
include_directories(${FFmpeg_DIR}/include)
link_directories(${FFmpeg_DIR}/lib)

# 公共模块
include_directories(${PROJECT_SOURCE_DIR}/src/common)

# 收集各模块源码
file(GLOB VIDEO_STREAM_SRC ${PROJECT_SOURCE_DIR}/src/video_stream/*.h ${PROJECT_SOURCE_DIR}/src/video_stream/*.cpp)
file(GLOB DETECTION_SRC ${PROJECT_SOURCE_DIR}/src/detection/*.h ${PROJECT_SOURCE_DIR}/src/detection/*.cpp)
file(GLOB FACE_CAPTURE_SRC ${PROJECT_SOURCE_DIR}/src/face_capture/*.h ${PROJECT_SOURCE_DIR}/src/face_capture/*.cpp)
file(GLOB EDITOR_SRC ${PROJECT_SOURCE_DIR}/src/editor/*.h ${PROJECT_SOURCE_DIR}/src/editor/*.cpp)
file(GLOB UI_SRC ${PROJECT_SOURCE_DIR}/src/ui/*.h ${PROJECT_SOURCE_DIR}/src/ui/*.cpp)

# 生成可执行文件
add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCE_DIR}/src/common/common.h
    ${PROJECT_SOURCE_DIR}/src/common/common.cpp
    ${VIDEO_STREAM_SRC}
    ${DETECTION_SRC}
    ${FACE_CAPTURE_SRC}
    ${EDITOR_SRC}
    ${UI_SRC}
)

# 链接依赖库
target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    dlib.lib
    avcodec.lib avformat.lib avutil.lib swscale.lib
)

# Windows 下自动复制 DLL
if(WIN32)
    file(GLOB OpenCV_DLL ${OpenCV_DIR}/bin/*.dll)
    file(COPY ${OpenCV_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    
    file(GLOB DLib_DLL ${DLib_DIR}/bin/*.dll)
    file(COPY ${DLib_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    
    file(GLOB FFmpeg_DLL ${FFmpeg_DIR}/bin/*.dll)
    file(COPY ${FFmpeg_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()