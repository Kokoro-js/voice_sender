#pragma once

#include "AudioDecoder.h"
#include <memory>
#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

class FfmpegDecoder : public AudioDecoder {
public:
    explicit FfmpegDecoder();

    ~FfmpegDecoder() override;

    int setup() override;

    int read(void *output_buffer, int buffer_size, size_t *data_size) override;

    int seek(double target_seconds) override;

    int getCurrentSamples() override;

    int getTotalSamples() override;

    void reset() override;

    AudioFormatInfo getAudioFormat() override;

private:
    AVFormatContext *format_ctx_;
    AVCodecContext *codec_ctx_;
    const AVCodec *codec_;
    AVPacket *packet_;
    AVFrame *frame_;

    int audio_stream_index_;
    int64_t total_samples_;

    AudioFormatInfo audio_format_;

    bool is_initialized_;
    bool needs_reinit_;

    // 持有 AVIOContext
    AVIOContext *avio_ctx_;
    static constexpr int avio_ctx_buffer_size = 4096;

    // 辅助函数
    int initialize_decoder();

    void cleanupFFmpeg();

    /**
     * @brief 将解码得到的 AVFrame 数据拷贝到外部缓冲区
     * @param frame 解码出的音频帧
     * @param output_buffer 目标输出缓冲区
     * @param buffer_size 输出缓冲区大小
     * @param data_size 实际拷贝的数据大小
     * @return 0 表示成功，负数表示失败
     */
    int copyDecodedData(AVFrame *frame,
                        void *output_buffer,
                        int buffer_size,
                        size_t *data_size);

    static const char *get_av_error_string(int errnum);

    // 禁用拷贝构造和赋值
    FfmpegDecoder(const FfmpegDecoder &) = delete;

    FfmpegDecoder &operator=(const FfmpegDecoder &) = delete;
};
