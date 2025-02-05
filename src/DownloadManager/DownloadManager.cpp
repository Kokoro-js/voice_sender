#include "DownloadManager.h"
#include "../CurlMultiManager.h"
#include "coro/coro.hpp"

#include <glog/logging.h>
#include <memory>
#include <optional>
#include <utility>
#include "ylt/struct_json/json_reader.h"

// 定义结构体用于解析缓存响应
struct UrlInfo {
    std::string url;
    std::optional<std::string> user_agent;
    std::optional<std::string> referer;
    std::optional<std::string> cookie;
    std::optional<std::string> proxy;
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
    while (true) {
        if (isStopped) {
            VLOG(1) << "下载任务已退出。";
            co_return;
        }

        if (extendedTask) {
            if (extendedTask->read_error) {
                err_count++;
            }
        }
        if (err_count > 3) {
            LOG(ERROR) << "错误次数过多退出。";
            cleanupJob();
            co_return;
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

        // 单个实例同期只能有一个 curl_handle，放在类中
        curl_handle = std::shared_ptr<CURL>(curl_easy_init(), CurlHandleDeleter());
        if (!curl_handle) {
            LOG(ERROR) << "无法初始化 CURL 句柄，任务: " << task_item.name;
            audio_sender_->doSkip();
            continue;
        }

        // extendedTask 存在类中，并且是用一次创建一次，作为 share_ptr 供别处获取，不需要重置它里边的任何状态，没有人用它的时候自然消失
        extendedTask = std::make_shared<ExtendedTaskItem>(std::move(task_item), curl_handle);
        ExtendedTaskItem *current_task = extendedTask.get();

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
                autoNext(); // 跳跃下一首逻辑。
                continue;
            }
        } else {
            final_url = extendedTask->item.url;
        }
        curl_easy_setopt(curl_handle.get(), CURLOPT_URL, final_url->c_str());

        // 设置写回调函数
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEDATA, current_task);

        // 设置低速限速和超时
        if (task_item.use_stream) {
            folly::IOBufQueue queue(folly::IOBufQueue::cacheChainLength());
            extendedTask->setData(std::move(queue));
            curl_easy_setopt(curl_handle.get(), CURLOPT_MAX_RECV_SPEED_LARGE, 1024L * 320); // 假设是流，应该进行限速
        } else {
            // 如果没在用流，则应该限制低速情况
            // 10s 内下载速度低于 320
            curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_TIME, 10L); // 10秒
            curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L * 320 / 8); // 320kbps
        }

        // 执行下载
        audio_sender_->EventNewDownload.set();
        bool success = co_await executeDownload(current_task, curl_handle);
        if (!success) {
            LOG(ERROR) << "下载任务跳过: " << task_item.name;
            err_count++;
            autoNext(); // 跳跃下一首逻辑。
            continue;
        }

        err_count = 0;
        if (!hasManualSkip) {
            autoNext(); // 跳跃下一首逻辑。
        }
        hasManualSkip = false;
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
    UrlInfo res;

    auto curl = curl_handle.get();
    curl_easy_setopt(curl, CURLOPT_URL, cached_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseString);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string_callback);
    manager.addTask(curl_handle,
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

    if (res.cookie) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, res.cookie.value().c_str());
    }
    if (res.referer) {
        curl_easy_setopt(curl, CURLOPT_REFERER, res.referer.value().c_str());
    }
    if (res.user_agent) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, res.user_agent.value().c_str());
    }

    co_return res.url;
}

// 辅助函数：执行下载任务
coro::task<bool> DownloadManager::executeDownload(ExtendedTaskItem *current_task, std::shared_ptr<CURL> curl_handle) {
    CurlMultiManager &manager = CurlMultiManager::getInstance();

    curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEDATA, current_task);
    curl_easy_setopt(curl_handle.get(), CURLOPT_BUFFERSIZE, FIXED_CHUNK_SIZE);

    // 允许最多 2 次 302 跳转
    curl_easy_setopt(curl_handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle.get(), CURLOPT_MAXREDIRS, 2L);

    current_task->state = AudioCurrentState::Downloading;

    coro::event EventCurlFinished;

    manager.addTask(curl_handle,
                    [current_task, curl_handle = curl_handle.get(), &EventCurlFinished](CURLcode result,
                                                                                        const std::string &message) {
                        if (result != CURLE_OK) {
                            LOG(ERROR) << "下载失败: " << current_task->item.name << "，错误码: " << result << "，消息: "
                                       << message;
                            current_task->should_skip = true;
                            EventCurlFinished.set();
                            return;
                        }


                        long http_code = 0;
                        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
                        if (http_code != 200) {
                            LOG(ERROR) << "服务端返回错误码，任务 " << current_task->item.name << "，错误码: "
                                       << http_code
                                       << "，消息: "
                                       << message;
                            current_task->should_skip = true;
                            EventCurlFinished.set();
                            return;
                        }

                        VLOG(1) << "下载成功: " << current_task->item.name;

                        double content_length = 0.0;
                        curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
                        current_task->total_size = static_cast<size_t>(content_length);
                        current_task->state = AudioCurrentState::DownloadAndWriteFinished;


                        EventCurlFinished.set();
                    });

    // 事件设置会导致逻辑跑到别的回调线程进行，但此处逻辑并不重，可以允许跑到 EventReadFinished 那，避免线程切换开销。
    co_await EventCurlFinished;

    if (current_task->should_skip) {
        // 该函数会确保 Control 完成周期。
        audio_sender_->doSkip();
    }

    co_await tp_->schedule();
    current_task->EventDownloadFinished.set();
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
        std::unique_ptr<folly::IOBuf> data_chunk = folly::IOBuf::copyBuffer(static_cast<const char *>(ptr), total_size);
        /*{
            // 加锁追加数据
            coro::sync_wait(current_task->mutex_data.lock());
            iobuf->append(std::move(data_chunk));
        }*/
        current_task->iobuf_write_queue.append(std::move(data_chunk));

        // 可以防止频繁 append 多线程读写的冲突，并且能减少 IOBUF 的碎片化，充分利用 CPU 缓存。
        if (current_task->iobuf_write_queue.chainLength() > 1024 * 32) {
            // 移动当前队列数据到一个连续 IOBuf
            auto flattened = current_task->iobuf_write_queue.move();
            flattened->coalesce();  // 合并碎片化数据

            // 当读取上的 iobufqueue 超过某个大小时不再往里边写入，flattened 的内存块会自己释放。
            if (iobuf->chainLength() > 5 * 1024 * 1024) {
                current_task->total_size += total_size;
                curl_easy_pause(current_task->curl_handler.get(), CURLPAUSE_RECV);
                return total_size;
            }

            {
                // 加锁追加数据
                coro::sync_wait(current_task->mutex_data.lock());
                iobuf->append(std::move(flattened));

                // 实现固定长度：如果链表超过阈值，移除最早的块
                // 在下载侧删除数据是坏注意
                /*while (iobuf->chainLength() > 5 * 1024 * 1024) {
                    iobuf->pop_front();
                }*/
            }
        }
    }

    // 累加下载的数据
    current_task->total_size += total_size;
    return total_size;
}
