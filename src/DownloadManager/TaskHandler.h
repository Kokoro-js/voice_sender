// TaskHandler.h
#pragma once

#include "coro/coro.hpp"
#include <memory>
#include <utility>

using RemoveCallback = std::function<void(const std::string&)>;
class TaskHandler {
public:
    explicit TaskHandler(std::shared_ptr<coro::thread_pool> tp)
        : tp_(std::move(tp)), task_container_(tp_) {}

    virtual ~TaskHandler() = default;

    virtual coro::task<void> initAndWaitJobs() = 0;
    virtual void cleanupJob() = 0;

    std::string id_;  // 用于标识自己在 map 中的 key
    void setRemoveCallback(RemoveCallback callback, const std::string& id) {
        removeCallback_ = std::move(callback);
        id_ = id;
    };

protected:
    std::shared_ptr<coro::thread_pool> tp_;

    coro::task_container<coro::thread_pool> task_container_;
    bool isStopped = false;

    RemoveCallback removeCallback_;
};
