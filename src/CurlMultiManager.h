#ifndef CURL_MULTI_MANAGER_H
#define CURL_MULTI_MANAGER_H

#include <curl/curl.h>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <unordered_map>
#include <string>

class CurlMultiManager {
public:
    using CompletionCallback = std::function<void(CURLcode result, const std::string& error)>;

    // 获取 CurlMultiManager 的全局唯一实例
    static CurlMultiManager& getInstance();

    // 禁止拷贝和赋值
    CurlMultiManager(const CurlMultiManager&) = delete;
    CurlMultiManager& operator=(const CurlMultiManager&) = delete;

    // 添加任务
    void addTask(CURL* easy_handle, CompletionCallback callback = nullptr);

    // 停止管理器
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
    bool running_;
    int still_running_;
    std::thread worker_thread_;
};

#endif // CURL_MULTI_MANAGER_H
