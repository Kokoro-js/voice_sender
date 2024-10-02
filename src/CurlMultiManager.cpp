#include "CurlMultiManager.h"
#include <iostream>

CurlMultiManager& CurlMultiManager::getInstance() {
    static CurlMultiManager instance;
    return instance;
}

CurlMultiManager::CurlMultiManager() : running_(true), still_running_(0) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    multi_handle_ = curl_multi_init();
    worker_thread_ = std::thread(&CurlMultiManager::run, this);
}

CurlMultiManager::~CurlMultiManager() {
    stop();
    curl_multi_cleanup(multi_handle_);
    curl_global_cleanup();
}

void CurlMultiManager::addTask(CURL* easy_handle, CompletionCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        curl_multi_add_handle(multi_handle_, easy_handle);
        callbacks_[easy_handle] = callback;
        still_running_++;
    } // 释放锁更早些，减少锁持有时间
    cv_.notify_one(); // 唤醒在等待任务的线程
}

void CurlMultiManager::stop() {
    running_ = false;
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void CurlMultiManager::run() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (still_running_ == 0) {
            cv_.wait_for(lock, std::chrono::milliseconds(200), [this] {
                return still_running_ > 0 || !running_;
            });
        }
        lock.unlock();

        if (running_) {
            processMultiPerform();
        }
    }
}

void CurlMultiManager::processMultiPerform() {
    int running_handles;
    CURLMcode result = curl_multi_perform(multi_handle_, &running_handles);

    if (result != CURLM_OK) {
        std::cerr << "Error during curl_multi_perform: " << curl_multi_strerror(result) << std::endl;
    }

    still_running_ = running_handles;

    if (running_handles > 0) {
        int numfds;
        curl_multi_wait(multi_handle_, nullptr, 0, 1000, &numfds);
    }

    checkCompletedTasks();
}

void CurlMultiManager::checkCompletedTasks() {
    int msgs_left;
    CURLMsg* msg;
    while ((msg = curl_multi_info_read(multi_handle_, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL* handle = msg->easy_handle;
            curl_multi_remove_handle(multi_handle_, handle);

            auto it = callbacks_.find(handle);
            if (it != callbacks_.end()) {
                it->second(msg->data.result, curl_easy_strerror(msg->data.result));
                callbacks_.erase(it);
            }

            // curl_easy_cleanup(handle);
            still_running_--;
        }
    }
}
