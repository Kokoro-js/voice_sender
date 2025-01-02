# 创建一个接口库来封装 Release 优化选项
add_library(ReleaseOptimizations INTERFACE)

# 设置编译选项
target_compile_options(ReleaseOptimizations INTERFACE
        $<$<CONFIG:Release>:
        # 通用优化
        -O3                    # 提升优化级别以提高性能
        -march=x86-64          # 目前代码只支持 SSE，没办法
        -mtune=ivybridge       # 针对 e5 洋垃圾优化
        -funroll-loops         # 保持循环展开优化
        -fomit-frame-pointer   # 保留减少开销的优化
        -fstrict-aliasing      # 启用严格别名优化
        -fprefetch-loop-arrays # 启用循环数组预取优化
        -falign-functions=16   # 函数起始地址对齐，提高缓存命中率

        # 针对 GCC 和 Clang 的额外优化
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        -finline-functions      # 启用内联函数优化
        -flto                   # 启用链接时间优化 (LTO)
        >

        # 针对 MSVC 的优化
        $<$<CXX_COMPILER_ID:MSVC>:
        /O2
        /fp:precise           # 更安全的浮点设置
        /GL
        /Oy
        /favor:INTEL64
        /Gw                   # 启用全局优化
        /GS-                  # 禁用安全检查减少开销
        >
        >
)

# 添加预处理器定义
target_compile_definitions(ReleaseOptimizations INTERFACE
        $<$<CONFIG:Release>:IS_RELEASE_BUILD>
)

# 设置链接选项
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(ReleaseOptimizations INTERFACE -Wl,--gc-sections)
        message(STATUS "Applying Release optimizations for GNU/Clang: -O3 -march=x86-64 -funroll-loops -fomit-frame-pointer -finline-functions -fstrict-aliasing -fvisibility=hidden -ffunction-sections -fdata-sections -flto")
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_link_options(ReleaseOptimizations INTERFACE /LTCG)
        message(STATUS "Applying Release optimizations for MSVC: /O2 /fp:precise /GL /Oy /favor:INTEL64 /Gw /GS- /LTCG:incremental /OPT:REF /OPT:ICF")
    endif ()

elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(ReleaseOptimizations INTERFACE
            # Debug 配置的优化选项
            -march=x86-64 # 应该启用以调试 SIMD
            -Og                    # 针对调试的优化
            -g                     # 启用调试信息
            -fno-omit-frame-pointer # 保留帧指针，便于调试
            -fno-strict-aliasing   # 禁用严格别名优化以提高调试能力
            $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
            -fstack-protector-strong # 启用堆栈保护
            >

            $<$<CXX_COMPILER_ID:MSVC>:
            /Od                    # 禁用优化
            /Z7                    # 启用调试信息
            /RTC1                  # 启用运行时检查
            >
    )
    target_compile_definitions(ReleaseOptimizations INTERFACE
            DEBUG_BUILD
    )
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
