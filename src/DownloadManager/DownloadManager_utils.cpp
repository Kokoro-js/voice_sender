#include "DownloadManager.h"
#include "../CurlMultiManager.h"

bool DownloadManager::skipDownload() {
    if (!curl_handle) {
        return false;
    }

    CurlMultiManager &manager = CurlMultiManager::getInstance();
    manager.cancelTask(curl_handle.get());
    return true;
}

coro::task<void> DownloadManager::initAndWaitJobs() {
    // 调度并等待 startQueueJob 完成
    task_container_.start(startQueueJob());
    const auto ptr = &extendedTask;
    task_container_.start(
            audio_sender_->start_producer(ptr, isStopped));
    task_container_.start(audio_sender_->start_consumer(isStopped));
    auto sender = audio_sender_->start_sender(isStopped);
    task_container_.start(std::move(sender));

    // 等待任务容器里响应 isStopped 自己终结
    co_await task_container_.garbage_collect_and_yield_until_empty();
    LOG(INFO) << "开始清理任务";
    // 调用删除回调
    if (removeCallback_) {
        removeCallback_(id_);
    }
    delete this;
    co_return;
}

void DownloadManager::cleanupJob() {
    isStopped = true;
    this->TaskUpdateEvent.set();
    // doSkip 确保 producer 畅通无阻。
    skipDownload();
    audio_sender_->clean_up();
    // audio_sender 的生产与消费又那边自己处理，只要 isStooped 被设置然后 control 被唤醒就行。
}

AudioSender *DownloadManager::get_audio_sender() const {
    return audio_sender_.get();
}
