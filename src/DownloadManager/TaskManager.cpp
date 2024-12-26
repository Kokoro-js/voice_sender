// TaskManager.cpp
#include "TaskManager.h"
#include "../api/EventPublisher.h"
#include <algorithm>
#include <unordered_set>
#include <iostream>

// 构造函数
TaskManager::TaskManager(ConsumerMode mode)
        : mode(mode), currentIndex(0), rng(std::random_device{}()) {
}

// 设置消费者模式
void TaskManager::setMode(ConsumerMode newMode) {
    if (mode != newMode) {
        mode = newMode;
        currentIndex = 0; // 重置索引
        TaskUpdateEvent.set();
    }
}

// 获取消费者模式
ConsumerMode TaskManager::getMode() const {
    return mode;
}

// 添加任务
bool TaskManager::addTask(const TaskItem &task_item) {
    auto [it, inserted] = taskMap.emplace(task_item.name, task_item);
    if (!inserted) {
        return false; // 任务已存在
    }
    taskOrder.push_back(task_item.name);
    TaskUpdateEvent.set();
    return true;
}

// 移除任务
bool TaskManager::removeTask(const std::string &taskName) {
    auto it = taskMap.find(taskName);
    if (it == taskMap.end()) {
        return false; // 任务不存在
    }

    // 从 taskOrder 中移除所有出现的任务
    taskOrder.erase(std::remove(taskOrder.begin(), taskOrder.end(), taskName), taskOrder.end());

    // 从 taskMap 中移除任务
    taskMap.erase(it);

    // 调整 currentIndex
    if (currentIndex >= taskOrder.size()) {
        currentIndex = 0;
    }

    TaskUpdateEvent.set();
    return true;
}

// 绝对跳转到指定任务
bool TaskManager::skipTo(const std::string &taskName) {
    auto it = std::find(taskOrder.begin(), taskOrder.end(), taskName);
    if (it == taskOrder.end()) {
        return false; // 任务不存在
    }
    currentIndex = std::distance(taskOrder.begin(), it);
    TaskUpdateEvent.set();
    hasManualSkip = true;
    return true;
}

// 相对跳转
bool TaskManager::skipRelative(int offset) {
    if (taskOrder.empty()) {
        return false; // 无任务
    }

    /*if (mode == ConsumerMode::SingleLoop) {
        // 在单曲循环模式下，无论偏移量如何，都保持在当前歌曲
        TaskUpdateEvent.set();
        return true;
    }*/

    // 计算新的索引，支持循环
    int newIndex = static_cast<int>(currentIndex) + offset;

    if (mode == ConsumerMode::RoundRobin) {
        newIndex %= static_cast<int>(taskOrder.size());
        if (newIndex < 0) {
            newIndex += taskOrder.size();
        }
    } else {
        // 对于其他模式，限制在有效范围内
        if (newIndex < 0) {
            newIndex = 0;
        } else if (static_cast<size_t>(newIndex) >= taskOrder.size()) {
            newIndex = taskOrder.size() - 1;
        }
    }

    currentIndex = static_cast<size_t>(newIndex);
    TaskUpdateEvent.set();
    hasManualSkip = true;
    return true;
}

// 清空所有任务
void TaskManager::clearTasks() {
    taskMap.clear();
    taskOrder.clear();
    currentIndex = 0;
    TaskUpdateEvent.set();
}

// 查找任务
std::optional<TaskItem> TaskManager::findTask(const std::string &taskName) const {
    auto it = taskMap.find(taskName);
    if (it != taskMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

// 获取下一个任务
std::optional<TaskItem> TaskManager::getNextTask() {
    if (taskOrder.empty()) {
        return std::nullopt; // 无任务
    }

    if (hasManualSkip) {
        hasManualSkip = false;
        // return taskMap.at(taskOrder[currentIndex]);
    }

    std::optional<TaskItem> result = std::nullopt;

    switch (mode) {
        case ConsumerMode::FIFO:
            if (currentIndex < taskOrder.size()) {
                result = taskMap.at(taskOrder[currentIndex]);
                currentIndex++;
            }
            break;

        case ConsumerMode::LIFO:
            if (currentIndex > 0) {
                currentIndex--;
                result = taskMap.at(taskOrder[currentIndex]);
            }
            break;

        case ConsumerMode::RoundRobin:
            if (currentIndex >= taskOrder.size()) {
                currentIndex = 0;
            }
            if (!taskOrder.empty()) {
                result = taskMap.at(taskOrder[currentIndex]);
                currentIndex = (currentIndex + 1) % taskOrder.size();
            }
            break;

        case ConsumerMode::Random: {
            size_t index = getRandomIndex();
            if (index < taskOrder.size()) {
                result = taskMap.at(taskOrder[index]);
            }
            break;
        }

        case ConsumerMode::SingleLoop:
            if (currentIndex < taskOrder.size()) {
                result = taskMap.at(taskOrder[currentIndex]);
            }
            break;

        default:
            break;
    }

    return result;
}

// 生成随机索引
size_t TaskManager::getRandomIndex() const {
    std::lock_guard<std::mutex> lock(rngMutex);
    if (taskOrder.empty()) return 0;
    std::uniform_int_distribution<size_t> dist(0, taskOrder.size() - 1);
    return dist(rng);
}

// 批量更新任务
bool TaskManager::updateTasks(const std::vector<TaskItem> &newTasks, const std::vector<std::string> &newOrder) {
    // 1. 添加或更新任务
    for (const auto &task: newTasks) {
        taskMap[task.name] = task;
    }

    // 2. 验证 newOrder 中的所有任务名称在 taskMap 中存在
    for (const auto &taskName: newOrder) {
        if (taskMap.find(taskName) == taskMap.end()) {
            // newOrder 中包含不存在的任务名称
            std::cerr << "Error: Task \"" << taskName << "\" in newOrder does not exist in taskMap.\n";
            return false;
        }
    }

    // 3. 确定要移除的任务（即不在 newOrder 中出现的任务）
    std::unordered_set<std::string> orderTaskSet(newOrder.begin(), newOrder.end());
    std::vector<std::string> tasksToRemove;
    for (const auto &[name, _]: taskMap) {
        if (orderTaskSet.find(name) == orderTaskSet.end()) {
            tasksToRemove.push_back(name);
        }
    }

    // 4. 移除不在 newOrder 中的任务
    for (const auto &taskName: tasksToRemove) {
        taskMap.erase(taskName);
    }

    // 5. 更新 taskOrder
    taskOrder = newOrder;

    // 6. 调整 currentIndex 如果超出范围
    if (currentIndex >= taskOrder.size()) {
        currentIndex = 0;
    }

    // 7. 统一触发事件
    TaskUpdateEvent.set();
    EventPublisher::getInstance().handle_event_publish(stream_id_, true);
    return true;
}
