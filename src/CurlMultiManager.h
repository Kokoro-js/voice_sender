#ifndef CURL_MULTI_MANAGER_H
#define CURL_MULTI_MANAGER_H

#include <curl/curl.h>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>

/**
 * @brief 管理多个 CURL easy handle 的单例类
 *        使用 std::shared_ptr<CURL> 确保句柄安全
 */
class CurlMultiManager {
public:
    using CompletionCallback = std::function<void(CURLcode, const std::string &)>;

    static CurlMultiManager &getInstance();

    // 添加任务：将共享指针和回调注册到 multi_handle
    void addTask(std::shared_ptr<CURL> easy_handle, CompletionCallback callback = nullptr);

    // 取消任务：根据裸指针从 multi_handle 中移除
    void cancelTask(CURL *easy_handle);

    // 停止管理器并清理所有句柄
    void stop();

    CurlMultiManager(const CurlMultiManager &) = delete;

    CurlMultiManager &operator=(const CurlMultiManager &) = delete;

private:
    CurlMultiManager();

    ~CurlMultiManager();

    void run();

    void processMultiPerform();

    void checkCompletedTasks();

private:
    CURLM *multi_handle_;  // libcurl multi 句柄
    bool running_;
    int still_running_;

    // 保存回调与 shared_ptr 映射
    std::unordered_map<CURL *, CompletionCallback> callbacks_;
    std::unordered_map<CURL *, std::shared_ptr<CURL>> handles_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_thread_;
};

#endif // CURL_MULTI_MANAGER_H
