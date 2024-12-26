// ExtendedTaskItem.h
#pragma once

#include <memory>
#include "../TaskManager.h"
#include "AudioTypes.h"

// 定义 ExtendedTaskItem 结构体
struct ExtendedTaskItem {
    explicit ExtendedTaskItem(TaskItem item) : item(std::move(item)) {
    }

    TaskItem item;
    AudioCurrentState state = AudioCurrentState::Downloading;
    bool should_skip = false;

    std::variant<FixedCapacityBuffer, folly::IOBufQueue> data = FixedCapacityBuffer(16 * 1024 * 1024);;
    // folly::IOBufQueue iobuf_buf_queue_ = folly::IOBufQueue(folly::IOBufQueue::cacheChainLength());

    size_t total_size = 0;
    
    // 该事件触发时，代表下载/写入已经完成
    coro::event EventDownloadFinished;
    // 该事件触发时，代表读取已完成
    coro::event EventReadFinished;
};
