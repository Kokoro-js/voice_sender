#include "AudioSender.h"
#include "AudioUtils.h"             // 我们的 SIMD 优化函数
#include "AudioAlignedAlloc.h"      // 自定义的对齐分配封装
#include <samplerate.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstring>                  // std::memcpy

coro::task<void> AudioSender::start_consumer(const bool &isStopped) {
    constexpr int OPUS_FRAMESIZE = 1920; // 40ms
    OpusTempBuffer opus_buffer(OPUS_FRAMESIZE * 2);
    auto opus_output_buffer = std::make_unique<unsigned char[]>(MAX_DECODE_SIZE);

    int result;
    size_t done;

    while (!isStopped) {
        co_await tp_->yield();

        {
            co_await EventFeedDecoder;
            auto scoped_lock = co_await mutex_buffer.lock();
            result = using_decoder->read(read_output_buffer_.get(), MAX_DECODE_SIZE, &done);
        }
        // 处理解码器状态
        if (result == MPG123_DONE) {
            LOG(WARNING) << "读取完成";
            EventFeedDecoder.reset();

            // 用事件通知状态切换而不是在此处设置 state
            EventReadFinshed.set();
            co_await tp_->schedule();
            continue;
        }

        if (result == MPG123_NEED_MORE) {
            EventFeedDecoder.reset();
            continue;
        }

        if (result == MPG123_ERR) {
            LOG(ERROR) << "解码器错误";
            continue;
        }

        // 如果是 MPG123_OK 或 MPG123_NEW_FORMAT，说明读到音频数据
        if (result == MPG123_OK || result == MPG123_NEW_FORMAT) {
            int channelCount = audio_props.channels;
            int totalSamples = done / audio_props.bytes_per_sample;
            audio_props.current_samples += totalSamples / channelCount;

            int16_t *pcm_data = nullptr;
            bool need_resample = (audio_props.rate != TARGET_SAMPLE_RATE);

            // Lambda 函数，用于执行重采样
            auto doResampleWork = [&](int &totalSamples, const int &channelCount, const long &rate) -> bool {
                SRC_DATA src_data;
                src_data.data_in = float_buffer_.get();
                src_data.input_frames = totalSamples / channelCount;
                src_data.src_ratio = static_cast<double>(TARGET_SAMPLE_RATE) / rate;
                // +1 以避免不足导致的崩溃
                src_data.output_frames = static_cast<long>(src_data.input_frames * src_data.src_ratio) + 1;
                src_data.end_of_input = 0;

                // 使用对齐的 vector
                AlignedMem::AlignedFloatVector resampled_float_data(src_data.output_frames * channelCount);
                src_data.data_out = resampled_float_data.data();

                // 使用快速重采样算法
                int error = src_simple(&src_data, SRC_SINC_FASTEST, channelCount);
                if (error != 0) {
                    LOG(ERROR) << "重采样失败: " << src_strerror(error);
                    return false;
                }
                // 重采样后，转换回 int16_t 存到 resampled_buffer_
                totalSamples = src_data.output_frames_gen * channelCount;
                // SIMD，在重采样后再调整音量
                AudioUtils::float_to_int16_optimized(
                        resampled_float_data.data(),
                        resampled_buffer_.get(),
                        totalSamples,
                        audio_props.volume
                );
                return true;
            };

            bool shouldApplyVolume = (audio_props.volume != 1.0f);
            switch (audio_props.encoding) {
                case AV_SAMPLE_FMT_S16:
                case MPG123_ENC_SIGNED_16: {
                    auto *raw_data = reinterpret_cast<int16_t *>(read_output_buffer_.get());
                    if (!need_resample) {
                        if (shouldApplyVolume) {
                            // 调整音量，将调整后的数据存到 resampled_buffer_
                            AudioUtils::adjust_int16_volume(
                                    raw_data,               // 输入
                                    resampled_buffer_.get(),// 输出
                                    totalSamples,           // 样本数
                                    audio_props.volume      // 音量
                            );
                            pcm_data = resampled_buffer_.get();
                        } else {
                            // 直接使用原始数据
                            pcm_data = raw_data;
                        }
                    } else {
                        // int16 => float (SIMD)，不在此处调整音量
                        AudioUtils::int16_to_float_optimized(
                                raw_data,               // 输入
                                float_buffer_.get(),    // 输出
                                totalSamples,           // 样本数
                                1.0f                    // 不需要额外音量调整
                        );
                        if (!doResampleWork(totalSamples, channelCount, audio_props.rate)) {
                            continue;
                        }
                        pcm_data = resampled_buffer_.get();
                    }
                }
                    break;

                case AV_SAMPLE_FMT_S32: {
                    auto *raw_data = reinterpret_cast<int32_t *>(read_output_buffer_.get());
                    // int32 => float (SIMD)，不在此处调整音量
                    AudioUtils::int32_to_float_optimized(
                            raw_data,               // 输入
                            float_buffer_.get(),    // 输出
                            totalSamples,           // 样本数
                            1.0f                    // 不需要额外音量调整
                    );
                    if (!need_resample) {
                        if (shouldApplyVolume) {
                            // float => int16 with volume adjustment
                            AudioUtils::float_to_int16_optimized(
                                    float_buffer_.get(),       // 输入
                                    resampled_buffer_.get(),   // 输出
                                    totalSamples,              // 样本数
                                    audio_props.volume          // 音量
                            );
                            pcm_data = resampled_buffer_.get();
                        } else {
                            // 直接转换，无需调整音量
                            AudioUtils::float_to_int16_optimized(
                                    float_buffer_.get(),       // 输入
                                    resampled_buffer_.get(),   // 输出
                                    totalSamples,              // 样本数
                                    1.0f                        // 不需要音量调整
                            );
                            pcm_data = resampled_buffer_.get();
                        }
                    } else {
                        // 需要重采样，音量调整在 doResampleWork 中完成
                        if (!doResampleWork(totalSamples, channelCount, audio_props.rate)) {
                            continue;
                        }
                        pcm_data = resampled_buffer_.get();
                    }
                }
                    break;

                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    auto *raw_data = reinterpret_cast<float *>(read_output_buffer_.get());
                    if (!need_resample) {
                        if (shouldApplyVolume) {
                            // float => int16 with volume adjustment
                            AudioUtils::float_to_int16_optimized(
                                    raw_data,                   // 输入
                                    resampled_buffer_.get(),   // 输出
                                    totalSamples,              // 样本数
                                    audio_props.volume          // 音量
                            );
                            pcm_data = resampled_buffer_.get();
                        } else {
                            // 直接转换，无需调整音量
                            AudioUtils::float_to_int16_optimized(
                                    raw_data,                   // 输入
                                    resampled_buffer_.get(),   // 输出
                                    totalSamples,              // 样本数
                                    1.0f                        // 不需要音量调整
                            );
                            pcm_data = resampled_buffer_.get();
                        }
                    } else {
                        // 需要重采样，音量调整在 doResampleWork 中完成
                        // 先复制到 float_buffer_
                        std::copy(raw_data, raw_data + totalSamples, float_buffer_.get());
                        if (!doResampleWork(totalSamples, channelCount, audio_props.rate)) {
                            continue;
                        }
                        pcm_data = resampled_buffer_.get();
                    }
                }
                    break;

                default: {
                    LOG(ERROR) << "不支持的音频格式, encoding=" << audio_props.encoding;
                    doSkip();
                    EventFeedDecoder.reset();
                    continue;
                }
            }

            // 执行后续的 opus 编码
            int samples_per_channel = totalSamples / channelCount;
            auto encoded_length = co_await encode_opus_frame(
                    opus_encoder_,
                    pcm_data,
                    samples_per_channel * channelCount,
                    opus_buffer,
                    MAX_DECODE_SIZE
            );
            if (encoded_length < 0) {
                LOG(ERROR) << "Opus 编码错误: " << opus_strerror(encoded_length);
                continue;
            }
        }
    }

    VLOG(1) << "生产者关闭";
    rb.notify_waiters();
    co_return;
}
