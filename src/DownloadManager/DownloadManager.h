// DownloadManager.h
#pragma once

#include <vector>
#include <memory>
#include <string>
#include <curl/curl.h>
#include "coro/coro.hpp"
#include "AudioSender/AudioSender.h"
#include "TaskManager.h"
#include "TaskHandler.h"
#include "utils/ExtendedTaskItem.h" // 包含 ExtendedTaskItem
#include "folly/io/IOBuf.h"

class AudioSender; // 前向声明

constexpr int MAX_CHUNK_SIZE = 1024 * 128; // 128 kb

// 定义 DownloadManager 类
class DownloadManager : public TaskManager, public TaskHandler {
public:
    explicit DownloadManager(std::shared_ptr<coro::thread_pool> tp, std::shared_ptr<AudioSender> audio_sender_ptr);

    struct curl_slist *headers = nullptr;

    coro::task<void> initAndWaitJobs() override;

    void cleanupJob() override;

    coro::task<void> startQueueJob();

    AudioSender *get_audio_sender() const;

    bool skipDownload();

private:
    static constexpr int FIXED_CHUNK_SIZE = 8096 * 2; // 固定块大小
    static constexpr int QUEUE_SIZE = 5 * 1024 * 1024 / MAX_CHUNK_SIZE; // 5MB

    std::shared_ptr<ExtendedTaskItem> extendedTask;

    std::shared_ptr<AudioSender> audio_sender_; // DownloadManager 拥有 Sender 的唯一控制权
    std::shared_ptr<void> curl_handle;

    static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata);

    static size_t write_to_string_callback(void *ptr, size_t size, size_t nmemb, void *userdata);

    coro::task<std::optional<std::string>>
    getRealUrl(const std::string &cached_url, std::shared_ptr<CURL> curl_handle) const;

    coro::task<bool> executeDownload(ExtendedTaskItem *current_task, std::shared_ptr<CURL> curl_handle);
};
