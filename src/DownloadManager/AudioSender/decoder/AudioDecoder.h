#include <mpg123.h>
#include "AudioDataBuffer.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#ifndef AUDIODECODER_H

// AudioDecoder.h
#pragma once

struct AudioFormatInfo {
    int sample_rate = 0;
    int channels = 0;
    int encoding = -1;
    int bytes_per_sample = 0;
    int bits_per_samples = 0;
};

class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    virtual int setup() = 0;
    virtual int read(void* output_buffer, int buffer_size, size_t* data_size) = 0;
    virtual int seek(double target_seconds) = 0;

    virtual int getTotalSamples() = 0;

    virtual void setBuffer(AudioDataBuffer* buffer) {
        data_buffer_ = buffer;
    }

    virtual void reset() = 0;

    virtual AudioFormatInfo getAudioFormat() = 0;

protected:
    AudioDataBuffer* data_buffer_;
};

/*class FFmpegDecoder : public AudioDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    int setup() override;
    int read(void* output_buffer, int buffer_size, int* data_size) override;
    int seek(double target_seconds) override;
    void setBuffer(AudioDataBuffer* buffer) override;
    void reset() override;

private:
    AVFormatContext* fmt_ctx_ = nullptr;
    AVIOContext* avio_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    int audio_stream_index_ = -1;
    AudioDataBuffer* data_buffer_;
    bool is_initialized_;
};*/

#define AUDIODECODER_H

#endif //AUDIODECODER_H
