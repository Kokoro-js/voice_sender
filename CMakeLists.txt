# 设置 CMake 最低版本
cmake_minimum_required(VERSION 3.29)

set(PROJECT_NAME voice_connector)
project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # 添加静态链接的标志（GNU 编译器的例子）
    # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# 设置源文件目录
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

# 递归查找源文件和头文件
file(GLOB_RECURSE SOURCES "${SRC_DIR}/*.cpp" "${SRC_DIR}/**/*.cpp")
file(GLOB_RECURSE HEADERS "${SRC_DIR}/*.h" "${SRC_DIR}/**/*.h")

find_package(protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")
set(PROTO_FILES src/api/proto/Request.proto src/api/proto/Response.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# 定义可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS}
        src/DownloadManager/AudioSender/decoder/CustomIO.hpp)

# 查找依赖库 Opus、Poco、plog 和 CURL
find_package(Opus CONFIG REQUIRED)
find_package(plog CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(mpg123 CONFIG REQUIRED)
find_package(SampleRate CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)

# 添加 readerwriterqueue 依赖库
file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(UVGRTP_DISABLE_CRYPTO 1 CACHE BOOL "Disable Crypto++ in uvgRTP.")
CPMAddPackage("gh:ultravideo/uvgRTP@3.1.3")

set(LIBCORO_EXTERNAL_DEPENDENCIES ON CACHE BOOL "Use CMake find_package to resolve dependencies instead of embedded libraries.")
CPMAddPackage("gh:jbaldwin/libcoro@0.12.1")

CPMAddPackage("gh:cameron314/readerwriterqueue@1.0.6")

CPMAddPackage(
        NAME FFmpeg
        GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg
        GIT_TAG n7.0.2
        DOWNLOAD_ONLY YES
)

if(FFmpeg_ADDED)
    include(ExternalProject)
    ExternalProject_Add(ffmpeg_project
            SOURCE_DIR ${FFmpeg_SOURCE_DIR}
            CONFIGURE_COMMAND ${FFmpeg_SOURCE_DIR}/configure  # 使用 ${FFmpeg_SOURCE_DIR} 替换 <SOURCE_DIR>
            --prefix=${CMAKE_BINARY_DIR}/ffmpeg
            --disable-shared
            --enable-static
            --disable-programs
            --disable-everything
            --enable-avformat  # 启用 avformat
            --enable-parser=mp3,aac,flac
            --enable-decoder=mp3,aac,flac,alac
            --enable-demuxer=mp3,aac,flac,mov
            BUILD_COMMAND $(MAKE)
            INSTALL_COMMAND $(MAKE) install
            BUILD_IN_SOURCE 1
    )

    # Make sure your project links against the locally built FFmpeg libraries
    add_dependencies(${PROJECT_NAME} ffmpeg_project)
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/ffmpeg/include)
    link_directories(${CMAKE_CURRENT_BINARY_DIR}/ffmpeg/lib)
endif()

# 设置 uvgRTP 选项，并添加子目录
#add_subdirectory(external/uvgRTP)
#add_subdirectory(external/libcoro)
add_subdirectory(external/libopusenc)

# 添加头文件搜索路径，确保包含 curl 和 libcoro 的头文件
include_directories(
        ${CURL_INCLUDE_DIRS}  # 添加 curl 头文件路径
        include  # 添加项目 include 目录
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
        Opus::opus
        uvgrtp
        CURL::libcurl
        readerwriterqueue
        plog::plog
        libcoro
        MPG123::libmpg123 MPG123::libout123 MPG123::libsyn123
        SampleRate::samplerate
        cppzmq cppzmq-static
        opusenc
        protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite
        ${CMAKE_BINARY_DIR}/ffmpeg/lib/libavformat.a
        ${CMAKE_BINARY_DIR}/ffmpeg/lib/libavcodec.a
        ${CMAKE_BINARY_DIR}/ffmpeg/lib/libavutil.a
)

add_executable(test test.cpp)
target_link_libraries(test PRIVATE
        MPG123::libmpg123 MPG123::libout123 MPG123::libsyn123
)

add_executable(testb testb.cpp)
target_link_libraries(testb PRIVATE
        MPG123::libmpg123 MPG123::libout123 MPG123::libsyn123
)
