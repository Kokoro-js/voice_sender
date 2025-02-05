#include "CurlMultiManager.h"
#include <iostream>
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
        throw std::runtime_error("Failed to init CURLM");
    }

    // 启动工作线程
    worker_thread_ = std::thread(&CurlMultiManager::run, this);
}

CurlMultiManager::~CurlMultiManager() {
    stop();  // 确保安全退出
    if (multi_handle_) {
        curl_multi_cleanup(multi_handle_);
    }
    curl_global_cleanup();
}

void CurlMultiManager::addTask(std::shared_ptr<CURL> easy_handle, CompletionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 将 CURL easy handle 加入 multi_handle
    CURLMcode rc = curl_multi_add_handle(multi_handle_, easy_handle.get());
    if (rc != CURLM_OK) {
        std::cerr << "[CurlMultiManager] add_handle failed: "
                  << curl_multi_strerror(rc) << std::endl;
        // 如果失败，直接调用回调通知
        if (callback) {
            callback(CURLE_FAILED_INIT, "Failed to add handle");
        }
        return;
    }

    // 保存回调和 shared_ptr 引用
    if (callback) {
        callbacks_[easy_handle.get()] = std::move(callback);
        handles_[easy_handle.get()] = std::move(easy_handle);
    }

    // 唤醒工作线程
    cv_.notify_one();
}

void CurlMultiManager::cancelTask(CURL *easy_handle) {
    CompletionCallback cb;

    {
        // 先上锁，移除句柄并取出回调
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = callbacks_.find(easy_handle);
        if (it != callbacks_.end()) {
            CURLMcode rc = curl_multi_remove_handle(multi_handle_, easy_handle);
            if (rc != CURLM_OK) {
                std::cerr << "[CurlMultiManager] remove_handle failed: "
                          << curl_multi_strerror(rc) << std::endl;
            }
            cb = std::move(it->second);
            callbacks_.erase(it);
            handles_.erase(easy_handle);
        }
    }

    // 在锁外执行回调，避免长时间持锁
    if (cb) {
        cb(CURLE_OK, "Canceled by user");
    }

    cv_.notify_one();
}

void CurlMultiManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();

    // 等待工作线程退出
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    // 彻底清理句柄
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &[handle, cb]: callbacks_) {
            curl_multi_remove_handle(multi_handle_, handle);
            if (cb) {
                cb(CURLE_ABORTED_BY_CALLBACK, "Aborted by manager stop");
            }
        }
        callbacks_.clear();
        handles_.clear();
    }
}

// 工作线程函数：不断执行 multi_perform, multi_wait, 并检查任务
void CurlMultiManager::run() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !running_ || !callbacks_.empty(); });

        if (!running_ && callbacks_.empty()) {
            break;  // 退出循环，线程结束
        }

        lock.unlock();
        processMultiPerform();
    }
}

void CurlMultiManager::processMultiPerform() {
    // 执行网络操作
    CURLMcode mc = curl_multi_perform(multi_handle_, &still_running_);
    if (mc != CURLM_OK) {
        std::cerr << "[CurlMultiManager] multi_perform failed: "
                  << curl_multi_strerror(mc) << std::endl;
        return;
    }

    // 等待网络事件
    int numfds = 0;
    mc = curl_multi_wait(multi_handle_, nullptr, 0, 1000, &numfds);
    if (mc != CURLM_OK) {
        std::cerr << "[CurlMultiManager] multi_wait failed: "
                  << curl_multi_strerror(mc) << std::endl;
        return;
    }

    // 检查已完成的请求
    checkCompletedTasks();
}

// 检查完成的请求，在锁外执行回调
void CurlMultiManager::checkCompletedTasks() {
    CURLMsg *msg;
    int msgs_left = 0;

    while ((msg = curl_multi_info_read(multi_handle_, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy = msg->easy_handle;
            CURLcode result = msg->data.result;
            CompletionCallback cb;

            {
                // 先上锁，移除 handle 并获取回调
                std::lock_guard<std::mutex> lock(mutex_);
                CURLMcode rc = curl_multi_remove_handle(multi_handle_, easy);
                if (rc != CURLM_OK) {
                    std::cerr << "[CurlMultiManager] remove_handle failed: "
                              << curl_multi_strerror(rc) << std::endl;
                }

                auto it = callbacks_.find(easy);
                if (it != callbacks_.end()) {
                    cb = std::move(it->second);
                    callbacks_.erase(it);
                }
                handles_.erase(easy);
            }

            // 在锁外执行回调
            if (cb) {
                cb(result, curl_easy_strerror(result));
            }
        }
    }
}
