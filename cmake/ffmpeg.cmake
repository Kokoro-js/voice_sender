# 引入 CPM 模块
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# 配置 FFmpeg 下载和版本信息
CPMAddPackage(
        NAME FFmpeg
        GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg
        GIT_TAG n7.1
        DOWNLOAD_ONLY YES
)

# 定义 FFmpeg 安装路径
set(FFMPEG_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/ffmpeg)

if (FFmpeg_ADDED)
    include(ExternalProject)

    # 配置 FFmpeg 编译
    ExternalProject_Add(ffmpeg_project
            SOURCE_DIR ${FFmpeg_SOURCE_DIR}
            CONFIGURE_COMMAND ${FFmpeg_SOURCE_DIR}/configure
            --prefix=${FFMPEG_INSTALL_PREFIX}
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

    # 确保主项目依赖 FFmpeg 编译
    add_dependencies(${PROJECT_NAME} ffmpeg_project)

    # 自动添加 include 和 lib 路径
    include_directories(${FFMPEG_INSTALL_PREFIX}/include)
    link_directories(${FFMPEG_INSTALL_PREFIX}/lib)
endif ()

# 创建 FFmpeg INTERFACE 库
add_library(FFmpeg INTERFACE)

# 自动查找并链接 FFmpeg 的静态库
target_link_libraries(FFmpeg INTERFACE
        ${FFMPEG_INSTALL_PREFIX}/lib/libavformat.a
        ${FFMPEG_INSTALL_PREFIX}/lib/libavcodec.a
        ${FFMPEG_INSTALL_PREFIX}/lib/libavutil.a  # 确保 libavutil.a 在 libavformat.a 之后
)

# 链接项目和 FFmpeg
target_link_libraries(${PROJECT_NAME} PRIVATE FFmpeg)
