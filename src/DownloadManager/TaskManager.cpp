#include "TaskManager.h"
#include <algorithm>
#include <ranges>
#include <unordered_set>

TaskManager::TaskManager(ConsumerMode mode) : mode(mode) {
    // 初始化其他成员变量或做一些初始化操作
}

// setMode 方法实现
void TaskManager::setMode(ConsumerMode newMode) {
    mode = newMode;
}

// getMode 方法实现
ConsumerMode TaskManager::getMode() const {
    return mode;
}

std::optional<TaskItem> TaskManager::findTask(const std::string& task) {
    auto it = task_map.find(task);
    if (it == task_map.end()) {
        return {};
    }
    return it->second;
}

int TaskManager::addTask(const std::string& task, const TaskItem& task_item) {
    // 检查任务是否已存在
    auto it = task_map.find(task);
    if (it == task_map.end()) {
        task_map[task] = task_item;
        task_order.push_back(task);  // 添加任务到顺序列表中
        TaskUpdateEvent.set();       // 触发任务更新事件
        return 1;                    // 返回1表示任务成功添加
    }
    task_order.push_back(task);
    return 0;  // 如果任务已存在，返回0
}

int TaskManager::removeTask(const std::string& task) {
    const auto it = findTask(task);
    if (it.has_value() == false) {
        return -1;
    }
    task_map.erase(task);
    // 使用 ranges::remove 重新排列向量
    auto newEnd = std::ranges::remove(task_order, it.value().name);
    // 使用 erase 删除新逻辑末尾到实际末尾的元素
    task_order.erase(newEnd.begin(), task_order.end());
    TaskUpdateEvent.set();
    return 1;
}

int TaskManager::removeTasks(const std::vector<std::string>& removeTaskList) {
    std::unordered_set<std::string> targets = {};
    for (const auto& task : removeTaskList) {
        auto it = findTask(task);
        if (it.has_value() == false) {
            continue;
        }
        targets.insert(it.value().name);
    }
    task_order.erase(std::remove_if(task_order.begin(), task_order.end(),
                         [&targets](const std::string& item) {
                             return targets.contains(item);
                         }),
          task_order.end());
    TaskUpdateEvent.set();
    return targets.size();
}

std::optional<TaskItem> TaskManager::getNextTask() {
    if (task_order.empty()) {
        return std::nullopt;  // 确保任务列表不为空
    }

    std::optional<TaskItem> result = std::nullopt;
    switch (mode) {
        case ConsumerMode::FIFO:
            if (currentIndex < task_order.size()) {
                result = task_map[task_order[currentIndex]];
                currentIndex++;  // 仅在有效索引时递增
            }
        break;
        case ConsumerMode::LIFO:
            if (currentIndex > 0) {
                currentIndex--;
                result = task_map[task_order[currentIndex]];
            }
        break;
        case ConsumerMode::RoundRobin:
            result = task_map[task_order[currentIndex]];
        currentIndex = (currentIndex + 1) % task_order.size();
        break;
        case ConsumerMode::Random:
            result = task_map[task_order[rand() % task_order.size()]];
        break;
        default:
            break;
    }

    return result;
}
