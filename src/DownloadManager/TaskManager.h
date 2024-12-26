// TaskManager.h
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <random>
#include <mutex>
#include "coro/event.hpp"

// 定义任务类型枚举
enum class TaskType {
    File,
    Cached,
};

// 定义 TaskItem 结构体
struct TaskItem {
    std::string name;
    std::string url;
    TaskType type;
    bool use_stream = false;
};

// 定义消费者模式枚举
enum class ConsumerMode {
    FIFO,          // 先进先出，或称为顺序消费
    LIFO,          // 后进先出，通常用于栈结构
    RoundRobin,    // 循环消费，按顺序依次消费，完成后重新开始
    Random,        // 随机消费，随机选择元素进行消费
    SingleLoop     // 单曲循环，只循环当前歌曲
};

// 定义 TaskManager 类
class TaskManager {
public:
    explicit TaskManager(ConsumerMode mode = ConsumerMode::FIFO);

    std::string stream_id_;

    // 设置和获取消费者模式
    void setMode(ConsumerMode newMode);

    ConsumerMode getMode() const;

    // 任务管理
    bool addTask(const TaskItem &task_item);

    bool removeTask(const std::string &taskName);

    // 跳转到指定任务（绝对跳转）或相对跳转
    bool skipTo(const std::string &taskName);

    bool skipRelative(int offset); // 新增相对跳转方法

    void clearTasks();

    // 批量更新任务
    bool updateTasks(const std::vector<TaskItem> &newTasks, const std::vector<std::string> &newOrder);

    // 获取任务
    std::optional<TaskItem> getNextTask();

    std::optional<TaskItem> findTask(const std::string &taskName) const;

    // 事件
    coro::event TaskUpdateEvent;

    // 公开任务顺序以便用户端显示（只读访问）
    const std::vector<std::string> &getTaskOrder() const { return taskOrder; }

private:
    ConsumerMode mode;
    bool hasManualSkip = false;
    std::vector<std::string> taskOrder;
    std::unordered_map<std::string, TaskItem> taskMap;

    size_t currentIndex;

    // 随机数生成器
    mutable std::mt19937 rng;
    mutable std::mutex rngMutex; // 保护 rng 的线程安全

    // 生成随机索引
    size_t getRandomIndex() const;
};
