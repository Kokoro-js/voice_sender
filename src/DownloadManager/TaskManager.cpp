#include "TaskManager.h"
#include "../api/EventPublisher.h"
#include <algorithm>
#include <unordered_set>
#include <iostream>

TaskManager::TaskManager(ConsumerMode mode)
        : mode(mode),
          currentIndex(0),
          rng(std::random_device{}()) {
}

void TaskManager::setMode(ConsumerMode newMode) {
    std::lock_guard<std::mutex> lock(mtx);
    if (mode != newMode) {
        mode = newMode;
        // 是否重置 currentIndex 看需求
        // currentIndex = 0;
        TaskUpdateEvent.set();
    }
}

ConsumerMode TaskManager::getMode() const {
    std::lock_guard<std::mutex> lock(mtx);
    return mode;
}

// 添加任务
bool TaskManager::addTask(const TaskItem &task_item) {
    std::lock_guard<std::mutex> lock(mtx);
    auto [it, inserted] = taskMap.emplace(task_item.name, task_item);
    if (!inserted) {
        return false; // 已有重名任务
    }
    taskOrder.push_back(task_item.name);
    TaskUpdateEvent.set();
    return true;
}

// 移除任务
bool TaskManager::removeTask(const std::string &taskName) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = taskMap.find(taskName);
    if (it == taskMap.end()) {
        return false;
    }
    taskOrder.erase(std::remove(taskOrder.begin(), taskOrder.end(), taskName),
                    taskOrder.end());
    taskMap.erase(it);

    if (currentIndex >= taskOrder.size()) {
        currentIndex = 0;
    }
    TaskUpdateEvent.set();
    return true;
}

// 绝对跳转到指定任务
bool TaskManager::skipTo(const std::string &taskName) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = std::find(taskOrder.begin(), taskOrder.end(), taskName);
    if (it == taskOrder.end()) {
        return false; // 不存在
    }
    currentIndex = static_cast<size_t>(std::distance(taskOrder.begin(), it));
    TaskUpdateEvent.set();
    hasManualSkip = true;
    return true;
}

// 相对跳转 offset
bool TaskManager::skipRelative(int offset) {
    std::lock_guard<std::mutex> lock(mtx);
    if (taskOrder.empty()) {
        return false;
    }

    int newIndex = static_cast<int>(currentIndex) + offset;

    if (mode == ConsumerMode::RoundRobin) {
        newIndex %= static_cast<int>(taskOrder.size());
        if (newIndex < 0) {
            newIndex += taskOrder.size();
        }
    } else {
        // 非 RoundRobin 下越界就钳制到边界
        if (newIndex < 0) {
            newIndex = 0;
        } else if (newIndex >= (int) taskOrder.size()) {
            newIndex = (int) taskOrder.size() - 1;
        }
    }

    currentIndex = static_cast<size_t>(newIndex);
    TaskUpdateEvent.set();
    hasManualSkip = true;
    return true;
}

// **自动切下一首**（或上一首 / 随机）
// 常用于"播放完毕后"或"自动循环"等场景
void TaskManager::autoNext() {
    std::lock_guard<std::mutex> lock(mtx);
    if (taskOrder.empty()) return;

    switch (mode) {
        case ConsumerMode::FIFO: {
            // 往后移动一首
            if (currentIndex + 1 < taskOrder.size()) {
                currentIndex++;
            } else {
                // FIFO 到尾部就停，也可重置为 0，看业务需求
                currentIndex = taskOrder.size() - 1;
            }
            break;
        }
        case ConsumerMode::LIFO: {
            // 往前移动一首
            if (currentIndex > 0) {
                currentIndex--;
            } else {
                // LIFO 到头就停，也可回到末尾
                currentIndex = 0;
            }
            break;
        }
        case ConsumerMode::RoundRobin: {
            currentIndex = (currentIndex + 1) % taskOrder.size();
            break;
        }
        case ConsumerMode::Random: {
            size_t r = getRandomIndex();
            currentIndex = r;
            break;
        }
        case ConsumerMode::SingleLoop: {
            // 单曲循环: 不动 currentIndex
            break;
        }
    }
    TaskUpdateEvent.set();
}

// 清空
void TaskManager::clearTasks() {
    std::lock_guard<std::mutex> lock(mtx);
    taskMap.clear();
    taskOrder.clear();
    currentIndex = 0;
    TaskUpdateEvent.set();
}

// 批量更新
bool TaskManager::updateTasks(const std::vector<TaskItem> &newTasks,
                              const std::vector<std::string> &newOrder) {
    std::lock_guard<std::mutex> lock(mtx);

    for (auto &task: newTasks) {
        taskMap[task.name] = task; // 更新或新增
    }

    // 验证 newOrder
    for (auto &name: newOrder) {
        if (taskMap.find(name) == taskMap.end()) {
            std::cerr << "Error: Task \"" << name
                      << "\" not in taskMap.\n";
            return false;
        }
    }

    // 移除多余
    std::unordered_set<std::string> s(newOrder.begin(), newOrder.end());
    std::vector<std::string> toRemove;
    for (auto &kv: taskMap) {
        if (s.find(kv.first) == s.end()) {
            toRemove.push_back(kv.first);
        }
    }
    for (auto &rm: toRemove) {
        taskMap.erase(rm);
    }

    // 更新顺序
    taskOrder = newOrder;
    if (currentIndex >= taskOrder.size()) {
        currentIndex = 0;
    }

    TaskUpdateEvent.set();
    EventPublisher::getInstance().handle_event_publish(stream_id_, true);
    return true;
}

// **不移动索引**，只返回当前索引指向的任务
std::optional<TaskItem> TaskManager::getNextTask() const {
    std::lock_guard<std::mutex> lock(mtx);
    if (taskOrder.empty() || currentIndex >= taskOrder.size()) {
        return std::nullopt;
    }
    return taskMap.at(taskOrder[currentIndex]);
}

// 查找
std::optional<TaskItem> TaskManager::findTask(const std::string &taskName) const {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = taskMap.find(taskName);
    if (it != taskMap.end()) {
        return it->second;
    }
    return std::nullopt;
}

// 随机索引
size_t TaskManager::getRandomIndex() const {
    std::lock_guard<std::mutex> lock(rngMutex);
    if (taskOrder.empty()) return 0;
    std::uniform_int_distribution<size_t> dist(0, taskOrder.size() - 1);
    return dist(rng);
}
