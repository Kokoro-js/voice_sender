#include "AudioDecoder_FFmpeg.h"
#include "CustomIO.hpp"

#include <cstring>
#include <glog/logging.h> // 包含glog头文件

// 获取 FFmpeg 错误字符串的辅助函数
const char *FfmpegDecoder::get_av_error_string(int errnum) {
    static thread_local char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, errnum);
    return errbuf;
}

// 构造函数
FfmpegDecoder::FfmpegDecoder()
        : format_ctx_(nullptr), codec_ctx_(nullptr), codec_(nullptr), packet_(nullptr), frame_(nullptr),
          audio_stream_index_(-1), total_samples_(0), is_initialized_(false), needs_reinit_(false), avio_ctx_(nullptr) {
    VLOG(1) << "[FfmpegDecoder] Constructor called.";
}

// 析构函数
FfmpegDecoder::~FfmpegDecoder() {
    VLOG(1) << "[FfmpegDecoder] Destructor called.";
    cleanupFFmpeg();
}

// 清理 FFmpeg 相关资源
void FfmpegDecoder::cleanupFFmpeg() {
    VLOG(1) << "[FfmpegDecoder] cleanupFFmpeg() start.";

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
        // avio_context_free 会连同 buffer 一起释放（若由 avio_alloc_context 分配）
        avio_context_free(&avio_ctx_);
        avio_ctx_ = nullptr;
    }

    is_initialized_ = false;
    needs_reinit_ = false;

    VLOG(1) << "[FfmpegDecoder] cleanupFFmpeg() done.";
}

// 设置解码器（仅标记，真正初始化延迟到 getAudioFormat() 或 read() 时）
int FfmpegDecoder::setup() {
    VLOG(1) << "[FfmpegDecoder] setup() called but does nothing. Delayed init.";
    return MPG123_OK;
}

// 修改后的 initialize_decoder() 部分
int FfmpegDecoder::initialize_decoder() {
    VLOG(1) << "[FfmpegDecoder] initialize_decoder() start.";
    cleanupFFmpeg();

    // 1. 创建自定义 AVIOContext（保持原有逻辑不变）
    if (auto buffer_warp = std::get_if<BufferWarp>(data_warpper_)) {
        auto *avio_ctx_buffer = static_cast<unsigned char *>(av_malloc(avio_ctx_buffer_size));
        if (!avio_ctx_buffer) {
            LOG(ERROR) << "[FfmpegDecoder] av_malloc for avio_ctx_buffer failed.";
            return MPG123_ERR;
        }
        avio_ctx_ = avio_alloc_context(
                avio_ctx_buffer,
                avio_ctx_buffer_size,
                0,
                buffer_warp,
                CustomIO::custom_read,
                nullptr,
                CustomIO::custom_seek
        );
        if (!avio_ctx_) {
            LOG(ERROR) << "[FfmpegDecoder] avio_alloc_context failed.";
            av_free(avio_ctx_buffer);
            return MPG123_ERR;
        }
    } else if (auto *iobuf_warp = std::get_if<IOBufWarp>(data_warpper_)) {
        auto *avio_ctx_buffer = static_cast<unsigned char *>(av_malloc(avio_ctx_buffer_size));
        if (!avio_ctx_buffer) {
            LOG(ERROR) << "[FfmpegDecoder] av_malloc for avio_ctx_buffer failed.";
            return MPG123_ERR;
        }
        avio_ctx_ = avio_alloc_context(
                avio_ctx_buffer,
                avio_ctx_buffer_size,
                0,
                iobuf_warp,
                CustomIO::iobuf_ffmpeg_read,
                nullptr,
                CustomIO::iobuf_ffmpeg_seek
        );
        if (!avio_ctx_) {
            LOG(ERROR) << "[FfmpegDecoder] avio_alloc_context failed.";
            av_free(avio_ctx_buffer);
            return MPG123_ERR;
        }
    }

    // 2. 创建 AVFormatContext 并指定自定义 IO
    format_ctx_ = avformat_alloc_context();
    if (!format_ctx_) {
        LOG(ERROR) << "[FfmpegDecoder] avformat_alloc_context failed.";
        cleanupFFmpeg();
        return MPG123_ERR;
    }
    format_ctx_->pb = avio_ctx_;

    // 3. 打开输入（使用自定义 IO）
    int ret = avformat_open_input(&format_ctx_, nullptr, nullptr, nullptr);
    if (ret < 0) {
        LOG(ERROR) << "[FfmpegDecoder] avformat_open_input failed: " << get_av_error_string(ret);
        cleanupFFmpeg();
        return MPG123_ERR;
    }

    // 4. 检索流信息
    ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (ret < 0) {
        LOG(ERROR) << "[FfmpegDecoder] avformat_find_stream_info failed: " << get_av_error_string(ret);
        cleanupFFmpeg();
        return MPG123_ERR;
    }

    // 5. 查找最佳音频流
    ret = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec_, 0);
    if (ret < 0) {
        LOG(ERROR) << "[FfmpegDecoder] Could not find audio stream: " << get_av_error_string(ret);
        cleanupFFmpeg();
        return MPG123_ERR;
    }
    audio_stream_index_ = ret;

    // 6. 分配解码器上下文
    codec_ctx_ = avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        LOG(ERROR) << "[FfmpegDecoder] avcodec_alloc_context3 failed.";
        cleanupFFmpeg();
        return MPG123_ERR;
    }

    // 7. 复制流参数到解码器上下文
    ret = avcodec_parameters_to_context(codec_ctx_, format_ctx_->streams[audio_stream_index_]->codecpar);
    if (ret < 0) {
        LOG(ERROR) << "[FfmpegDecoder] avcodec_parameters_to_context failed: " << get_av_error_string(ret);
        cleanupFFmpeg();
        return MPG123_ERR;
    }

    // 8. 打开解码器
    ret = avcodec_open2(codec_ctx_, codec_, nullptr);
    if (ret < 0) {
        LOG(ERROR) << "[FfmpegDecoder] avcodec_open2 failed: " << get_av_error_string(ret);
        cleanupFFmpeg();
        return MPG123_ERR;
    }

    // 9. 分配 packet_ 和 frame_
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!packet_ || !frame_) {
        LOG(ERROR) << "[FfmpegDecoder] av_packet_alloc / av_frame_alloc failed.";
        cleanupFFmpeg();
        return MPG123_ERR;
    }

    // 10. 设置音频格式信息（关键修改：使用 codec_ctx_->channels 优先）
    audio_format_.sample_rate = codec_ctx_->sample_rate;
    // 从 AVCodecParameters 中获取通道数（注意：AVCodecParameters 存在于 format_ctx_->streams[index]->codecpar 中）
    AVCodecParameters *codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
    if (codecpar->ch_layout.nb_channels > 0) {
        audio_format_.channels = codecpar->ch_layout.nb_channels;
    } else if (codec_ctx_->ch_layout.nb_channels) {
        // 如果设置了 channel_layout，则通过该值获得通道数
        audio_format_.channels = codec_ctx_->ch_layout.nb_channels;
    } else {
        // 无法获取时，设置一个合理的默认值（例如立体声）
        audio_format_.channels = 2;
    }
    audio_format_.encoding = codec_ctx_->sample_fmt;
    audio_format_.bytes_per_sample = av_get_bytes_per_sample(codec_ctx_->sample_fmt);
    audio_format_.bits_per_samples = (codec_ctx_->bits_per_raw_sample > 0)
                                     ? codec_ctx_->bits_per_raw_sample
                                     : audio_format_.bytes_per_sample * 8;

    // 11. 计算总样本数（若可用）
    AVStream *audio_stream = format_ctx_->streams[audio_stream_index_];
    if (audio_stream->duration != AV_NOPTS_VALUE && codec_ctx_->sample_rate > 0) {
        double duration_sec = audio_stream->duration * av_q2d(audio_stream->time_base);
        total_samples_ = static_cast<int64_t>(duration_sec * codec_ctx_->sample_rate);
    } else {
        total_samples_ = 0; // 未知
    }

    // 初始化完成
    is_initialized_ = true;
    needs_reinit_ = false;
    VLOG(1) << "[FfmpegDecoder] initialize_decoder() success. sample_rate=" << audio_format_.sample_rate
            << ", channels=" << audio_format_.channels
            << ", format=" << audio_format_.encoding;
    return MPG123_OK;
}


// 将解码后的帧数据拷贝到外部缓冲区
int FfmpegDecoder::copyDecodedData(AVFrame *frame,
                                   void *output_buffer,
                                   int buffer_size,
                                   size_t *data_size) {
    int bytes_per_sample = av_get_bytes_per_sample(
            static_cast<AVSampleFormat>(frame->format)
    );
    if (bytes_per_sample < 0) {
        LOG(ERROR) << "[FfmpegDecoder] copyDecodedData: invalid bytes_per_sample.";
        return MPG123_ERR;
    }

    int data_size_bytes = frame->nb_samples
                          * audio_format_.channels
                          * bytes_per_sample;
    if (data_size_bytes > buffer_size) {
        LOG(ERROR) << "[FfmpegDecoder] copyDecodedData: Output buffer too small. "
                   << "Required=" << data_size_bytes
                   << ", Available=" << buffer_size;
        return MPG123_ERR;
    }

    bool is_planar = av_sample_fmt_is_planar(
            static_cast<AVSampleFormat>(frame->format)
    );
    if (is_planar) {
        // 平面格式，需要交织拷贝
        uint8_t *out_ptr = static_cast<uint8_t *>(output_buffer);
        for (int i = 0; i < frame->nb_samples; ++i) {
            for (int ch = 0; ch < audio_format_.channels; ++ch) {
                memcpy(out_ptr,
                       frame->data[ch] + i * bytes_per_sample,
                       bytes_per_sample);
                out_ptr += bytes_per_sample;
            }
        }
    } else {
        // 紧凑格式可以直接复制
        memcpy(output_buffer, frame->data[0], data_size_bytes);
    }

    *data_size = static_cast<size_t>(data_size_bytes);
    return MPG123_OK;
}

// 读取解码后的音频数据
int FfmpegDecoder::read(void *output_buffer, int buffer_size, size_t *data_size) {
    if (!data_size) {
        LOG(ERROR) << "[FfmpegDecoder] read: data_size is null!";
        return MPG123_ERR;
    }
    *data_size = 0;

    // 检查初始化状态
    if (!is_initialized_ || needs_reinit_) {
        VLOG(1) << "[FfmpegDecoder] read: Initializing decoder.";
        if (initialize_decoder() != MPG123_OK) {
            LOG(ERROR) << "[FfmpegDecoder] read: initialize_decoder failed.";
            return MPG123_ERR;
        }
    }

    if (audio_format_.channels == 0 || audio_format_.sample_rate == 0) {
        LOG(ERROR) << "[FfmpegDecoder] read: Audio format not properly initialized.";
        return MPG123_ERR;
    }

    int ret = 0;
    size_t total_copied = 0;
    uint8_t *out_ptr = static_cast<uint8_t *>(output_buffer);
    int consecutive_errors = 0;
    const int MAX_CONSECUTIVE_ERRORS = 5; // 最大连续错误次数

    // 循环读包并解码，尝试读取多个帧
    while (total_copied < static_cast<size_t>(buffer_size)) {
        ret = av_read_frame(format_ctx_, packet_);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                return MPG123_DONE;
            else if (ret == AVERROR(EAGAIN))
                return MPG123_NEED_MORE;  // 当没有足够数据时返回 NEED_MORE
            else {
                LOG(ERROR) << "[FfmpegDecoder] read: av_read_frame failed: " << get_av_error_string(ret);
            }

            // 原有代码：刷新解码器（发送空包）以获取剩余数据
            ret = avcodec_send_packet(codec_ctx_, nullptr);
            if (ret < 0) {
                LOG(ERROR) << "[FfmpegDecoder] read: avcodec_send_packet (flush) failed: " << get_av_error_string(ret);
                return MPG123_ERR;
            }

            // 尝试接收剩余的帧
            while (avcodec_receive_frame(codec_ctx_, frame_) == 0) {
                size_t current_data_size = 0;
                int copy_ret = copyDecodedData(frame_, out_ptr, buffer_size - total_copied, &current_data_size);
                if (copy_ret < 0) {
                    LOG(ERROR) << "[FfmpegDecoder] read: copyDecodedData failed during flushing.";
                    return MPG123_ERR;
                }
                out_ptr += current_data_size;
                total_copied += current_data_size;
                if (total_copied >= static_cast<size_t>(buffer_size)) {
                    *data_size = total_copied;
                    return MPG123_OK;
                }
            }

            // 如果已经读取了一些数据，返回
            if (total_copied > 0) {
                *data_size = total_copied;
                return MPG123_OK;
            }
            return MPG123_DONE;
        }

        if (packet_->stream_index != audio_stream_index_) {
            // 若不是音频流，直接释放包并继续
            av_packet_unref(packet_);
            continue;
        }

        // 在发送前保存 packet 的 pts
        int64_t packet_pts = packet_->pts;

        ret = avcodec_send_packet(codec_ctx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0) {
            LOG(ERROR) << "[FfmpegDecoder] read: avcodec_send_packet failed: " << get_av_error_string(ret);
            consecutive_errors++;
            if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
                LOG(ERROR) << "[FfmpegDecoder] read: Exceeded maximum consecutive errors.";
                return MPG123_ERR;
            }
            // 跳过损坏的数据包，继续处理下一个包
            continue;
        }

        // 重置错误计数
        consecutive_errors = 0;

        // 不断接收可用的帧
        while (true) {
            ret = avcodec_receive_frame(codec_ctx_, frame_);
            if (ret == AVERROR(EAGAIN))
                break;
            else if (ret == AVERROR_EOF) {
                VLOG(1) << "[FfmpegDecoder] read: Decoding finished.";
                break;
            } else if (ret < 0) {
                LOG(ERROR) << "[FfmpegDecoder] read: avcodec_receive_frame error: " << get_av_error_string(ret);
                consecutive_errors++;
                if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
                    LOG(ERROR) << "[FfmpegDecoder] read: Exceeded maximum consecutive errors.";
                    return MPG123_ERR;
                }
                // 尝试跳过损坏的帧
                av_frame_unref(frame_);
                continue;
            }

            // 如果 frame->pts 为无效值，则尝试用之前保存的 packet pts 赋值
            if (frame_->pts == AV_NOPTS_VALUE && packet_pts != AV_NOPTS_VALUE) {
                frame_->pts = packet_pts;
            }

            consecutive_errors = 0;

            // 将解码后的数据拷贝到输出缓冲
            size_t current_data_size = 0;
            int copy_ret = copyDecodedData(frame_, out_ptr, buffer_size - total_copied, &current_data_size);
            if (copy_ret < 0) {
                LOG(ERROR) << "[FfmpegDecoder] read: copyDecodedData failed.";
                return MPG123_ERR;
            }

            // 更新指针和总大小
            out_ptr += current_data_size;
            total_copied += current_data_size;

            VLOG(2) << "[FfmpegDecoder] read: Decoded one frame, size=" << current_data_size << " bytes.";

            // 如果输出缓冲区已满，返回
            if (total_copied >= static_cast<size_t>(buffer_size)) {
                // VLOG(1) << "[FfmpegDecoder] read: Output buffer filled.";
                *data_size = total_copied;
                return MPG123_OK;
            }
        }
    }
    *data_size = total_copied;
    return MPG123_OK;
}

// 定位到指定时间（秒）
int FfmpegDecoder::seek(double target_seconds) {
    if (!is_initialized_) {
        LOG(ERROR) << "[FfmpegDecoder] seek: Decoder not initialized.";
        return MPG123_ERR;
    }
    AVStream *audio_stream = format_ctx_->streams[audio_stream_index_];
    AVRational time_base = audio_stream->time_base;
    int64_t target_ts = static_cast<int64_t>(target_seconds / av_q2d(time_base));

    int ret = av_seek_frame(format_ctx_, audio_stream_index_, target_ts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
    if (ret < 0) {
        LOG(ERROR) << "[FfmpegDecoder] seek: av_seek_frame failed: " << get_av_error_string(ret);
        return MPG123_ERR;
    }
    // 刷新解码器缓冲区
    avcodec_flush_buffers(codec_ctx_);
    VLOG(1) << "[FfmpegDecoder] seek: Seek to " << target_seconds << "s succeeded.";

    // 重用成员变量 packet_ 尝试解码第一帧以更新 frame_
    while (true) {
        ret = av_read_frame(format_ctx_, packet_);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LOG(ERROR) << "[FfmpegDecoder] seek: Reached EOF while trying to decode after seek.";
                return MPG123_DONE;
            }
            LOG(ERROR) << "[FfmpegDecoder] seek: av_read_frame failed: " << get_av_error_string(ret);
            return MPG123_ERR;
        }
        if (packet_->stream_index != audio_stream_index_) {
            av_packet_unref(packet_);
            continue;
        }
        ret = avcodec_send_packet(codec_ctx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0) {
            LOG(ERROR) << "[FfmpegDecoder] seek: avcodec_send_packet failed: " << get_av_error_string(ret);
            return MPG123_ERR;
        }
        ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret >= 0) {
            VLOG(1) << "[FfmpegDecoder] seek: Decoded first frame after seek.";
            break;
        }
        // 若当前包未能解码出帧，则继续读取
    }
    return MPG123_OK;
}

// 获取当前解析到的样本数
int FfmpegDecoder::getCurrentSamples() {
    if (!is_initialized_ || audio_stream_index_ < 0) {
        LOG(ERROR) << "[FfmpegDecoder] getCurrentSamples: Not initialized or no audio stream.";
        return -1;
    }
    if (!frame_ || frame_->pts == AV_NOPTS_VALUE) {
        LOG(ERROR) << "[FfmpegDecoder] getCurrentSamples: No valid timestamp in frame.";
        return -1;
    }
    AVStream *audio_stream = format_ctx_->streams[audio_stream_index_];
    AVRational time_base = audio_stream->time_base;
    int64_t current_samples = av_rescale_q(frame_->pts, time_base, AVRational{1, codec_ctx_->sample_rate});
    VLOG(1) << "[FfmpegDecoder] getCurrentSamples: " << static_cast<int>(current_samples);
    return static_cast<int>(current_samples);
}

// 获取音频总样本数
int FfmpegDecoder::getTotalSamples() {
    VLOG(1) << "[FfmpegDecoder] getTotalSamples: " << total_samples_;
    return static_cast<int>(total_samples_);
}

// 重置解码器
void FfmpegDecoder::reset() {
    VLOG(1) << "[FfmpegDecoder] reset() called.";
    cleanupFFmpeg();
    needs_reinit_ = true;
}

// 获取音频格式信息
AudioFormatInfo FfmpegDecoder::getAudioFormat() {
    VLOG(1) << "[FfmpegDecoder] getAudioFormat() called.";
    if (!is_initialized_) {
        if (initialize_decoder() != MPG123_OK) {
            LOG(ERROR) << "[FfmpegDecoder] getAudioFormat: initialize_decoder failed.";
            // 根据需要可以抛出异常或返回一个空的 AudioFormatInfo
        }
    }
    return audio_format_;
}
