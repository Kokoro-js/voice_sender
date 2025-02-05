#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <random>
#include <mutex>
#include "coro/event.hpp"

enum class TaskType {
    File,
    Cached,
};

struct TaskItem {
    std::string name;
    std::string url;
    TaskType type;
    bool use_stream = false;
};

enum class ConsumerMode {
    FIFO,
    LIFO,
    RoundRobin,
    Random,
    SingleLoop
};

class TaskManager {
public:
    explicit TaskManager(ConsumerMode mode = ConsumerMode::FIFO);

    std::string stream_id_;

    void setMode(ConsumerMode newMode);

    ConsumerMode getMode() const;

    bool addTask(const TaskItem &task_item);

    bool removeTask(const std::string &taskName);

    // 跳转到指定任务 / 相对跳转
    bool skipTo(const std::string &taskName);

    bool skipRelative(int offset);

    // 给“自动下一首”或“自动上一首”用的，根据 mode 来移动
    // 例如 FIFO => currentIndex++， LIFO => currentIndex--
    // RoundRobin => (currentIndex+1)%size, Random => 随机选, SingleLoop => 不动
    void autoNext();

    // 清空
    void clearTasks();

    // 批量更新任务
    bool updateTasks(const std::vector<TaskItem> &newTasks,
                     const std::vector<std::string> &newOrder);

    // **只返回当前索引指向的任务，不移动索引**
    std::optional<TaskItem> getNextTask() const;

    // 查找
    std::optional<TaskItem> findTask(const std::string &taskName) const;

    // 事件
    coro::event TaskUpdateEvent;

    const std::vector<std::string> &getTaskOrder() const {
        return taskOrder;
    }

    bool hasManualSkip = false;

private:
    ConsumerMode mode;
    std::vector<std::string> taskOrder;
    std::unordered_map<std::string, TaskItem> taskMap;

    size_t currentIndex;

    mutable std::mt19937 rng;
    mutable std::mutex rngMutex;
    mutable std::mutex mtx;

    // 生成随机索引
    size_t getRandomIndex() const;
};
