#include <mpg123.h>
#include <plog/Log.h>
#include "CustomIO.hpp"

#include "AudioDataBuffer.h"

// 自定义读取函数
mpg123_ssize_t CustomIO::custom_mpg123_read(void *handle, void *buffer, size_t size) {
    auto *src_buffer = static_cast<AudioDataBuffer *>(handle);
    if (src_buffer->current_pos + size > src_buffer->size()) {
        size = src_buffer->size() - src_buffer->current_pos; // 防止读取超出范围
    }

    memcpy(buffer, src_buffer->data() + src_buffer->current_pos, size);
    src_buffer->current_pos += size;

    return size;
}

// 自定义寻址函数
off_t CustomIO::custom_mpg123_lseek(void *handle, off_t offset, int whence) {
    auto *src_buffer = static_cast<AudioDataBuffer *>(handle);
    off_t new_pos = 0;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = src_buffer->current_pos + offset;
            break;
        case SEEK_END:
            new_pos = src_buffer->size() + offset;
            break;
        default:
            PLOG_ERROR << "Invalid 'whence': " << whence;
            return -1;
    }

    if (new_pos < 0 || static_cast<size_t>(new_pos) > src_buffer->size()) {
        PLOG_ERROR << "Seek out of range. Position: " << new_pos;
        return -1;
    }

    src_buffer->current_pos = static_cast<size_t>(new_pos);
    PLOG_DEBUG << "Seeking to position: " << new_pos;
    return new_pos;
}

// 自定义清理函数
void CustomIO::custom_mpg123_cleanup(void *handle) {
    // 清理操作，如有需要
}
