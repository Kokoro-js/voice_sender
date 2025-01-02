// AudioAlignedAlloc.h
#pragma once

#include <memory>
#include <vector>
#include <cstddef>
#include <xsimd/xsimd.hpp>

namespace AlignedMem {
    // 自定义 deleter，用于自动调用 xsimd::aligned_free
    template<typename T>
    struct XsimdAlignedDeleter {
        void operator()(T *ptr) const noexcept {
            if (ptr) {
                xsimd::aligned_free(ptr);
            }
        }
    };

    // 创建对齐后的 unique_ptr
    template<typename T>
    inline std::unique_ptr<T[], XsimdAlignedDeleter<T>> make_aligned_unique(std::size_t count) {
        constexpr std::size_t alignment = xsimd::default_arch::alignment(); // 16/32/64...
        void *raw = xsimd::aligned_malloc(count * sizeof(T), alignment);
        if (!raw) {
            throw std::bad_alloc();
        }
        return std::unique_ptr<T[], XsimdAlignedDeleter<T>>(
                reinterpret_cast<T *>(raw),
                XsimdAlignedDeleter<T>()
        );
    }

    // 类型别名
    template<typename T>
    using AlignedUniquePtr = std::unique_ptr<T[], XsimdAlignedDeleter<T>>;

    // 自定义对齐分配器，用于 std::vector
    template<typename T, std::size_t Alignment>
    struct AlignedAllocator {
        using value_type = T;

        AlignedAllocator() noexcept {}

        template<typename U>
        AlignedAllocator(const AlignedAllocator<U, Alignment> &) noexcept {}

        T *allocate(std::size_t n) {
            void *ptr = xsimd::aligned_malloc(n * sizeof(T), Alignment);
            if (!ptr) {
                throw std::bad_alloc();
            }
            return reinterpret_cast<T *>(ptr);
        }

        void deallocate(T *ptr, std::size_t) noexcept {
            xsimd::aligned_free(ptr);
        }

        // 添加 rebind 结构体以支持类型重绑定
        template<typename U>
        struct rebind {
            using other = AlignedAllocator<U, Alignment>;
        };
    };

    // 比较两个 AlignedAllocator 是否相等
    template<typename T, typename U, std::size_t Alignment>
    bool operator==(const AlignedAllocator<T, Alignment> &, const AlignedAllocator<U, Alignment> &) { return true; }

    template<typename T, typename U, std::size_t Alignment>
    bool operator!=(const AlignedAllocator<T, Alignment> &a, const AlignedAllocator<U, Alignment> &b) {
        return !(a == b);
    }

    // 使用自定义对齐分配器的 std::vector<float> 类型别名
    using AlignedFloatVector = std::vector<float, AlignedAllocator<float, xsimd::default_arch::alignment()>>;

} // namespace AlignedMem
