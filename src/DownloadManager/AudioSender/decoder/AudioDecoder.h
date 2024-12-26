// AudioDecoder.h
#pragma once

#include <variant>

#include "../../utils/AudioTypes.h" // 包含 AudioFormatInfo 和 IOBufWarp

// 定义 AudioDecoder 类
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    virtual int setup() = 0;
    virtual int read(void* output_buffer, int buffer_size, size_t* data_size) = 0;
    virtual int seek(double target_seconds) = 0;

    virtual int getCurrentSamples() = 0;
    virtual int getTotalSamples() = 0;

    virtual void setBuffer(DataVariant* buffer) {
        data_warpper_ = buffer;
    }

    virtual void reset() = 0;

    virtual AudioFormatInfo getAudioFormat() = 0;

protected:
    std::variant<BufferWarp, IOBufWarp> *data_warpper_{};
};
