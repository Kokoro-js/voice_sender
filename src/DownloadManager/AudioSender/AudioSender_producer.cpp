#include "AudioSender.h"

coro::task<int> AudioSender::encode_opus_frame(OpusEncoder *encoder, int16_t *pcm_data, size_t total_samples,
                                               OpusTempBuffer &temp_buffer, int max_data_bytes) {
    size_t wantedSamples = OPUS_FRAMESIZE * audio_props.channels;
    int total_encoded_bytes = 0;

    std::vector<uint8_t> encoded_frame(max_data_bytes);

    // 当总样本数和缓冲区的样本数足够时，进行编码
    while (total_samples + temp_buffer.temp_samples >= wantedSamples) {
        if (temp_buffer.temp_samples > 0) {
            // 将 PCM 数据填充到临时缓冲区进行编码
            size_t needed = wantedSamples - temp_buffer.temp_samples;
            std::memcpy(temp_buffer.temp_buffer.data() + temp_buffer.temp_samples, pcm_data, needed * sizeof(int16_t));

            pcm_data += needed;
            total_samples -= needed;

            int encoded_bytes = opus_encode(encoder, temp_buffer.temp_buffer.data(), OPUS_FRAMESIZE,
                                            encoded_frame.data(), max_data_bytes - total_encoded_bytes);
            if (encoded_bytes < 0) {
                co_return encoded_bytes; // 编码错误处理
            }
            encoded_frame.resize(encoded_bytes);
            co_await rb.produce(encoded_frame);

            /* int write_err = ope_encoder_write(enc, temp_buffer.temp_buffer.data(), OPUS_FRAMESIZE);
            if (write_err) {
                LOGE << "写入文件错误";
            }*/

            total_encoded_bytes += encoded_bytes;
            temp_buffer.temp_samples = 0; // 编码完成后重置临时样本数
        } else {
            // 直接从 pcm_data 进行编码
            int encoded_bytes = opus_encode(encoder, pcm_data, OPUS_FRAMESIZE, encoded_frame.data(),
                                            max_data_bytes - total_encoded_bytes);
            if (encoded_bytes < 0) {
                co_return encoded_bytes; // 编码错误处理
            }
            encoded_frame.resize(encoded_bytes);
            co_await rb.produce(encoded_frame);

            /* int write_err = ope_encoder_write(enc, pcm_data, OPUS_FRAMESIZE);
            if (write_err) {
                LOGE << "写入文件错误";
            }*/

            total_encoded_bytes += encoded_bytes;
            pcm_data += wantedSamples;
            total_samples -= wantedSamples;
        }
    }

    // Cache remaining samples if any
    if (total_samples > 0) {
        std::memcpy(temp_buffer.temp_buffer.data() + temp_buffer.temp_samples, pcm_data,
                    total_samples * sizeof(int16_t));
        temp_buffer.temp_samples += total_samples;
    }

    co_return total_encoded_bytes;
}

#include <samplerate.h>

coro::task<void> AudioSender::start_consumer(const bool &isStopped) {
    constexpr int MAX_DECODE_SIZE = 73728;
    auto buffer = std::make_unique<unsigned char[]>(MAX_DECODE_SIZE);

    constexpr int MAX_PCM_SIZE = 131072;
    constexpr int MAX_SAMPLES_COUNT = MAX_PCM_SIZE / sizeof(int16_t);
    auto float_buffer = std::make_unique<float[]>(MAX_SAMPLES_COUNT);
    auto resampledBuffer = std::make_unique<int16_t[]>(MAX_SAMPLES_COUNT);

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
            result = using_decoder->read(buffer.get(), MAX_DECODE_SIZE, &done);
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

        if (result == MPG123_ERR) continue;

        if (result == MPG123_OK || result == MPG123_NEW_FORMAT) {
            int channelCount = audio_props.channels;
            int totalSamples = done / audio_props.bytes_per_sample;
            audio_props.current_samples += totalSamples / channelCount;

            int16_t *pcm_data = nullptr;
            bool need_resample = audio_props.rate != TARGET_SAMPLE_RATE;

            // 定义 Lambda 函数用于重采样
            auto doResampleWork = [&float_buffer, &resampledBuffer](int &totalSamples, const int &channelCount,
                                                                    const long &rate) -> bool {
                SRC_DATA src_data;
                src_data.data_in = float_buffer.get();
                src_data.input_frames = totalSamples / channelCount;
                src_data.src_ratio = static_cast<double>(TARGET_SAMPLE_RATE) / rate;
                // 加1以防止取整导致数据不足
                src_data.output_frames = static_cast<long>(src_data.input_frames * src_data.src_ratio) + 1;

                std::vector<float> resampled_float_data(src_data.output_frames * channelCount);
                src_data.data_out = resampled_float_data.data();
                src_data.end_of_input = 0;

                int error = src_simple(&src_data, SRC_SINC_FASTEST, channelCount);
                if (error != 0) {
                    LOG(ERROR) << "重采样失败: " << src_strerror(error);
                    return false;
                }

                totalSamples = src_data.output_frames_gen * channelCount;
                src_float_to_short_array(resampled_float_data.data(), resampledBuffer.get(), totalSamples);
                return true;
            };

            switch (audio_props.encoding) {
                case AV_SAMPLE_FMT_S16:
                case MPG123_ENC_SIGNED_16: {
                    auto *raw_data = reinterpret_cast<int16_t *>(buffer.get());
                    if (!need_resample) {
                        // 采样率一致，直接使用原始数据
                        pcm_data = raw_data;
                    } else {
                        // 将 int16_t 转换为 float
                        src_short_to_float_array(raw_data, float_buffer.get(), totalSamples);
                        if (!doResampleWork(totalSamples, channelCount, audio_props.rate)) continue;
                        pcm_data = resampledBuffer.get();
                    }
                }
                    break;
                case AV_SAMPLE_FMT_S32: {
                    auto *raw_data = reinterpret_cast<int32_t *>(buffer.get());

                    // 将 int32_t 转换为 float
                    src_int_to_float_array(raw_data, float_buffer.get(), totalSamples);
                    if (!need_resample) {
                        src_float_to_short_array(float_buffer.get(), resampledBuffer.get(), totalSamples);
                        pcm_data = resampledBuffer.get();
                    } else {
                        if (!doResampleWork(totalSamples, channelCount, audio_props.rate)) continue;
                        pcm_data = resampledBuffer.get();
                    }
                }
                    break;
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    auto *raw_data = reinterpret_cast<float *>(buffer.get());
                    if (!need_resample) {
                        src_float_to_short_array(raw_data, resampledBuffer.get(), totalSamples);
                        pcm_data = resampledBuffer.get();
                    } else {
                        std::copy(raw_data, raw_data + totalSamples, float_buffer.get());
                        if (!doResampleWork(totalSamples, channelCount, audio_props.rate)) continue;
                        pcm_data = resampledBuffer.get();
                    }
                }
                    break;
                default: {
                    LOG(ERROR) << "不支持的音频格式, encoding=" << audio_props.encoding;
                    doSkip();
                    // 重置事件避免继续读
                    EventFeedDecoder.reset();
                    continue;
                }
            }

            int samples_per_channel = totalSamples / channelCount;

            // 编码 Opus
            auto encoded_length = co_await encode_opus_frame(opus_encoder_, pcm_data,
                                                             samples_per_channel * channelCount, opus_buffer,
                                                             MAX_DECODE_SIZE);
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