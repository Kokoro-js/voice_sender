#include "DownloadManager.h"

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
    audio_sender_->doSkip();
    audio_sender_->EventReadFinshed.set();
    audio_sender_->EventFeedDecoder.set();
}

AudioSender *DownloadManager::get_audio_sender() const {
    return audio_sender_.get();
}
