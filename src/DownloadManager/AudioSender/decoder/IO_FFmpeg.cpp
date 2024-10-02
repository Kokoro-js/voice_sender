#include <cstdint>

#include "AudioDataBuffer.h"
#include "CustomIO.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// FFmpeg 自定义读函数
int CustomIO::custom_read(void *opaque, uint8_t *buf, int buf_size) {
    AudioDataBuffer *adb = static_cast<AudioDataBuffer *>(opaque);
    size_t available = adb->size() - adb->current_pos;
    int to_read = FFMIN(buf_size, static_cast<int>(available));

    if (to_read <= 0)
        return AVERROR_EOF;

    memcpy(buf, adb->data() + adb->current_pos, to_read);
    adb->current_pos += to_read;

    return to_read;
}

// FFmpeg 自定义寻址函数
int64_t CustomIO::custom_seek(void *opaque, int64_t offset, int whence) {
    AudioDataBuffer *adb = static_cast<AudioDataBuffer *>(opaque);
    int64_t new_pos = -1;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
        break;
        case SEEK_CUR:
            new_pos = adb->current_pos + offset;
        break;
        case SEEK_END:
            new_pos = adb->size() + offset;
        break;
        case AVSEEK_SIZE:
            return adb->size();
        default:
            return -1;
    }

    if (new_pos < 0 || new_pos > static_cast<int64_t>(adb->size()))
        return -1;

    adb->current_pos = static_cast<size_t>(new_pos);
    return new_pos;
}