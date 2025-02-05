// Mpg123Decoder.cpp
#include "AudioDecoder_Mpg123.h"
#include "CustomIO.hpp" // 假设你有一个 CustomIO 类处理自定义 IO
#include <cstring>
#include <variant>

Mpg123Decoder::Mpg123Decoder()
        : is_initialized_(false), mpg123_handle_(nullptr) {
    int err = MPG123_OK;
    mpg123_handle_ = mpg123_new(nullptr, &err);
    if (!mpg123_handle_) {
        LOG(ERROR) << "Failed to create mpg123 handle: " << mpg123_plain_strerror(err);
        return;
    }

    // 设置参数
    int MPGFlag = MPG123_SEEKBUFFER; // 启用内部缓冲区加速 seek。
    // MPGFlag |= MPG123_NO_PEEK_END;
    // MPGFlag |= MPG123_NO_READAHEAD;
    // MPGFlag |= MPG123_FUZZY;
    mpg123_param2(mpg123_handle_, MPGFlag, 1, 0.0);

    mpg123_format_none(mpg123_handle_);
    mpg123_format_all(mpg123_handle_);
}

Mpg123Decoder::~Mpg123Decoder() {
    if (mpg123_handle_) {
        mpg123_close(mpg123_handle_);
        mpg123_delete(mpg123_handle_);
    }
}

int Mpg123Decoder::setup() {
    if (!data_warpper_) {
        LOG(ERROR) << "Buffer not set";
        return -1;
    }

    if (iobuf_wrapper) {
        iobuf_wrapper = nullptr;
    }

    if (is_initialized_) {
        // 重置 mpg123 的内部状态
        mpg123_close(mpg123_handle_);
        is_initialized_ = false;
    }

    if (auto buffer_warp = std::get_if<BufferWarp>(data_warpper_)) {
        mpg123_replace_reader_handle(mpg123_handle_, CustomIO::custom_mpg123_read, CustomIO::custom_mpg123_lseek,
                                     nullptr);
        int ret = mpg123_open_handle(mpg123_handle_, buffer_warp);
        if (ret != MPG123_OK) {
            LOG(ERROR) << "mpg123_open_handle failed: " << mpg123_strerror(mpg123_handle_);
            return -1;
        }
    } else if (auto *iobuf_warp = std::get_if<IOBufWarp>(data_warpper_)) {
        mpg123_replace_reader_handle(mpg123_handle_, CustomIO::iobuf_mpg123_read, CustomIO::iobuf_mpg123_lseek,
                                     nullptr);
        // int ret = mpg123_open_handle(mpg123_handle_, buffer_warp);
        int ret = mpg123_open_feed(mpg123_handle_);
        iobuf_wrapper = iobuf_warp;
        if (ret != MPG123_OK) {
            LOG(ERROR) << "mpg123_open_handle failed: " << mpg123_strerror(mpg123_handle_);
            return -1;
        }
    }

    is_initialized_ = true;

    return 0;
}

int Mpg123Decoder::read(void *output_buffer, int buffer_size, size_t *data_size) {
    if (iobuf_wrapper != nullptr) {
        auto iobuf = iobuf_wrapper->io_buf_queue->pop_front();
        if (iobuf != nullptr) {
            mpg123_feed(mpg123_handle_, iobuf->data(), iobuf->length());
        }
    }
    int result = mpg123_read(mpg123_handle_, static_cast<unsigned char *>(output_buffer), buffer_size, data_size);

    if (result == MPG123_ERR) {
        LOG(ERROR) << "MP3 decoding error: " << mpg123_strerror(mpg123_handle_);
        return -1;
    }

    if (result == MPG123_DONE) {
        // 处理解码结束（根据需要）
    }

    return result;
}

int Mpg123Decoder::seek(double target_seconds) {
    off_t frame_offset = mpg123_timeframe(mpg123_handle_, target_seconds);
    if (frame_offset < 0) {
        LOG(ERROR) << "mpg123_timeframe error: " << mpg123_strerror(mpg123_handle_);
        return -1;
    }
    off_t ret = mpg123_seek_frame(mpg123_handle_, frame_offset, SEEK_SET);
    if (ret < 0) {
        LOG(ERROR) << "mpg123_seek_frame error: " << mpg123_strerror(mpg123_handle_);
        return -1;
    }
    return 0;
}

int Mpg123Decoder::getCurrentSamples() {
    return static_cast<int>(mpg123_tell(mpg123_handle_));
}

int Mpg123Decoder::getTotalSamples() {
    return static_cast<int>(mpg123_length(mpg123_handle_));
}

void Mpg123Decoder::reset() {
    /*if (data_buffer_) {
        data_buffer_->current_pos = 0;
    }*/
    if (mpg123_handle_) {
        mpg123_close(mpg123_handle_);
    }
    is_initialized_ = false;
}

int get_bit_depth_from_encoding(int encoding) {
    if (encoding & MPG123_ENC_SIGNED_16 || encoding & MPG123_ENC_UNSIGNED_16) {
        return 16;
    } else if (encoding & MPG123_ENC_SIGNED_24 || encoding & MPG123_ENC_UNSIGNED_24) {
        return 24;
    } else if (encoding & MPG123_ENC_SIGNED_32 || encoding & MPG123_ENC_UNSIGNED_32 || encoding & MPG123_ENC_FLOAT_32) {
        return 32;
    } else if (encoding & MPG123_ENC_SIGNED_8 || encoding & MPG123_ENC_UNSIGNED_8) {
        return 8;
    }
    return 0;  // 无法识别编码
}

AudioFormatInfo Mpg123Decoder::getAudioFormat() {
    if (iobuf_wrapper != nullptr) {
        auto iobuf = iobuf_wrapper->io_buf_queue->pop_front();
        if (iobuf != nullptr) {
            mpg123_feed(mpg123_handle_, iobuf->data(), iobuf->length());
        }
    }
    long rate = 0;
    int encoding = 0;
    int channels = 0;
    if (mpg123_getformat(mpg123_handle_, &rate, &channels, &encoding) != MPG123_OK) {
        LOG(ERROR) << "mpg123_getformat failed: " << mpg123_strerror(mpg123_handle_);
    }
    audio_format_.sample_rate = static_cast<int>(rate);
    audio_format_.channels = channels;
    audio_format_.encoding = encoding;

    audio_format_.bytes_per_sample = mpg123_encsize(encoding);
    audio_format_.bits_per_samples = get_bit_depth_from_encoding(encoding);

    return audio_format_;
}
