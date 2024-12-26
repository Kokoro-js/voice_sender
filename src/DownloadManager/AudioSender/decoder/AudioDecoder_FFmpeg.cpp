#include "AudioDecoder_FFmpeg.h"
#include "CustomIO.hpp" // 假设包含自定义 IO 回调函数的声明
#include <cstring>

// 获取 FFmpeg 错误字符串的辅助函数
const char *FfmpegDecoder::get_av_error_string(int errnum) {
    static thread_local char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, errnum);
    return errbuf;
}

// 构造函数
FfmpegDecoder::FfmpegDecoder()
    : format_ctx_(nullptr), codec_ctx_(nullptr), codec_(nullptr),
      packet_(nullptr), frame_(nullptr),
      audio_stream_index_(-1), total_samples_(0),
      is_initialized_(false),
      needs_reinit_(false),
      avio_ctx_(nullptr) {
}

// 析构函数
FfmpegDecoder::~FfmpegDecoder() {
    cleanupFFmpeg();

    if (avio_ctx_) {
        if (avio_ctx_->buffer) {
            av_free(avio_ctx_->buffer);
            avio_ctx_->buffer = nullptr;
        }
        avio_context_free(&avio_ctx_);
        avio_ctx_ = nullptr;
    }
}

// 初始化解码器
int FfmpegDecoder::initialize_decoder() {
    // 清理现有的 FFmpeg 资源，但保留 AVIOContext

    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (avio_ctx_) {
        avio_context_free(&avio_ctx_);
        avio_ctx_ = nullptr;
    }

    if (auto buffer_warp = std::get_if<BufferWarp>(data_warpper_)) {
        unsigned char *avio_ctx_buffer = static_cast<unsigned char *>(av_malloc(avio_ctx_buffer_size));
        avio_ctx_ = avio_alloc_context(
            avio_ctx_buffer,
            avio_ctx_buffer_size,
            0, // 0 /表示只读
            buffer_warp, // opaque，传递给回调函数的用户数据
            CustomIO::custom_read, // 读回调
            nullptr, // 写回调
            CustomIO::custom_seek // seek回调
        );

        /*if (!avio_ctx_) {
            LOG(ERROR) << "Failed to create AVIOContext";
            av_free(avio_ctx_buffer);
            throw std::runtime_error("Failed to create AVIOContext");
        }*/
    } else if (auto *iobuf_warp = std::get_if<IOBufWarp>(data_warpper_)) {
        LOG(ERROR) << "暂不支持";
    }

    // 分配 AVFormatContext，但复用已有的 AVIOContext
    format_ctx_ = avformat_alloc_context();
    if (!format_ctx_) {
        LOG(ERROR) << "Failed to allocate AVFormatContext";
        return -1;
    }

    format_ctx_->pb = avio_ctx_; // 复用 AVIOContext

    // 打开输入（使用 nullptr 作为文件名，因为我们使用自定义 IO）
    int ret = avformat_open_input(&format_ctx_, nullptr, nullptr, nullptr);
    if (ret < 0) {
        LOG(ERROR) << "avformat_open_input failed: " << get_av_error_string(ret);
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    // 检索流信息
    ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (ret < 0) {
        LOG(ERROR) << "avformat_find_stream_info failed: " << get_av_error_string(ret);
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    // 查找最佳音频流
    ret = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec_, 0);
    if (ret < 0) {
        LOG(ERROR) << "Could not find audio stream in input: " << get_av_error_string(ret);
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    audio_stream_index_ = ret;

    // 分配解码器上下文
    codec_ctx_ = avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        LOG(ERROR) << "Failed to allocate codec context";
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    // 从输入流复制编解码器参数到解码器上下文
    ret = avcodec_parameters_to_context(codec_ctx_, format_ctx_->streams[audio_stream_index_]->codecpar);
    if (ret < 0) {
        LOG(ERROR) << "Failed to copy codec parameters to codec context: " << get_av_error_string(ret);
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    // 打开解码器
    ret = avcodec_open2(codec_ctx_, codec_, nullptr);
    if (ret < 0) {
        LOG(ERROR) << "Failed to open codec: " << get_av_error_string(ret);
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    // 分配包和帧
    packet_ = av_packet_alloc();
    if (!packet_) {
        LOG(ERROR) << "Failed to allocate AVPacket";
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    frame_ = av_frame_alloc();
    if (!frame_) {
        LOG(ERROR) << "Failed to allocate AVFrame";
        av_packet_free(&packet_);
        packet_ = nullptr;
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
        return -1;
    }

    // 设置音频格式信息
    audio_format_.sample_rate = codec_ctx_->sample_rate;
    audio_format_.channels = codec_ctx_->ch_layout.nb_channels;

    audio_format_.encoding = codec_ctx_->sample_fmt;
    audio_format_.bytes_per_sample = av_get_bytes_per_sample(codec_ctx_->sample_fmt);

    audio_format_.bits_per_samples = codec_ctx_->bits_per_raw_sample;

    // 计算总样本数（如果可用）
    if (format_ctx_->streams[audio_stream_index_]->duration != AV_NOPTS_VALUE && codec_ctx_->sample_rate > 0) {
        total_samples_ = static_cast<int64_t>(format_ctx_->streams[audio_stream_index_]->duration * codec_ctx_->
                                              sample_rate *
                                              av_q2d(format_ctx_->streams[audio_stream_index_]->time_base));
    } else {
        total_samples_ = 0; // 未知
    }

    is_initialized_ = true;
    needs_reinit_ = false;

    LOG(INFO) << "Decoder initialized: sample_rate=" << audio_format_.sample_rate << ", channels=" << audio_format_.
            channels << ", encoding=" << audio_format_.encoding;

    return 0;
}

// 清理 FFmpeg 相关资源
void FfmpegDecoder::cleanupFFmpeg() {
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    if (avio_ctx_) {
        avio_context_free(&avio_ctx_);
        avio_ctx_ = nullptr;
    }

    is_initialized_ = false;
    needs_reinit_ = false;
}

// 设置解码器
int FfmpegDecoder::setup() {
    return 0; // 外部逻辑要求 getAudioFormat 有数据时才开始 read，所以把实例化解码器延迟到那会。
    // return initialize_decoder();
}

// 读取解码后的音频数据
int FfmpegDecoder::read(void *output_buffer, int buffer_size, size_t *data_size) {
    if (needs_reinit_ || !is_initialized_) {
        // 重新初始化解码器
        int ret = initialize_decoder();
        if (ret != 0) {
            LOG(ERROR) << "initialize_decoder failed during read()";
            return -1;
        }
    }

    if (audio_format_.channels == 0 || audio_format_.sample_rate == 0) {
        LOG(ERROR) << "Audio format not properly initialized";
        return -1;
    }

    // 尝试读取和解码数据
    int ret = 0;
    *data_size = 0;

    while ((ret = av_read_frame(format_ctx_, packet_)) >= 0) {
        if (packet_->stream_index == audio_stream_index_) {
            // 发送包到解码器
            ret = avcodec_send_packet(codec_ctx_, packet_);
            if (ret < 0) {
                LOG(ERROR) << "avcodec_send_packet failed: " << get_av_error_string(ret);
                av_packet_unref(packet_);
                return -1;
            }

            // 接收帧从解码器
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx_, frame_);
                if (ret == AVERROR(EAGAIN)) {
                    return MPG123_NEED_MORE;
                }

                if (ret == AVERROR_EOF) {
                    break;
                }

                if (ret < 0) {
                    LOG(ERROR) << "avcodec_receive_frame failed: " << get_av_error_string(ret);
                    av_packet_unref(packet_);
                    return -1;
                }

                // 计算解码后数据的字节大小
                int bytes_per_sample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame_->format));
                if (bytes_per_sample < 0) {
                    LOG(ERROR) << "Failed to get bytes per sample";
                    av_packet_unref(packet_);
                    return -1;
                }

                int data_size_bytes = frame_->nb_samples * audio_format_.channels * bytes_per_sample;

                if (data_size_bytes > buffer_size) {
                    LOG(ERROR) << "Output buffer too small: required=" << data_size_bytes << ", available=" <<
                            buffer_size;
                    av_packet_unref(packet_);
                    return -1;
                }

                // 复制解码后的数据到输出缓冲区
                // 检查是否为平面格式
                bool is_planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame_->format));
                if (is_planar) {
                    uint8_t *out_ptr = static_cast<uint8_t *>(output_buffer);
                    for (int i = 0; i < frame_->nb_samples; ++i) {
                        for (int ch = 0; ch < audio_format_.channels; ++ch) {
                            memcpy(out_ptr, frame_->data[ch] + i * bytes_per_sample, bytes_per_sample);
                            out_ptr += bytes_per_sample;
                        }
                    }
                } else {
                    // 紧凑格式，直接复制
                    memcpy(output_buffer, frame_->data[0], data_size_bytes);
                }
                *data_size = data_size_bytes;

                av_packet_unref(packet_);
                return 0; // 成功读取一帧
            }
        }

        // 释放包
        av_packet_unref(packet_);
    }

    // 如果读取到文件末尾，尝试刷新解码器
    if (ret == AVERROR_EOF) {
        avcodec_send_packet(codec_ctx_, nullptr);
        while (avcodec_receive_frame(codec_ctx_, frame_) == 0) {
            int bytes_per_sample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame_->format));
            if (bytes_per_sample < 0) {
                LOG(ERROR) << "Failed to get bytes per sample";
                return -1;
            }

            int data_size_bytes = frame_->nb_samples * audio_format_.channels * bytes_per_sample;

            if (data_size_bytes > buffer_size) {
                LOG(ERROR) << "Output buffer too small: required=" << data_size_bytes << ", available=" << buffer_size;
                return -1;
            }

            bool is_planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame_->format));
            if (is_planar) {
                uint8_t *out_ptr = static_cast<uint8_t *>(output_buffer);
                for (int i = 0; i < frame_->nb_samples; ++i) {
                    for (int ch = 0; ch < audio_format_.channels; ++ch) {
                        memcpy(out_ptr, frame_->data[ch] + i * bytes_per_sample, bytes_per_sample);
                        out_ptr += bytes_per_sample;
                    }
                }
            } else {
                memcpy(output_buffer, frame_->data[0], data_size_bytes);
            }
            *data_size = data_size_bytes;

            return 0; // 成功读取一帧
        }
    }

    // 没有更多数据
    return MPG123_DONE;
}

// 定位到指定时间
int FfmpegDecoder::seek(double target_seconds) {
    if (!is_initialized_) {
        LOG(ERROR) << "Decoder not initialized";
        return -1;
    }

    // 转换目标时间到流的时间基
    AVRational time_base = format_ctx_->streams[audio_stream_index_]->time_base;
    int64_t target_timestamp = static_cast<int64_t>(target_seconds / av_q2d(time_base));

    // 执行查找
    int ret = av_seek_frame(format_ctx_, audio_stream_index_, target_timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOG(ERROR) << "av_seek_frame failed: " << get_av_error_string(ret);
        return -1;
    }

    // 刷新解码器缓冲区
    avcodec_flush_buffers(codec_ctx_);

    return 0;
}

// 获取当前解析到的样本数
int FfmpegDecoder::getCurrentSamples() {
    if (!is_initialized_ || audio_stream_index_ < 0) {
        LOG(ERROR) << "Decoder not initialized or audio stream not found";
        return -1;
    }

    // 使用 frame_->pts 获取当前帧的时间戳
    int64_t current_timestamp = frame_ ? frame_->pts : AV_NOPTS_VALUE;
    if (current_timestamp == AV_NOPTS_VALUE) {
        LOG(ERROR) << "No valid timestamp available for current frame";
        return -1;
    }

    // 转换时间戳为样本数
    AVRational time_base = format_ctx_->streams[audio_stream_index_]->time_base;
    int64_t current_samples = av_rescale_q(current_timestamp, time_base, {1, codec_ctx_->sample_rate});

    return current_samples;
}

// 获取音频总样本数
int FfmpegDecoder::getTotalSamples() {
    return static_cast<int>(total_samples_);
}

// 重置解码器
void FfmpegDecoder::reset() {
    // 标记需要重新初始化解码器
    cleanupFFmpeg();
    needs_reinit_ = true;
}

// 获取音频格式信息
AudioFormatInfo FfmpegDecoder::getAudioFormat() {
    initialize_decoder();
    return audio_format_;
}
