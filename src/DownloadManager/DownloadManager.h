#pragma once

#include <vector>
#include "coro/coro.hpp"
#include "readerwriterqueue.h"
#include "AudioSender/AudioSender.h"
#include "TaskManager.h"

constexpr int MAX_CHUNK_SIZE = 1024 * 16; // 16 kb

class DownloadManager : public TaskManager {
public:
    explicit DownloadManager(std::shared_ptr<coro::thread_pool> tp, std::shared_ptr<AudioSender> audio_sender_ptr);
    static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata);

    coro::task<void> initAndWaitJobs();
    coro::task<void> startQueueJob();
    void cleanupJob();

    coro::event DownloadFinishedEvent;

    std::string id_;  // 用于标识自己在 map 中的 key
    using RemoveCallback = std::function<void(const std::string&)>;
    RemoveCallback removeCallback_;
    void setRemoveCallback(RemoveCallback callback, const std::string& id);

    AudioSender* get_audio_sender() const;
    std::unique_ptr<TaskManager> task_manager;
    // 其他成员函数...

private:
    static constexpr int FIXED_CHUNK_SIZE = 8096 * 2;  // 固定块大小
    static constexpr int QUEUE_SIZE = 5 * 1024 * 1024 / MAX_CHUNK_SIZE; // 5MB

    moodycamel::ReaderWriterQueue<std::vector<char>> download_chunk_queues_{};  // 下载块队列

    std::shared_ptr<AudioSender> audio_sender_; // DownloadManager 拥有 Sender 的唯一控制权

    std::shared_ptr<coro::thread_pool> tp_;
    coro::task_container<coro::thread_pool> task_container_;

    coro::event dataEnqueue;
    bool isStopped;
};
