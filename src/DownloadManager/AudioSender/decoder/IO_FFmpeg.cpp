#include <cstdint>
#include "CustomIO.hpp"
#include "../../utils/AudioTypes.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// FFmpeg 自定义读函数
int CustomIO::custom_read(void *opaque, uint8_t *buf, int buf_size) {
    auto *adb = static_cast<BufferWarp *>(opaque);
    size_t available = adb->size() - adb->pos_;
    int to_read = FFMIN(buf_size, static_cast<int>(available));

    if (to_read <= 0)
        return AVERROR_EOF;

    memcpy(buf, adb->buffer->data() + adb->pos_, to_read);
    adb->pos_ += to_read;

    return to_read;
}

// FFmpeg 自定义寻址函数
int64_t CustomIO::custom_seek(void *opaque, int64_t offset, int whence) {
    auto *adb = static_cast<BufferWarp *>(opaque);
    int64_t new_pos = 0;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = static_cast<int64_t>(adb->pos_) + offset;
            break;
        case SEEK_END:
            new_pos = static_cast<int64_t>(adb->size()) + offset;
            break;
        case AVSEEK_SIZE:
            return static_cast<int64_t>(adb->size());
        default:
            return AVERROR(EINVAL);
    }

    if (new_pos < 0 || new_pos > static_cast<int64_t>(adb->size())) {
        return AVERROR(EINVAL);
    }

    adb->pos_ = static_cast<size_t>(new_pos);
    return new_pos;
}
