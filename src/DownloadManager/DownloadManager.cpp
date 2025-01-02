#include "DownloadManager.h"
#include "../CurlMultiManager.h"
#include "coro/coro.hpp"

#include <glog/logging.h>
#include <memory>
#include <optional>
#include <utility>
#include "ylt/struct_json/json_reader.h"

// 定义结构体用于解析缓存响应
struct ResCached {
    std::string url;
};

// 自定义删除器，确保 CURL 句柄被正确清理
struct CurlHandleDeleter {
    void operator()(CURL *curl) const {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }
};

DownloadManager::DownloadManager(std::shared_ptr<coro::thread_pool> tp, std::shared_ptr<AudioSender> audio_sender_ptr)
        : TaskManager(ConsumerMode::RoundRobin),
          TaskHandler(std::move(tp)),
          audio_sender_(std::move(audio_sender_ptr)) {
    stream_id_ = audio_sender_->stream_id_;
    std::string auth = "InstanceId: " + stream_id_;
    headers = curl_slist_append(headers, auth.c_str());
}

coro::task<void> DownloadManager::startQueueJob() {
    int err_count = 0;
    while (!isStopped) {
        if (err_count > 3) {
            cleanupJob();
            break;
        }
        co_await tp_->yield();

        auto task = getNextTask();
        if (!task.has_value()) {
            LOG(WARNING) << "任务队列为空，等待新任务...";
            co_await TaskUpdateEvent;
            TaskUpdateEvent.reset();
            continue;
        }

        auto task_item = std::move(task.value());
        // extendedTask 是用一次创建一次，作为 share_ptr 供别处获取，不需要重置它里边的任何状态，没有人用它的时候自然消失
        extendedTask = std::make_shared<ExtendedTaskItem>(std::move(task_item));
        ExtendedTaskItem *current_task = extendedTask.get();

        // 初始化 CURL 句柄
        auto curl_handle = std::shared_ptr<CURL>(curl_easy_init(), CurlHandleDeleter());
        if (!curl_handle) {
            LOG(ERROR) << "无法初始化 CURL 句柄，任务: " << task_item.name;
            audio_sender_->doSkip();
            co_await current_task->EventReadFinished;
            continue;
        }

        std::optional<std::string> final_url;

        if (task_item.type == TaskType::Cached) {
            final_url = co_await getRealUrl(extendedTask->item.url, curl_handle);
            if (!final_url.has_value()) {
                LOG(ERROR) << "获取真实 URL 失败，任务: " << extendedTask->item.name;
                audio_sender_->doSkip();
                /* 如果真实链接都没获取就不会触发 EventNewDownload，所以根本不可能等到
                 * co_await current_task->EventReadFinished;
                 * */
                err_count++;
                continue;
            }
            curl_easy_setopt(curl_handle.get(), CURLOPT_URL, final_url->c_str());
        } else {
            final_url = extendedTask->item.url;
            curl_easy_setopt(curl_handle.get(), CURLOPT_URL, final_url->c_str());
        }

        // 设置写回调函数
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEDATA, current_task);

        // 设置低速限速和超时
        if (!task_item.use_stream) {
            curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_TIME, 30L); // 30秒
            curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L * 320); // 320KB/s
        }

        // 执行下载
        audio_sender_->EventNewDownload.set();
        bool success = co_await executeDownload(current_task, curl_handle);
        if (!success) {
            LOG(ERROR) << "下载任务跳过: " << task_item.name;
            err_count++;
            continue;
        }

        err_count = 0;
        VLOG(1) << "任务完成，准备下一个任务。";
    }
}

// 辅助函数：获取真实 URL（用于 Cached 类型任务）
coro::task<std::optional<std::string>>
DownloadManager::getRealUrl(const std::string &cached_url, std::shared_ptr<CURL> curl_handle) const {
    CurlMultiManager &manager = CurlMultiManager::getInstance();

    // 该事件用于桥接协程和 CurlManager 完成后的回调
    coro::event EventCurlFinished;

    std::string responseString;
    struct ResCached res;

    curl_easy_setopt(curl_handle.get(), CURLOPT_URL, cached_url.c_str());
    curl_easy_setopt(curl_handle.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEDATA, &responseString);
    curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_to_string_callback);
    manager.addTask(curl_handle.get(),
                    [&EventCurlFinished, &responseString, &res, curl_handle = curl_handle.get()](CURLcode result,
                                                                                                 const std::string & /*message*/) {


                        if (result != CURLE_OK) {
                            LOG(ERROR) << "CURL 请求失败，错误码: " << result;
                            res.url = "";
                            EventCurlFinished.set();
                            return;
                        }

                        long http_code = 0;
                        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

                        if (http_code != 200) {
                            LOG(ERROR) << "获取真实 URL 时服务器返回错误" << http_code << ": " << responseString;
                            res.url = "";
                        } else {
                            try {
                                struct_json::from_json(res, responseString);
                            } catch (const std::exception &e) {
                                LOG(ERROR) << "JSON 解析失败: " << e.what();
                                res.url = "";
                            }
                        }
                        EventCurlFinished.set();
                    });

    co_await EventCurlFinished;
    // 让他从回调线程返回线程池
    co_await tp_->schedule();

    if (res.url.empty()) {
        LOG(ERROR) << "未能获取到真实的 URL，检查 API 日志获取详细信息";
        co_return std::nullopt;
    }
    co_return res.url;
}

// 辅助函数：执行下载任务
coro::task<bool> DownloadManager::executeDownload(ExtendedTaskItem *current_task, std::shared_ptr<CURL> curl_handle) {
    CurlMultiManager &manager = CurlMultiManager::getInstance();

    curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEDATA, current_task);
    curl_easy_setopt(curl_handle.get(), CURLOPT_BUFFERSIZE, FIXED_CHUNK_SIZE);

    // 设置低速限速和超时
    if (!current_task->item.use_stream) {
        curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_TIME, 30L); // 30秒
        curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L * 320); // 320KB/s
    }

    current_task->state = AudioCurrentState::Downloading;

    manager.addTask(curl_handle.get(),
                    [current_task, curl_handle = curl_handle.get()](CURLcode result,
                                                                    const std::string &message) {
                        if (result != CURLE_OK) {
                            LOG(ERROR) << "下载失败: " << current_task->item.name << "，错误码: " << result << "，消息: "
                                       << message;
                            current_task->should_skip = true;
                            current_task->EventDownloadFinished.set();
                            return;
                        }


                        long http_code = 0;
                        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
                        if (http_code != 200) {
                            LOG(ERROR) << "服务端返回错误码，任务 " << current_task->item.name << "，错误码: "
                                       << http_code
                                       << "，消息: "
                                       << message;
                        } else {
                            VLOG(1) << "下载成功: " << current_task->item.name;

                            double content_length = 0.0;
                            curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
                            current_task->total_size = static_cast<size_t>(content_length);
                            current_task->state = AudioCurrentState::DownloadAndWriteFinished;
                        }

                        current_task->EventDownloadFinished.set();
                    });

    // 事件设置会导致逻辑跑到别的回调线程进行，但此处逻辑并不重，可以允许跑到 EventReadFinished 那，避免线程切换开销。
    co_await current_task->EventDownloadFinished;

    if (current_task->should_skip) {
        // 该函数会确保 Control 完成周期。
        audio_sender_->doSkip();
    }

    // 等待 Control 周期走完，设置 EventReadFinished。
    co_await current_task->EventReadFinished;

    if (current_task->should_skip) {
        co_return false;
    }

    VLOG(1) << "下载任务完成: " << current_task->item.name;
    co_return true;
}

// 写入字符串的回调函数
size_t DownloadManager::write_to_string_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *response = static_cast<std::string *>(userdata);
    size_t total_size = size * nmemb;
    response->append(static_cast<char *>(ptr), total_size);
    LOG(INFO) << "write_to_string_callback 被调用，大小: " << total_size;
    return total_size;
}

// 写入数据的回调函数
size_t DownloadManager::write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *current_task = static_cast<ExtendedTaskItem *>(userdata);
    size_t total_size = size * nmemb;

    auto &data = current_task->data;
    if (auto fixed_buffer = std::get_if<FixedCapacityBuffer>(&data)) {
        fixed_buffer->insert(static_cast<const unsigned char *>(ptr), total_size);
    } else if (auto iobuf = std::get_if<folly::IOBufQueue>(&data)) {
        auto data_chunk = folly::IOBuf::copyBuffer(static_cast<const char *>(ptr), total_size);
        iobuf->append(std::move(data_chunk));
        LOG(ERROR) << "使用了 IOBuf";
    }

    // 累加下载的数据
    current_task->total_size += total_size;
    return total_size;
}
