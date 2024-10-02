#include "DownloadManager.h"
#include "../CurlMultiManager.h"
#include "coro/coro.hpp"
#include "plog/Log.h"


DownloadManager::DownloadManager(std::shared_ptr<coro::thread_pool> tp, std::shared_ptr<AudioSender> audio_sender_ptr)
    : download_chunk_queues_(QUEUE_SIZE), isStopped(false),
      audio_sender_(std::move(audio_sender_ptr)),
      tp_(std::move(tp)), task_container_(tp_), TaskManager(RoundRobin) {}

coro::task<void> DownloadManager::startQueueJob() {
    auto sender = get_audio_sender();
    CurlMultiManager &manager = CurlMultiManager::getInstance();
    std::optional<TaskItem> task;

    while (!isStopped) {
        co_await tp_->yield();
        if (task.has_value() == false) {
            LOGE << "为空";
            co_await TaskUpdateEvent;
            task = getNextTask();
            continue;
        }

        auto task_item = task.value();

        CURL *curl_handle = curl_easy_init();
        if (!curl_handle) {
            LOGE << "Failed to initialize CURL handle for task: " << task_item.name;
            continue;
        }


        curl_easy_setopt(curl_handle, CURLOPT_URL, task_item.url.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_handle, CURLOPT_BUFFERSIZE, FIXED_CHUNK_SIZE);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, this); // 传递所属对象指针
        // 用于防止在文件这边推无限的流
        if (task_item.type == File) {
            curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, 30L); // 设置低速超时时间为 30 秒
            curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 1024L * 320); // 设置低速限制为 32 KB/秒
        }

        sender->audio_props.setTaskItem(task_item);
        audio_sender_->audio_props.state = AudioCurrentState::Downloading;
        LOGE << task_item.name;
        manager.addTask(curl_handle,
                        [task_item, this, curl_handle, sender](
                    CURLcode result, const std::string &message) {
                            if (result != CURLE_OK) {
                                LOGE << "CURL failed for task " << task_item.name << " with code " << result << ": " << message;
                            } else {
                                LOGD << "CURL completed successfully for task " << task_item.name << ": " << message;
                                size_t size;
                                curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &size);
                                // audio_sender 里的参数永远是正在处理的，下载有可能提前，不要在下载完成时更新
                                sender->audio_props.total_bytes = size;
                                sender->audio_props.state = AudioCurrentState::DownloadFinished;
                                /*mpg123_set_filesize(audio_sender_->mpg123_handle_, size);*/
                            }
                            curl_easy_cleanup(curl_handle);
                            DownloadFinishedEvent.set();
                            //dataEnqueue.set();
                        });

        co_await audio_sender_->startDownloadNextEvent;
        audio_sender_->startDownloadNextEvent.reset();
        /*audio_sender_->startDownloadNextEvent.reset();
        audio_sender_->audio_props.state = AudioCurrentState::Downloading;*/

        LOGD << "Task Finished, jump to next.";
        task = getNextTask();

        if (isStopped) {
            co_return;
        }
    }
    co_return;
}

size_t DownloadManager::write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *manager = static_cast<DownloadManager *>(userdata);
    size_t total_size = size * nmemb;

    // 直接构造vector并尝试入队
    std::vector<char> data_chunk(static_cast<char *>(ptr), static_cast<char *>(ptr) + total_size);
    if (!manager->download_chunk_queues_.enqueue(std::move(data_chunk))) {
        LOGW << "Failed to enqueue data.";
    }

    manager->dataEnqueue.set();
    return total_size;
}
