#ifndef AUDIO_UTILS_H
#define AUDIO_UTILS_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm> // std::min, std::max
#include <cmath>     // std::round
#include <limits>
#include <xsimd/xsimd.hpp>

#if defined(__SSE2__)

#include <emmintrin.h>

#endif
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace AudioUtils {

    /**************************************************************************
     * narrow_int32_to_int16
     * 将两个 xsimd::batch<int32_t>（分别代表低半部和高半部）饱和窄化为一个
     * xsimd::batch<int16_t>。对于 SSE2/AVX2 平台，利用 _mm_packs_epi32/_mm256_packs_epi32，
     * 否则退化到先存临时数组再转换。
     **************************************************************************/
    static inline xsimd::batch<int16_t> narrow_int32_to_int16(const xsimd::batch<int32_t> &low,
                                                              const xsimd::batch<int32_t> &high) {
        constexpr size_t batch32_size = xsimd::batch<int32_t>::size;
        if constexpr (batch32_size == 4) {
#if defined(__SSE2__)
            alignas(16) int32_t tmp_low[4];
            alignas(16) int32_t tmp_high[4];
            low.store_aligned(tmp_low);
            high.store_aligned(tmp_high);
            __m128i reg_low = _mm_load_si128(reinterpret_cast<const __m128i *>(tmp_low));
            __m128i reg_high = _mm_load_si128(reinterpret_cast<const __m128i *>(tmp_high));
            __m128i packed = _mm_packs_epi32(reg_low, reg_high);
            alignas(16) int16_t result[8];
            _mm_store_si128(reinterpret_cast<__m128i *>(result), packed);
            return xsimd::load_aligned(result);
#else
            alignas(xsimd::default_arch::alignment()) int32_t tmp_low[4];
            alignas(xsimd::default_arch::alignment()) int32_t tmp_high[4];
            low.store_aligned(tmp_low);
            high.store_aligned(tmp_high);
            int16_t tmp[8];
            for (std::size_t i = 0; i < 4; ++i)
            {
                tmp[i]     = static_cast<int16_t>(tmp_low[i]);
                tmp[i + 4] = static_cast<int16_t>(tmp_high[i]);
            }
            return xsimd::load_aligned(tmp);
#endif
        } else if constexpr (batch32_size == 8) {
#if defined(__AVX2__)
            alignas(32) int32_t tmp_low[8];
            alignas(32) int32_t tmp_high[8];
            low.store_aligned(tmp_low);
            high.store_aligned(tmp_high);
            __m256i reg_low  = _mm256_load_si256(reinterpret_cast<const __m256i*>(tmp_low));
            __m256i reg_high = _mm256_load_si256(reinterpret_cast<const __m256i*>(tmp_high));
            __m256i packed   = _mm256_packs_epi32(reg_low, reg_high);
            alignas(32) int16_t result[16];
            _mm256_store_si256(reinterpret_cast<__m256i*>(result), packed);
            return xsimd::load_aligned(result);
#else
            alignas(xsimd::default_arch::alignment()) int32_t tmp_low[8];
            alignas(xsimd::default_arch::alignment()) int32_t tmp_high[8];
            low.store_aligned(tmp_low);
            high.store_aligned(tmp_high);
            int16_t tmp[16];
            for (std::size_t i = 0; i < 8; ++i) {
                tmp[i] = static_cast<int16_t>(tmp_low[i]);
                tmp[i + 8] = static_cast<int16_t>(tmp_high[i]);
            }
            return xsimd::load_aligned(tmp);
#endif
        } else {
            constexpr size_t N = batch32_size;
            alignas(xsimd::default_arch::alignment()) int32_t tmp_low[N];
            alignas(xsimd::default_arch::alignment()) int32_t tmp_high[N];
            low.store_aligned(tmp_low);
            high.store_aligned(tmp_high);
            int16_t tmp[2 * N];
            for (std::size_t i = 0; i < N; ++i) {
                tmp[i] = static_cast<int16_t>(tmp_low[i]);
                tmp[i + N] = static_cast<int16_t>(tmp_high[i]);
            }
            return xsimd::load_aligned(tmp);
        }
    }

    /**************************************************************************
     * clamp_simd
     * 将 xsimd::batch<float> 内每个元素 clamp 到 T 的数值范围内，
     * 如 T 为 int16_t 则范围为 [-32768, 32767]。
     **************************************************************************/
    template<typename T>
    inline xsimd::batch<T> clamp_simd(const xsimd::batch<float> &valF) {
        const float fmin = static_cast<float>(std::numeric_limits<T>::min());
        const float fmax = static_cast<float>(std::numeric_limits<T>::max());
        auto clampedF = xsimd::min(xsimd::max(valF, xsimd::batch<float>(fmin)),
                                   xsimd::batch<float>(fmax));
        alignas(xsimd::default_arch::alignment()) float tmpF[xsimd::batch<float>::size];
        clampedF.store_aligned(tmpF);
        alignas(xsimd::default_arch::alignment()) T tmpI[xsimd::batch<T>::size];
        for (std::size_t i = 0; i < xsimd::batch<float>::size; ++i) {
            tmpI[i] = static_cast<T>(tmpF[i]);
        }
        return xsimd::load_aligned(tmpI);
    }

    /**************************************************************************
     * int16_to_float_optimized
     * 将 int16_t 数据转换为 float 数据（单通道），支持可选的音量调整。
     * 由于 xsimd::batch<int16_t>::size 在 SSE/AVX2 下分别为 8/16，所以一次载入 SIMD_SIZE 个 int16_t，
     * 再拆分为两组处理，每组转换为 int32_t->float。
     **************************************************************************/
    inline void int16_to_float_optimized(const int16_t *input,
                                         float *output,
                                         std::size_t size,
                                         float volume = 1.0f) {
        using batch_i16 = xsimd::batch<int16_t>;
        using batch_i32 = xsimd::batch<int32_t>;
        using batch_f32 = xsimd::batch<float>;

        constexpr std::size_t SIMD_SIZE = batch_i16::size;  // 如 SSE:8, AVX2:16
        constexpr std::size_t HALF_SIZE = SIMD_SIZE / 2;

        const bool need_volume = (volume != 1.0f);
        const batch_f32 volF(volume);

        std::size_t i = 0;
        for (; i + SIMD_SIZE <= size; i += SIMD_SIZE) {
            auto data = xsimd::load_aligned(input + i);
            alignas(xsimd::default_arch::alignment()) int16_t tmp16[SIMD_SIZE];
            data.store_aligned(tmp16);

            // 前半部分转换
            {
                alignas(xsimd::default_arch::alignment()) int32_t tmp32[HALF_SIZE];
                for (std::size_t j = 0; j < HALF_SIZE; ++j)
                    tmp32[j] = static_cast<int32_t>(tmp16[j]);
                auto i32Batch = xsimd::load_aligned(tmp32);
                auto fBatch = xsimd::to_float(i32Batch);
                if (need_volume)
                    fBatch *= volF;
                fBatch.store_aligned(output + i);
            }
            // 后半部分转换
            {
                alignas(xsimd::default_arch::alignment()) int32_t tmp32[HALF_SIZE];
                for (std::size_t j = 0; j < HALF_SIZE; ++j)
                    tmp32[j] = static_cast<int32_t>(tmp16[j + HALF_SIZE]);
                auto i32Batch = xsimd::load_aligned(tmp32);
                auto fBatch = xsimd::to_float(i32Batch);
                if (need_volume)
                    fBatch *= volF;
                fBatch.store_aligned(output + i + HALF_SIZE);
            }
        }
        // 处理不足 SIMD_SIZE 的尾部（标量）
        for (; i < size; ++i) {
            float val = static_cast<float>(input[i]);
            if (need_volume)
                val *= volume;
            output[i] = val;
        }
    }

    /**************************************************************************
     * int32_to_float_optimized
     * 将 int32_t 数据转换为 float 数据，支持可选的音量调整。
     **************************************************************************/
    inline void int32_to_float_optimized(const int32_t *input,
                                         float *output,
                                         std::size_t size,
                                         float volume = 1.0f) {
        using batch_i32 = xsimd::batch<int32_t>;
        using batch_f32 = xsimd::batch<float>;

        constexpr std::size_t SIMD_SIZE = batch_i32::size;
        const bool need_volume = (volume != 1.0f);
        const batch_f32 volF(volume);

        std::size_t i = 0;
        for (; i + SIMD_SIZE <= size; i += SIMD_SIZE) {
            auto i32Batch = xsimd::load_aligned(input + i);
            auto fBatch = xsimd::to_float(i32Batch);
            if (need_volume)
                fBatch *= volF;
            fBatch.store_aligned(output + i);
        }
        for (; i < size; ++i) {
            float val = static_cast<float>(input[i]);
            if (need_volume)
                val *= volume;
            output[i] = val;
        }
    }

    /**************************************************************************
     * float_to_int16_optimized
     * 将 float 数据转换为 int16_t 数据：
     *   1. 乘以音量因子（如有需要）；
     *   2. 四舍五入；
     *   3. 限幅到 int16_t 的范围；
     *   4. 转换为 int32_t 后饱和窄化为 int16_t。
     *
     * 为保证写入外部缓冲区地址依然对齐，假设外部输出缓冲区已对齐，
     * 且一次处理的输出元素数目应为 OUT_BATCH，其中
     *   OUT_BATCH = 2 * batch_f32::size = batch<int16_t>::size.
     **************************************************************************/
    inline void float_to_int16_optimized(const float *input,
                                         int16_t *output,
                                         std::size_t size,
                                         float volume = 1.0f) {
        using batch_f32 = xsimd::batch<float>;      // 对于 AVX2: size=8, SSE: size=4
        using batch_i32 = xsimd::batch<int32_t>;
        using batch_i16 = xsimd::batch<int16_t>;

        constexpr std::size_t IN_BATCH = batch_f32::size;   // 例如 8（AVX2）或 4（SSE）
        constexpr std::size_t OUT_BATCH = 2 * IN_BATCH;       // 对应 int16_t 的数量（16 或 8）
        std::size_t i = 0;
        const bool need_volume = (volume != 1.0f);
        const batch_f32 volF(volume);
        const batch_f32 min_val(static_cast<float>(std::numeric_limits<int16_t>::min()));
        const batch_f32 max_val(static_cast<float>(std::numeric_limits<int16_t>::max()));

        // 每次处理 OUT_BATCH 个 float，转换后生成 OUT_BATCH 个 int16_t
        for (; i + OUT_BATCH <= size; i += OUT_BATCH) {
            // 载入前半部分 float
            auto f_low = xsimd::load_aligned(input + i);
            // 载入后半部分 float
            auto f_high = xsimd::load_aligned(input + i + IN_BATCH);
            if (need_volume) {
                f_low *= volF;
                f_high *= volF;
            }
            f_low = xsimd::round(f_low);
            f_high = xsimd::round(f_high);
            f_low = xsimd::max(f_low, min_val);
            f_low = xsimd::min(f_low, max_val);
            f_high = xsimd::max(f_high, min_val);
            f_high = xsimd::min(f_high, max_val);
            batch_i32 i32_low = xsimd::to_int(f_low);
            batch_i32 i32_high = xsimd::to_int(f_high);
            auto i16_pack = narrow_int32_to_int16(i32_low, i32_high);
            // 此处 i 是 OUT_BATCH 的倍数，故 output+i 必然对齐
            i16_pack.store_aligned(output + i);
        }

        // 处理剩余不足 OUT_BATCH 个 float 的部分（采用标量处理）
        for (; i < size; ++i) {
            float f = input[i];
            if (need_volume)
                f *= volume;
            f = std::round(f);
            f = std::min(std::max(f, static_cast<float>(std::numeric_limits<int16_t>::min())),
                         static_cast<float>(std::numeric_limits<int16_t>::max()));
            output[i] = static_cast<int16_t>(f);
        }
    }

    /**************************************************************************
     * adjust_int16_volume
     * 调整 int16_t 数据的音量（支持 SIMD 加速）。
     * 如果 volume==1.0 则直接 memcpy，否则先转换为 float、调整音量、四舍五入、
     * 限幅后转换回 int16_t。
     **************************************************************************/
    inline void adjust_int16_volume(const int16_t *input,
                                    int16_t *output,
                                    std::size_t size,
                                    float volume = 1.0f) {
        using batch_i16 = xsimd::batch<int16_t>;
        using batch_i32 = xsimd::batch<int32_t>;
        using batch_f32 = xsimd::batch<float>;

        // 当音量因子为 1.0 时直接拷贝（注意：假设 input/output 均已对齐）
        if (volume == 1.0f) {
            if (input != output)
                std::memcpy(output, input, size * sizeof(int16_t));
            return;
        }
        const batch_f32 volF(volume);
        constexpr std::size_t SIMD_SIZE = batch_i16::size; // 如 SSE:8, AVX2:16
        constexpr std::size_t HALF_SIZE = SIMD_SIZE / 2;
        const batch_f32 min_val(static_cast<float>(std::numeric_limits<int16_t>::min()));
        const batch_f32 max_val(static_cast<float>(std::numeric_limits<int16_t>::max()));

        std::size_t i = 0;
        for (; i + SIMD_SIZE <= size; i += SIMD_SIZE) {
            auto data = xsimd::load_aligned(input + i);
            alignas(xsimd::default_arch::alignment()) int16_t tmp[SIMD_SIZE];
            data.store_aligned(tmp);

            // 处理前半部分
            {
                alignas(xsimd::default_arch::alignment()) int32_t tmp_low[HALF_SIZE];
                for (std::size_t j = 0; j < HALF_SIZE; ++j)
                    tmp_low[j] = static_cast<int32_t>(tmp[j]);
                auto i32_low = xsimd::load_aligned(tmp_low);
                auto f_low = xsimd::to_float(i32_low);
                f_low *= volF;
                f_low = xsimd::round(f_low);
                f_low = xsimd::max(f_low, min_val);
                f_low = xsimd::min(f_low, max_val);
                batch_i32 i32_low_out = xsimd::to_int(f_low);
                // 处理后半部分
                alignas(xsimd::default_arch::alignment()) int32_t tmp_high[HALF_SIZE];
                for (std::size_t j = 0; j < HALF_SIZE; ++j)
                    tmp_high[j] = static_cast<int32_t>(tmp[j + HALF_SIZE]);
                auto i32_high = xsimd::load_aligned(tmp_high);
                auto f_high = xsimd::to_float(i32_high);
                f_high *= volF;
                f_high = xsimd::round(f_high);
                f_high = xsimd::max(f_high, min_val);
                f_high = xsimd::min(f_high, max_val);
                batch_i32 i32_high_out = xsimd::to_int(f_high);
                auto i16_pack = narrow_int32_to_int16(i32_low_out, i32_high_out);
                i16_pack.store_aligned(output + i);
            }
        }
        for (; i < size; ++i) {
            float f = static_cast<float>(input[i]) * volume;
            f = std::round(f);
            f = std::min(std::max(f, static_cast<float>(std::numeric_limits<int16_t>::min())),
                         static_cast<float>(std::numeric_limits<int16_t>::max()));
            output[i] = static_cast<int16_t>(f);
        }
    }

} // namespace AudioUtils

#endif
