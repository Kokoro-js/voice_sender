#ifndef CURL_MULTI_MANAGER_H
#define CURL_MULTI_MANAGER_H

#include <curl/curl.h>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <functional>
#include <unordered_map>

class CurlMultiManager {
public:
    using CompletionCallback = std::function<void(CURLcode result, const std::string& response)>;

    // 获取CurlMultiManager的全局唯一实例
    static CurlMultiManager& getInstance();

    CurlMultiManager(const CurlMultiManager&) = delete;
    CurlMultiManager& operator=(const CurlMultiManager&) = delete;

    void addTask(CURL* easy_handle, CompletionCallback callback = nullptr);
    void stop();

private:
    CurlMultiManager();
    ~CurlMultiManager();

    void run();
    void processMultiPerform();
    void checkCompletedTasks();

    CURLM* multi_handle_;
    std::unordered_map<CURL*, CompletionCallback> callbacks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
    std::atomic<int> still_running_;
    std::thread worker_thread_;
};

#endif // CURL_MULTI_MANAGER_H