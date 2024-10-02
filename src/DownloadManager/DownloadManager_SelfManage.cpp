#include <plog/Log.h>

#include "DownloadManager.h"

coro::task<void> DownloadManager::initAndWaitJobs() {
    // 调度并等待 startQueueJob 完成
    task_container_.start(startQueueJob());
    task_container_.start(
        audio_sender_->start_producer(download_chunk_queues_, dataEnqueue, isStopped));
    task_container_.start(audio_sender_->start_consumer(isStopped));
    auto sender = audio_sender_->start_sender(isStopped);
    task_container_.start(std::move(sender));

    // 等待任务容器里响应 isStopped 自己终结
    co_await task_container_.garbage_collect_and_yield_until_empty();
    LOGI << "开始清理任务";
    // 调用删除回调
    if (removeCallback_) {
        removeCallback_(id_);
    }
    delete this;
    co_return;
}

void DownloadManager::cleanupJob() {
    isStopped = true;
    dataEnqueue.set(); // 触发数据变动提醒其他携程进行来响应 isStopped;
}

void DownloadManager::setRemoveCallback(RemoveCallback callback, const std::string& id) {
    removeCallback_ = callback;
    id_ = id;
}

AudioSender *DownloadManager::get_audio_sender() const {
    return audio_sender_.get();
}
