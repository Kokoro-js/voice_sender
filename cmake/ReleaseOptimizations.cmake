# 创建接口库来封装 Release 优化选项
add_library(ReleaseOptimizations INTERFACE)

# 设置编译选项
target_compile_options(ReleaseOptimizations INTERFACE
        $<$<CONFIG:Debug>:
        -march=x86-64      # 指定目标架构，即使在 Debug 模式下也开启 SIMD 指令集
        -mtune=native      # 进行架构调优
        -mfpmath=sse       # 让浮点运算使用 SSE 而非 x87
        -mavx2             # 启用 AVX2 指令集
        -mfma              # 启用 FMA 指令
        -msse4.2           # 启用 SSE 4.2 指令
        -g                 # 生成调试符号
        -fno-omit-frame-pointer # 保留帧指针
        >

        $<$<CONFIG:Release>:
        # 通用优化
        -O2                    # 较高的优化级别，同时保持调试友好
        -march=x86-64          # 指定目标架构，当前代码仅支持 x86-64
        -mtune=ivybridge       # 针对特定硬件平台优化
        -mno-avx2              # 避免降频，e5 v2
        -mfma                  # 启用 FMA 指令
        -msse4.2               # 启用 SSE 4.2 指令
        -fstrict-aliasing      # 启用严格别名优化
        -falign-functions=16   # 函数起始地址对齐，提高缓存命中率
        -fprefetch-loop-arrays # 启用循环数组预取优化

        # 调试友好的设置
        -g                     # 生成调试符号
        -fno-omit-frame-pointer # 保留帧指针，便于调试
        -fno-unroll-loops      # 禁用循环展开，避免调试器难以跟踪

        # 如果特别要求不需要调试时，可启用以下选项：
        $<$<BOOL:${ENABLE_STRICT_OPTIMIZATION}>:
        -funroll-loops       # 循环展开，仅在明确不调试时启用
        -fomit-frame-pointer # 省略帧指针，仅在明确不调试时启用
        >

        # 针对 GCC 和 Clang 的额外优化
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -fvisibility=hidden # 隐藏符号以减少符号表大小
        -ffunction-sections # 按函数划分节，优化链接
        -fdata-sections     # 按数据划分节，优化链接
        -finline-functions  # 启用内联函数优化
        >

        # 针对 MSVC 的优化
        $<$<CXX_COMPILER_ID:MSVC>:
        /O2                  # 优化级别 /O2
        /fp:precise          # 更安全的浮点设置
        /Zi                  # 生成调试符号
        /Oy-                 # 禁用帧指针优化，便于调试
        >
        >
)

# 添加预处理器定义
target_compile_definitions(ReleaseOptimizations INTERFACE
        $<$<CONFIG:Release>:IS_RELEASE_BUILD>
        $<$<BOOL:${ENABLE_STRICT_OPTIMIZATION}>:STRICT_OPTIMIZATION>
)

# 设置链接选项
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(ReleaseOptimizations INTERFACE
                -Wl,--gc-sections    # 删除未使用的代码段
        )
        message(STATUS "Applying Release optimizations for GNU/Clang with debug symbols: -O2 -march=x86-64 -fno-omit-frame-pointer -fno-unroll-loops")
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_link_options(ReleaseOptimizations INTERFACE
                /OPT:REF             # 删除未使用的代码
                /OPT:ICF             # 合并重复的函数
        )
        message(STATUS "Applying Release optimizations for MSVC with debug symbols: /O2 /fp:precise /Zi /Oy- /OPT:REF /OPT:ICF")
    endif ()
endif ()

# 提供开关以控制严格优化
option(ENABLE_STRICT_OPTIMIZATION "Enable strict optimizations for Release builds (disable debugging-friendly options)" OFF)
if (ENABLE_STRICT_OPTIMIZATION)
    message(STATUS "Strict optimizations are enabled for Release builds.")
else ()
    message(STATUS "Debug-friendly optimizations are applied for Release builds.")
endif ()

# 提供开关以控制跨过程优化
option(ENABLE_IPO "Enable Interprocedural Optimization (LTO)" OFF) # 默认关闭 IPO
if (ENABLE_IPO)
    set_property(TARGET ReleaseOptimizations PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "Interprocedural Optimization (LTO) is enabled.")
else ()
    set_property(TARGET ReleaseOptimizations PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
    message(STATUS "Interprocedural Optimization (LTO) is disabled.")
endif ()

# 链接 ReleaseOptimizations 到目标
function(add_release_optimizations target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "Target '${target}' does not exist.")
    endif ()
    target_link_libraries(${target} PRIVATE ReleaseOptimizations)
endfunction()

# 自动检测编译器版本
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 10)
        target_compile_options(ReleaseOptimizations INTERFACE -flto=auto)
    endif ()
endif ()

# 添加分离调试符号的自定义命令
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_custom_command(TARGET voice_connector POST_BUILD
                # 分离调试符号到 .debug 文件
                COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:voice_connector> $<TARGET_FILE:voice_connector>.debug
                # 从原始可执行文件中剥离调试符号，并添加 .debug 链接
                COMMAND ${CMAKE_OBJCOPY} --strip-debug --add-gnu-debuglink=$<TARGET_FILE:voice_connector>.debug $<TARGET_FILE:voice_connector>
                # 进一步剥离未必要的符号
                COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:voice_connector>
                COMMENT "Separating debug symbols from voice_connector and adding debug link"
        )
    endif ()
endif ()

