// ExtendedTaskItem.h
#pragma once

#include <memory>
#include <variant>
#include <optional>
#include <utility>
#include <curl/curl.h>
#include "../TaskManager.h"
#include "AudioTypes.h"
#include "../../ConfigManager.h"

enum class ReaderErrorCode {
    InvalidFormat = 1001,
    CannotFindInfo = 1002,
    DecoderError = 1002
};
struct ReaderErrorInfo {
    ReaderErrorCode code;
    std::string message;
};

struct ExtendedTaskItem {
    explicit ExtendedTaskItem(TaskItem item, std::shared_ptr<CURL> curl_handler) : item(std::move(item)), curl_handler(
            std::move(curl_handler)) {}

    TaskItem item;
    std::shared_ptr<CURL> curl_handler;
    AudioCurrentState state = AudioCurrentState::Downloading;
    bool should_skip = false;

    std::variant<FixedCapacityBuffer, folly::IOBufQueue> data = FixedCapacityBuffer(
            ConfigManager::getInstance().getConfig().default_buffer_size);
    folly::IOBufQueue iobuf_write_queue{folly::IOBufQueue::cacheChainLength()};
    coro::mutex mutex_data;

    size_t total_size = 0;

    coro::event EventDownloadFinished;
    coro::event EventReadFinished;

    std::optional<ReaderErrorInfo> read_error;

    void set_read_error(ReaderErrorCode code, const std::string &message) {
        read_error = ReaderErrorInfo{code, message};
        EventReadFinished.set();
    }


    // 设置 FixedCapacityBuffer
    void setData(FixedCapacityBuffer buffer) {
        data.emplace<FixedCapacityBuffer>(std::move(buffer));
    }

    // 设置 folly::IOBufQueue
    void setData(folly::IOBufQueue queue) {
        data.emplace<folly::IOBufQueue>(std::move(queue));
    }

    // 获取 FixedCapacityBuffer
    std::optional<std::reference_wrapper<FixedCapacityBuffer>> getFixedCapacityBuffer() {
        if (std::holds_alternative<FixedCapacityBuffer>(data)) {
            return std::ref(std::get<FixedCapacityBuffer>(data));
        }
        return std::nullopt;
    }

    // 获取 folly::IOBufQueue
    std::optional<std::reference_wrapper<folly::IOBufQueue>> getIOBufQueue() {
        if (std::holds_alternative<folly::IOBufQueue>(data)) {
            return std::ref(std::get<folly::IOBufQueue>(data));
        }
        return std::nullopt;
    }

    // 检查当前存储的数据类型
    bool isFixedCapacityBuffer() const {
        return std::holds_alternative<FixedCapacityBuffer>(data);
    }

    bool isIOBufQueue() const {
        return std::holds_alternative<folly::IOBufQueue>(data);
    }
};
