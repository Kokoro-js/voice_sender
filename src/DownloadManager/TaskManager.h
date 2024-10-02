#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "coro/event.hpp"
#ifndef TASKMANAGER_H
#define TASKMANAGER_H

enum TaskType {
    File,
    Cached,
    Stream
};

struct TaskItem {
    std::string name;
    std::string url;
    TaskType type;
};

enum ConsumerMode {
    FIFO,           // 先进先出，或称为顺序消费
    LIFO,           // 后进先出，通常用于栈结构
    RoundRobin,     // 循环消费，按顺序依次消费，完成后重新开始
    Random,         // 随机消费，随机选择元素进行消费
};

class TaskManager {
private:
    ConsumerMode mode;


public:
    explicit TaskManager(ConsumerMode mode = ConsumerMode::FIFO);
    void setMode(ConsumerMode newMode);
    [[nodiscard]] ConsumerMode getMode() const;

    size_t currentIndex = 0;
    std::vector<std::string> task_order;
    std::unordered_map<std::string, TaskItem> task_map;

    std::optional<TaskItem> findTask(const std::string& task);
    std::optional<TaskItem> getNextTask();
    coro::event TaskUpdateEvent;
    int addTask(const std::string& task, const TaskItem& task_item);
    int removeTask(const std::string& task);
    int removeTasks(const std::vector<std::string>& removeTaskList);
};

#endif //TASKMANAGER_H
