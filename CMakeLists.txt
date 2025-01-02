# 设置 CMake 最低版本
cmake_minimum_required(VERSION 3.29)

set(PROJECT_NAME voice_connector)
project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)

# 设置源文件目录
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

# 递归查找源文件和头文件
file(GLOB_RECURSE SOURCES "${SRC_DIR}/*.cpp" "${SRC_DIR}/**/*.cpp")
file(GLOB_RECURSE HEADERS "${SRC_DIR}/*.h" "${SRC_DIR}/**/*.h")

find_package(protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

set(PROTO_FILES
        src/api/proto/Base.proto
        src/api/proto/Action.proto
        src/api/proto/Stream.proto
        src/api/proto/Request.proto
        src/api/proto/Response.proto
        src/api/proto/PlayList.proto
)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# 定义可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS}
        src/DownloadManager/AudioSender/decoder/CustomIO.hpp
)

# 包含优化设置
include(cmake/ReleaseOptimizations.cmake)
add_release_optimizations(${PROJECT_NAME})

# 查找依赖库
find_package(Opus CONFIG REQUIRED)
#find_package(plog CONFIG REQUIRED)
#find_package(opusenc CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
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
include(cmake/ffmpeg.cmake)
include(cmake/folly.cmake)

set(UVGRTP_DISABLE_CRYPTO 1 CACHE BOOL "Disable Crypto++ in uvgRTP.")
CPMAddPackage("gh:ultravideo/uvgRTP@3.1.3")

CPMAddPackage(
        NAME libcoro
        GITHUB_REPOSITORY "jbaldwin/libcoro"
        GIT_TAG "v0.12.1"
        OPTIONS "LIBCORO_EXTERNAL_DEPENDENCIES ON" "LIBCORO_BUILD_TESTS OFF" "LIBCORO_BUILD_EXAMPLES OFF"
)

CPMAddPackage(
        NAME figcone
        GITHUB_REPOSITORY "kamchatka-volcano/figcone"
        GIT_TAG "v3.2.0"
        OPTIONS "FIGCONE_USE_ALL OFF" "FIGCONE_USE_TOML ON"
)

CPMAddPackage(
        NAME xsimd
        GITHUB_REPOSITORY "xtensor-stack/xsimd"
        GIT_TAG "13.1.0"
        OPTIONS "BUILD_TESTS OFF"
)

CPMAddPackage(
        NAME yalantinglibs
        GITHUB_REPOSITORY "alibaba/yalantinglibs"
        GIT_TAG "0.3.9"
        OPTIONS "BUILD_EXAMPLES OFF" "BUILD_BENCHMARK OFF"
        "BUILD_UNIT_TESTS OFF" "GENERATE_BENCHMARK_DATA OFF" "BUILD_BUILD_CORO_RPC OFF"
)

CPMAddPackage("gh:cameron314/readerwriterqueue@1.0.6")

# 添加头文件搜索路径
include_directories(
        ${CURL_INCLUDE_DIRS}
        include
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
        Opus::opus
        uvgrtp
        CURL::libcurl
        readerwriterqueue
        # plog::plog
        libcoro
        MPG123::libmpg123 MPG123::libout123 MPG123::libsyn123
        SampleRate::samplerate
        cppzmq cppzmq-static
        # opusenc
        protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite
        glog::glog
        yalantinglibs::yalantinglibs
        figcone::figcone
        xsimd
)

#[[
# 测试可执行文件
target_link_libraries(test PRIVATE
        MPG123::libmpg123 MPG123::libout123 MPG123::libsyn123
)

add_executable(testb testb.cpp)
target_link_libraries(testb PRIVATE
        MPG123::libmpg123 MPG123::libout123 MPG123::libsyn123
)
]]
