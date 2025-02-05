#include <cstdint>
#include "CustomIO.hpp"
#include "../../utils/AudioTypes.h"
#include <glog/logging.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

int CustomIO::iobuf_ffmpeg_read(void *opaque, uint8_t *buf, int buf_size) {
    auto *adb = static_cast<IOBufWarp *>(opaque);
    if (!adb || !adb->io_buf_queue) {
        return AVERROR(EINVAL); // 无效参数
    }

    auto *iobuf_queue = adb->io_buf_queue;

    // 获取当前队列中可用的数据长度
    size_t available = iobuf_queue->chainLength();
    int to_read = FFMIN(buf_size, static_cast<int>(available));

    if (to_read <= 0) {
        if (adb->is_eof) {
            return AVERROR_EOF; // 文件结束
        } else {
            return AVERROR(EAGAIN); // 数据不够，提示解码器稍后重试
        }
    }

    // 从队列中提取数据
    auto data = iobuf_queue->split(to_read);

    // 将数据从 IOBuf 复制到 buf
    if (data->isChained()) {
        // 多个内存块，需要遍历
        size_t offset = 0;
        for (auto &range: *data) {
            memcpy(buf + offset, range.data(), range.size());
            offset += range.size();
        }
    } else {
        // 单个内存块，直接复制
        memcpy(buf, data->data(), data->length());
    }

    // 更新读取位置
    adb->pos_ += to_read;

    LOG(INFO) << "Read " << to_read << " bytes from IOBufQueue";
    return to_read; // 返回实际读取的字节数
}

int64_t CustomIO::iobuf_ffmpeg_seek(void *opaque, int64_t offset, int whence) {
    auto *adb = static_cast<IOBufWarp *>(opaque);
    if (!adb) {
        return AVERROR(EINVAL); // 无效参数
    }

    switch (whence) {
        case SEEK_CUR:
            if (offset == 0) {
                // 获取当前位置：即已读取的总字节数
                return static_cast<int64_t>(adb->pos_);
            }
            break;
        case SEEK_END:
            // 返回一个假的总大小
            return AV_NOPTS_VALUE;  // 或返回实际值（如果有）
        case AVSEEK_SIZE:
            // 返回总数据大小（如果未知可返回默认值）
            return AV_NOPTS_VALUE;  // 或实际值（如果有）
        default:
            break;
    }

    // 其他情况均不支持
    LOG(WARNING) << "Unsupported seek operation: whence=" << whence << ", offset=" << offset;
    return AVERROR(ESPIPE);  // ESPIPE: Illegal seek
}
