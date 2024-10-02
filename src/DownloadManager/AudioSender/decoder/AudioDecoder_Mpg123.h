// Mpg123Decoder.h
#pragma once
#include "AudioDecoder.h"
#include <mpg123.h>
#include <plog/Log.h>

class Mpg123Decoder : public AudioDecoder {
public:
    Mpg123Decoder();
    ~Mpg123Decoder() override;

    int setup() override;
    int read(void* output_buffer, int buffer_size, size_t* data_size) override;
    int seek(double target_seconds) override;

    int getTotalSamples() override;
    void reset() override;

    AudioFormatInfo getAudioFormat() override;

private:
    mpg123_handle* mpg123_handle_;
    bool is_initialized_;
    AudioFormatInfo audio_format_;
};
