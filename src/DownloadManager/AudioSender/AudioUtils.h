#ifndef AUDIO_UTILS_H
#define AUDIO_UTILS_H

#include <cstddef>
#include <cstdint>
#include <xsimd/xsimd.hpp>
#include <algorithm> // std::min / std::max
#include <cmath>     // std::round
#include <limits>

namespace AudioUtils {

/**
 * @brief clamp_simd: 批量将 batch<float> 中每个元素 clamp 到 T(min~max) 范围，
 *        并最终返回 batch<T>；如 T=int16_t 则 clamp 到 [-32768, 32767].
 */
    template<typename T>
    inline xsimd::batch<T> clamp_simd(const xsimd::batch<float> &valF) {
        // 先把浮点数 clamp 到 T 的极值范围
        const float fmin = static_cast<float>(std::numeric_limits<T>::min());
        const float fmax = static_cast<float>(std::numeric_limits<T>::max());

        auto clampedF = xsimd::min(xsimd::max(valF, xsimd::batch<float>(fmin)),
                                   xsimd::batch<float>(fmax));

        // 再转换成 batch<T>
        alignas(xsimd::default_arch::alignment()) float tmpF[xsimd::batch<float>::size];
        clampedF.store_aligned(tmpF);

        alignas(xsimd::default_arch::alignment()) T tmpI[xsimd::batch<T>::size];
        for (size_t i = 0; i < xsimd::batch<float>::size; ++i) {
            tmpI[i] = static_cast<T>(tmpF[i]);
        }
        return xsimd::load_aligned(tmpI);
    }


/**
 * @brief int16 => float (单通道)，带可选音量
 *
 * - SSE2/SSE4.1 下，xsimd::batch<int16_t> 的大小通常是 8（128 bit）。
 * - 没有旧版的 split_low/high 时，我们依然可以一次载入 8×int16，
 *   然后存到临时数组，再分两批 4×int16 => float。
 * - 若支持更底层 SSE4.1 intrinsic，可手动用 `_mm_cvtepi16_epi32` 拆分 8×int16。
 */
    inline void int16_to_float_optimized(const int16_t *input,
                                         float *output,
                                         size_t size,
                                         float volume = 1.0f) {
        using batch_i16 = xsimd::batch<int16_t>;  // 128 bit => 8×int16
        using batch_i32 = xsimd::batch<int32_t>;  // 128 bit => 4×int32
        using batch_f32 = xsimd::batch<float>;    // 128 bit => 4×float

        constexpr size_t SIMD_SIZE = batch_i16::size; // = 8
        constexpr size_t HALF_SIZE = SIMD_SIZE / 2;   // = 4

        const bool need_volume = (volume != 1.0f);
        const batch_f32 volF(volume);

        size_t i = 0;
        for (; i + SIMD_SIZE <= size; i += SIMD_SIZE) {
            // 1) 一次载入 8×int16
            auto data_8 = xsimd::load_aligned(input + i);

            // 2) 存到临时数组，以便拆分两批处理
            alignas(xsimd::default_arch::alignment()) int16_t tmp16[SIMD_SIZE];
            data_8.store_aligned(tmp16);

            // 3) 处理前4个 int16 => float
            {
                // 3.1) 转到 int32
                int32_t tmp32[HALF_SIZE];
                for (size_t k = 0; k < HALF_SIZE; ++k) {
                    tmp32[k] = static_cast<int32_t>(tmp16[k]);
                }
                // 3.2) batch<int32> => batch<float>
                auto i32Batch = xsimd::load_aligned(tmp32);
                auto fBatch = xsimd::to_float(i32Batch);
                if (need_volume) {
                    fBatch *= volF;
                }
                fBatch.store_aligned(output + i);
            }

            // 4) 处理后4个 int16 => float
            {
                int32_t tmp32[HALF_SIZE];
                for (size_t k = 0; k < HALF_SIZE; ++k) {
                    tmp32[k] = static_cast<int32_t>(tmp16[4 + k]);
                }
                auto i32Batch = xsimd::load_aligned(tmp32);
                auto fBatch = xsimd::to_float(i32Batch);
                if (need_volume) {
                    fBatch *= volF;
                }
                fBatch.store_aligned(output + i + 4);
            }
        }

        // 处理剩余不足 8 个的部分（标量方式）
        for (; i < size; ++i) {
            float val = static_cast<float>(input[i]);
            if (need_volume) {
                val *= volume;
            }
            output[i] = val;
        }
    }


/**
 * @brief int32 => float，带可选音量
 *
 * - SSE2/SSE4.1 下，xsimd::batch<int32_t> 的大小通常是 4（128 bit）。
 * - 处理方法是：载入 4×int32 => to_float => 可选乘音量 => 存回。
 */
    inline void int32_to_float_optimized(const int32_t *input,
                                         float *output,
                                         size_t size,
                                         float volume = 1.0f) {
        using batch_i32 = xsimd::batch<int32_t>; // 128 bit => 4×int32
        using batch_f32 = xsimd::batch<float>;   // 128 bit => 4×float

        constexpr size_t SIMD_SIZE = batch_i32::size; // = 4

        const bool need_volume = (volume != 1.0f);
        const batch_f32 volF(volume);

        size_t i = 0;
        for (; i + SIMD_SIZE <= size; i += SIMD_SIZE) {
            auto i32Batch = xsimd::load_aligned(input + i);
            auto fBatch = xsimd::to_float(i32Batch);
            if (need_volume) {
                fBatch *= volF;
            }
            fBatch.store_aligned(output + i);
        }

        // 处理剩余不足 4 个的部分（标量方式）
        for (; i < size; ++i) {
            float val = static_cast<float>(input[i]);
            if (need_volume) {
                val *= volume;
            }
            output[i] = val;
        }
    }


/**
 * @brief float => int16，带可选音量；需要做 round + clamp 到 int16 范围。
 *
 * - SSE2/SSE4.1 下，xsimd::batch<float> 的大小通常是 4（128 bit）。
 * - 可以用 xsimd::round (若可用) 对 batch<float> 做批量 round。
 * - clamp 到 [-32768, 32767] 后再转 batch<int16_t> 输出。
 */
    inline void float_to_int16_optimized(const float *input,
                                         int16_t *output,
                                         size_t size,
                                         float volume = 1.0f) {
        // 确保 output 本身是对齐的
        assert(((reinterpret_cast<uintptr_t>(output) % xsimd::default_arch::alignment()) == 0) &&
               "output buffer is not properly aligned");

        using batch_f32 = xsimd::batch<float>;   // 128 bit => 4×float
        using batch_i16 = xsimd::batch<int16_t>; // 128 bit => 8×int16

        constexpr size_t SIMD_SIZE = batch_f32::size; // = 4
        constexpr size_t STEP_SIZE = 8; // 8 int16_t per iteration (16 bytes)

        const bool need_volume = (volume != 1.0f);
        const batch_f32 volF(volume);

        size_t i = 0;
        // 处理能够整除8的部分
        for (; i + STEP_SIZE <= size; i += STEP_SIZE) {
            // 断言当前 output + i 是否对齐
            assert(((reinterpret_cast<uintptr_t>(output + i) % xsimd::default_arch::alignment()) == 0) &&
                   "output + i is not properly aligned");

            // 处理前4个 float
            auto fBatch1 = xsimd::load_aligned(input + i); // Load 4 floats
            if (need_volume) {
                fBatch1 *= volF;
            }
            fBatch1 = xsimd::round(fBatch1);
            fBatch1 = xsimd::max(fBatch1, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::min())));
            fBatch1 = xsimd::min(fBatch1, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::max())));
            auto i16Batch1 = clamp_simd<int16_t>(fBatch1);
            i16Batch1.store_aligned(output + i); // output + i 是对齐的

            // 处理后4个 float
            auto fBatch2 = xsimd::load_aligned(input + i + SIMD_SIZE); // Load next 4 floats
            if (need_volume) {
                fBatch2 *= volF;
            }
            fBatch2 = xsimd::round(fBatch2);
            fBatch2 = xsimd::max(fBatch2, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::min())));
            fBatch2 = xsimd::min(fBatch2, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::max())));
            auto i16Batch2 = clamp_simd<int16_t>(fBatch2);
            i16Batch2.store_unaligned(output + i + 4); // output + i + 4 不是对齐的，使用 store_unaligned
        }

        // 处理剩余不足8个的部分
        for (; i < size; ++i) {
            float val = input[i];
            if (need_volume) {
                val *= volume;
            }
            val = std::round(val);
            val = std::max(static_cast<float>(std::numeric_limits<int16_t>::min()),
                           std::min(val, static_cast<float>(std::numeric_limits<int16_t>::max())));
            output[i] = static_cast<int16_t>(val);
        }
    }

    inline void adjust_int16_volume(const int16_t *input,
                                    int16_t *output,
                                    size_t size,
                                    float volume = 1.0f) {
        using batch_i16 = xsimd::batch<int16_t>;  // 128 bit => 8×int16
        using batch_f32 = xsimd::batch<float>;    // 128 bit => 4×float

        // 若音量系数为 1.0，则直接拷贝即可
        if (volume == 1.0f) {
            if (input != output) {
                std::memcpy(output, input, size * sizeof(int16_t));
            }
            return;
        }

        // 构造批量的 volume
        const batch_f32 volF(volume);

        constexpr size_t SIMD_SIZE = batch_i16::size; // = 8 (SSE2/4.1 下)
        constexpr size_t HALF_SIZE = SIMD_SIZE / 2;   // = 4

        size_t i = 0;
        for (; i + SIMD_SIZE <= size; i += SIMD_SIZE) {
            // 1) 一次载入 8×int16
            auto data_8 = xsimd::load_aligned(input + i);

            // 2) 由于 xsimd::to_float 等常见函数只能一次处理 4×int16，
            //    这里和您之前类似，把它存进 tmp16 再分两段处理。
            alignas(xsimd::default_arch::alignment()) int16_t tmp16[SIMD_SIZE];
            data_8.store_aligned(tmp16);

            // ---- 前 4 个 int16 => float => 乘音量 => clamp => 转回 int16
            {
                int32_t tmp32[HALF_SIZE];
                for (size_t k = 0; k < HALF_SIZE; ++k) {
                    tmp32[k] = static_cast<int32_t>(tmp16[k]);
                }
                auto i32Batch = xsimd::load_aligned(tmp32);      // 4×int32
                auto fBatch = xsimd::to_float(i32Batch);       // => 4×float
                fBatch *= volF;                            // multiply volume
                fBatch = xsimd::round(fBatch);            // round
                // clamp 到 int16_t 的取值范围
                fBatch = xsimd::max(fBatch, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::min())));
                fBatch = xsimd::min(fBatch, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::max())));
                // 转回 int16
                auto i16Batch = clamp_simd<int16_t>(fBatch);

                // 存回 output（前半段 4 个）
                alignas(xsimd::default_arch::alignment()) int16_t tmpI16[HALF_SIZE];
                i16Batch.store_aligned(tmpI16);
                for (size_t k = 0; k < HALF_SIZE; ++k) {
                    output[i + k] = tmpI16[k];
                }
            }

            // ---- 后 4 个 int16 => float => 乘音量 => clamp => 转回 int16
            {
                int32_t tmp32[HALF_SIZE];
                for (size_t k = 0; k < HALF_SIZE; ++k) {
                    tmp32[k] = static_cast<int32_t>(tmp16[4 + k]);
                }
                auto i32Batch = xsimd::load_aligned(tmp32);
                auto fBatch = xsimd::to_float(i32Batch);
                fBatch *= volF;
                fBatch = xsimd::round(fBatch);
                // clamp
                fBatch = xsimd::max(fBatch, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::min())));
                fBatch = xsimd::min(fBatch, batch_f32(static_cast<float>(std::numeric_limits<int16_t>::max())));
                auto i16Batch = clamp_simd<int16_t>(fBatch);

                // 存回 output（后半段 4 个）
                alignas(xsimd::default_arch::alignment()) int16_t tmpI16[HALF_SIZE];
                i16Batch.store_aligned(tmpI16);
                for (size_t k = 0; k < HALF_SIZE; ++k) {
                    output[i + 4 + k] = tmpI16[k];
                }
            }
        }

        // 处理剩余不足 8 个的部分（标量方式）
        for (; i < size; ++i) {
            float val = static_cast<float>(input[i]) * volume;
            val = std::round(val);
            val = std::max(static_cast<float>(std::numeric_limits<int16_t>::min()),
                           std::min(val, static_cast<float>(std::numeric_limits<int16_t>::max())));
            output[i] = static_cast<int16_t>(val);
        }
    }
} // namespace AudioUtils

#endif // AUDIO_UTILS_H
