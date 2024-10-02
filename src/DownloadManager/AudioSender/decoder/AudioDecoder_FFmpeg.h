#pragma once

#include "AudioDecoder.h"
#include "AudioDataBuffer.h" // 假设 AudioDataBuffer.h 已经定义
#include <plog/Log.h>
#include <memory>
#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class FfmpegDecoder : public AudioDecoder {
public:
    /**
     * @brief 构造函数
     *
     * @param data_buffer 外部已存在的 AudioDataBuffer 指针
     */
    explicit FfmpegDecoder(AudioDataBuffer* data_buffer);

    /**
     * @brief 析构函数
     */
    ~FfmpegDecoder() override;

    /**
     * @brief 设置解码器
     *
     * @return int 0 表示成功，非0表示失败
     */
    int setup() override;

    /**
     * @brief 读取解码后的音频数据
     *
     * @param output_buffer 输出缓冲区指针
     * @param buffer_size 输出缓冲区大小（字节）
     * @param data_size 实际读取的数据大小（字节）
     * @return int 0 表示成功，非0表示失败
     */
    int read(void* output_buffer, int buffer_size, size_t* data_size) override;

    /**
     * @brief 定位到指定的时间点
     *
     * @param target_seconds 目标时间（秒）
     * @return int 0 表示成功，非0表示失败
     */
    int seek(double target_seconds) override;

    /**
     * @brief 获取音频总样本数
     *
     * @return int 总样本数
     */
    int getTotalSamples() override;

    /**
     * @brief 重置解码器
     */
    void reset() override;

    /**
     * @brief 获取音频格式信息
     *
     * @return AudioFormatInfo 音频格式信息
     */
    AudioFormatInfo getAudioFormat() override;

private:
    // FFmpeg相关成员
    AVFormatContext* format_ctx_;
    AVCodecContext* codec_ctx_;
    const AVCodec* codec_;
    AVPacket* packet_;
    AVFrame* frame_;

    int audio_stream_index_;
    int64_t total_samples_;

    AudioFormatInfo audio_format_;

    bool is_initialized_;
    bool needs_reinit_;

    // 持有 AVIOContext
    AVIOContext* avio_ctx_;

    // 外部提供的 AudioDataBuffer，不在类的声明中
    // 通过 avio_alloc_context 的 opaque 参数传递

    // 辅助函数
    int initialize_decoder();
    void cleanupFFmpeg();

    // 辅助函数声明
    static const char* get_av_error_string(int errnum);

    // 禁用拷贝构造和赋值
    FfmpegDecoder(const FfmpegDecoder&) = delete;
    FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;
};
