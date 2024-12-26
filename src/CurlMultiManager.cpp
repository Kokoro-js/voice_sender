#include "CurlMultiManager.h"
#include <iostream>
#include <chrono>
#include <stdexcept>

CurlMultiManager &CurlMultiManager::getInstance() {
    static CurlMultiManager instance;
    return instance;
}

CurlMultiManager::CurlMultiManager()
        : multi_handle_(nullptr), running_(true), still_running_(0) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    multi_handle_ = curl_multi_init();
    if (!multi_handle_) {
        throw std::runtime_error("Failed to initialize CURLM");
    }
    worker_thread_ = std::thread(&CurlMultiManager::run, this);
}

CurlMultiManager::~CurlMultiManager() {
    stop();
    if (multi_handle_) {
        curl_multi_cleanup(multi_handle_);
    }
    curl_global_cleanup();
}

// IMPORTANT: 不要在回调里进行任何协程事件设置，事件设置会导致协程跑到 Curl 的线程上。
void CurlMultiManager::addTask(CURL *easy_handle, CompletionCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        CURLMcode rc = curl_multi_add_handle(multi_handle_, easy_handle);
        if (rc != CURLM_OK) {
            std::cerr << "curl_multi_add_handle failed: " << curl_multi_strerror(rc) << std::endl;
            if (callback) {
                callback(CURLE_FAILED_INIT, "Failed to add handle to multi handle");
            }
            return;
        }
        if (callback) {
            callbacks_[easy_handle] = std::move(callback);
        }
    }
    cv_.notify_one();
}

void CurlMultiManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void CurlMultiManager::run() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !running_ || !callbacks_.empty(); });

        if (!running_ && callbacks_.empty()) {
            break;
        }

        lock.unlock();

        processMultiPerform();
    }
}

void CurlMultiManager::processMultiPerform() {
    int numfds;
    CURLMcode mc = curl_multi_perform(multi_handle_, &still_running_);
    if (mc != CURLM_OK) {
        std::cerr << "curl_multi_perform() failed: " << curl_multi_strerror(mc) << std::endl;
        return;
    }

    // 等待网络事件
    mc = curl_multi_wait(multi_handle_, nullptr, 0, 1000, &numfds);
    if (mc != CURLM_OK) {
        std::cerr << "curl_multi_wait() failed: " << curl_multi_strerror(mc) << std::endl;
        return;
    }

    // 检查已完成的任务
    checkCompletedTasks();
}

void CurlMultiManager::checkCompletedTasks() {
    CURLMsg *msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(multi_handle_, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy_handle = msg->easy_handle;

            // 移除句柄
            curl_multi_remove_handle(multi_handle_, easy_handle);

            // 获取并移除回调
            CompletionCallback callback;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = callbacks_.find(easy_handle);
                if (it != callbacks_.end()) {
                    callback = std::move(it->second);
                    callbacks_.erase(it);
                }
            }

            // 执行回调（在锁之外）
            if (callback) {
                std::string error_str = curl_easy_strerror(msg->data.result);
                callback(msg->data.result, error_str);
            }

            // 在回调中清理句柄
            // 这要求回调捕获并管理 `CURL` 句柄的生命周期
        }
    }
}
