#include "AudioSender.h"
#include "AudioUtils.h"             // SIMD 优化函数
#include "AudioAlignedAlloc.h"      // 自定义的对齐分配封装
#include <samplerate.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstring>

// AudioSender 类的其他成员和声明请参见 AudioSender.h
// 主消费者协程：负责从解码器读取数据、处理转换、重采样、音量调整，并执行 Opus 编码
coro::task<void> AudioSender::start_consumer(const bool &isStopped) {
    // 创建 Opus 临时缓冲区
    OpusTempBuffer opus_buffer(OPUS_FRAMESIZE * 2);
    auto opus_output_buffer = std::make_unique<unsigned char[]>(MAX_DECODE_SIZE);

    int result = 0;
    size_t done = 0;

    while (true) {
        co_await tp_->yield();
        // 如果生产者已经停止，则通知等待者并退出协程
        if (isStopped) {
            VLOG(1) << "生产者关闭";
            co_return;
        }

        // 协程让出执行权，避免长时间占用线程
        co_await EventFeedDecoder;

        // 如果 task 为空，则重置解码事件，防止异常状态
        if (task == nullptr) {
            EventFeedDecoder.reset();
            continue;
        }

        {
            // 等待解码器事件，并锁定数据以确保线程安全
            co_await task->mutex_data.lock();
            result = using_decoder->read(read_output_buffer_.get(), MAX_DECODE_SIZE, &done);
        }

        // 根据解码器返回状态进行处理
        if (result == MPG123_DONE) {
            LOG(WARNING) << "读取完成";
            EventFeedDecoder.reset();
            // 通知其他模块音频读取结束
            EventReadFinshed.set();
            co_await tp_->schedule();
            continue;
        }
        if (result == MPG123_NEED_MORE) {
            EventFeedDecoder.reset();
            // 恢复下载
            curl_easy_pause(task->curl_handler.get(), CURLPAUSE_RECV_CONT);
            continue;
        }
        if (result == MPG123_ERR) {
            LOG(ERROR) << "解码器错误";
            EventFeedDecoder.reset();
            task->set_read_error(ReaderErrorCode::DecoderError, "解码器遇到错误");
            // 出现错误时直接跳过当前循环
            continue;
        }

        // 当读取到音频数据（MPG123_OK 或 MPG123_NEW_FORMAT）
        if (result == MPG123_OK || result == MPG123_NEW_FORMAT) {
            int channelCount = audio_props.channels;
            // 根据字节数计算样本总数
            int totalSamples = static_cast<int>(done / audio_props.bytes_per_sample);
            audio_props.current_samples += totalSamples / channelCount;

            int16_t *pcm_data = nullptr;
            // 判断是否需要重采样（目标采样率 TARGET_SAMPLE_RATE 在其他地方定义）
            bool need_resample = (audio_props.rate != TARGET_SAMPLE_RATE);
            // 判断是否需要调整音量（音量不为1.0时需要调整）
            bool applyVolume = (audio_props.volume != 1.0f);

            // 调用辅助函数处理音频数据的转换、重采样与音量调整
            if (!process_audio_frame(totalSamples, channelCount, need_resample, applyVolume,
                                     read_output_buffer_.get(), audio_props.encoding, pcm_data)) {
                // 如果处理失败，则跳过当前帧
                continue;
            }

            // 计算每个声道的样本数
            int samples_per_channel = totalSamples / channelCount;
            // 执行 Opus 编码（encode_opus_frame 为异步接口）
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

    co_return;
}

//------------------------------------------------------------------------------
// 辅助函数：重采样处理
// 将 float_buffer_ 中的音频数据进行重采样，并转换为 int16_t，同时应用音量调整
// 参数 totalSamples：输入时为原始样本总数，重采样后会更新为新的样本总数
// 参数 channelCount：声道数
// 参数 rate：原始采样率
// 参数 volume: 音量因子(必须由外部控制，那样才能做到音量放大)
// 返回值：true 表示重采样成功；false 表示重采样失败
//------------------------------------------------------------------------------
bool AudioSender::resample_audio(int &totalSamples, int channelCount, long rate, float volume) {
    SRC_DATA src_data;
    src_data.data_in = float_buffer_.get();
    src_data.input_frames = totalSamples / channelCount;
    src_data.src_ratio = static_cast<double>(TARGET_SAMPLE_RATE) / rate;
    // 为输出帧分配足够空间（+1 避免不足导致崩溃）
    src_data.output_frames = static_cast<long>(src_data.input_frames * src_data.src_ratio) + 1;
    src_data.end_of_input = 0;

    // 使用对齐内存存储重采样后的 float 数据
    AlignedMem::AlignedFloatVector resampled_float_data(src_data.output_frames * channelCount);
    src_data.data_out = resampled_float_data.data();

    // 使用快速重采样算法（SRC_SINC_FASTEST）
    int error = src_simple(&src_data, SRC_SINC_FASTEST, channelCount);
    if (error != 0) {
        LOG(ERROR) << "重采样失败: " << src_strerror(error);
        return false;
    }

    // 更新样本总数为重采样生成的样本数
    totalSamples = static_cast<int>(src_data.output_frames_gen * channelCount);

    // 将重采样后的 float 数据转换为 int16_t，同时应用音量调整
    AudioUtils::float_to_int16_optimized(
            resampled_float_data.data(),
            resampled_buffer_.get(),
            totalSamples,
            volume
    );
    return true;
}

//------------------------------------------------------------------------------
// 辅助函数：处理音频帧数据
// 根据不同的编码格式，执行必要的数据转换、重采样和音量调整，输出最终的 int16_t PCM 数据
// 参数说明：
//    totalSamples   —— 样本总数（单位：样本数，不是字节数），如果重采样会更新该值
//    channelCount   —— 声道数
//    need_resample  —— 是否需要重采样（原采样率 != 目标采样率）
//    applyVolume    —— 是否需要调整音量（音量因子 != 1.0）
//    raw_data       —— 原始音频数据缓冲区
//    encoding       —— 音频数据编码格式
//    pcm_data       —— 输出处理后的 PCM 数据指针
// 返回值：true 表示处理成功；false 表示处理失败
//------------------------------------------------------------------------------
bool AudioSender::process_audio_frame(int &totalSamples, int channelCount, bool need_resample, bool applyVolume,
                                      const void *raw_data, int encoding, int16_t *&pcm_data) {
    switch (encoding) {
        // 处理 16 位有符号 PCM 数据
        case AV_SAMPLE_FMT_S16:
        case MPG123_ENC_SIGNED_16: {
            auto *data = reinterpret_cast<const int16_t *>(raw_data);
            if (!need_resample) {
                if (applyVolume) {
                    // 调整音量，将调整后的数据存入 resampled_buffer_
                    AudioUtils::adjust_int16_volume(
                            data,                    // 输入数据
                            resampled_buffer_.get(), // 输出数据
                            totalSamples,            // 样本总数
                            audio_props.volume       // 音量因子
                    );
                    pcm_data = resampled_buffer_.get();
                } else {
                    // 不需要调整音量时直接使用原始数据
                    pcm_data = const_cast<int16_t *>(data);
                }
            } else {
                // 需要重采样：先将 int16 数据转换为 float（不在此处调整音量）
                AudioUtils::int16_to_float_optimized(
                        data,
                        float_buffer_.get(),
                        totalSamples,
                        1.0f
                );
                if (!resample_audio(totalSamples, channelCount, audio_props.rate, audio_props.volume)) {
                    return false;
                }
                pcm_data = resampled_buffer_.get();
            }
        }
            break;

            // 处理 32 位 PCM 数据
        case AV_SAMPLE_FMT_S32: {
            auto *data = reinterpret_cast<const int32_t *>(raw_data);
            // 将 int32 数据转换为 float 数据
            AudioUtils::int32_to_float_optimized(
                    data,
                    float_buffer_.get(),
                    totalSamples,
                    1.0f
            );
            if (!need_resample) {
                // 无需重采样时，直接将 float 数据转换为 int16_t，并根据需要调整音量
                float volumeFactor = applyVolume ? audio_props.volume : 1.0f;
                AudioUtils::float_to_int16_optimized(
                        float_buffer_.get(),
                        resampled_buffer_.get(),
                        totalSamples,
                        volumeFactor
                );
                pcm_data = resampled_buffer_.get();
            } else {
                if (!resample_audio(totalSamples, channelCount, audio_props.rate, audio_props.volume)) {
                    return false;
                }
                pcm_data = resampled_buffer_.get();
            }
        }
            break;

            // 处理 float 类型 PCM 数据（单通道或平面格式）
        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_FLTP: {
            // 对于已经标准化的 float 数据应该先乘以 32767，保证能听到东西。
            auto *data = reinterpret_cast<const float *>(raw_data);
            if (!need_resample) {
                // 直接将 float 数据转换为 int16_t，并根据需要调整音量
                float volumeFactor = applyVolume ? audio_props.volume : 1.0f;
                AudioUtils::float_to_int16_optimized(
                        data,
                        resampled_buffer_.get(),
                        totalSamples,
                        volumeFactor * 32767.0f
                );
                pcm_data = resampled_buffer_.get();
            } else {
                // 需要重采样时，先复制 float 数据到缓冲区
                std::copy(data, data + totalSamples, float_buffer_.get());
                if (!resample_audio(totalSamples, channelCount, audio_props.rate, audio_props.volume * 32767.0f)) {
                    return false;
                }
                pcm_data = resampled_buffer_.get();
            }
        }
            break;

        default: {
            LOG(ERROR) << "不支持的音频格式, encoding=" << encoding;
            doSkip();  // 执行跳帧操作
            EventFeedDecoder.reset();
            return false;
        }
    }
    return true;
}
