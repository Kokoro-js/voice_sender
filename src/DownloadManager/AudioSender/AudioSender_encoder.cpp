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
