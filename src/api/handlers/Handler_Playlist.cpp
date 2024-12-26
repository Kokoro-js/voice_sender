#include "Handlers.h"
#include "Base.pb.h"
#include <stdexcept> // 用于 std::exception

void Handlers::getPlayListHandler(const Instance::GetPlayListPayload *data, OMNI::Response &res) {
    auto streamId = res.stream_id();
    auto targetOpt = findById(streamId);

    if (!targetOpt.has_value()) {
        res.set_code(OMNI::NOT_FOUND);
        res.set_message("GetPlayList: 未找到对应 ID 的流");
        return;
    }

    auto target = targetOpt.value();

    // 获取指向 PlayListResponse 的指针，避免手动分配内存
    PlayListResponse *play_list = res.mutable_play_list_response();
    play_list->set_stream_id(streamId);

    // 获取任务顺序并添加到 PlayListResponse 中
    try {
        auto order = target->getTaskOrder();
        play_list->mutable_order_list()->Add(order.begin(), order.end());
    } catch (const std::exception &e) {
        res.set_code(OMNI::ERROR);
        res.set_message(std::string("获取任务顺序时发生错误: ") + e.what());
        return;
    }
}

void Handlers::updatePlayListHandler(const Instance::UpdatePlayListPayload *data, OMNI::Response &res) {
    auto streamId = res.stream_id();
    auto targetOpt = findById(streamId);
    if (!targetOpt.has_value()) {
        res.set_code(OMNI::NOT_FOUND);
        res.set_message("UpdatePlayList: 未找到对应 ID 的流");
        return;
    }
    auto target = targetOpt.value();

    std::vector<TaskItem> newTasks;
    std::vector<std::string> newOrder;


    // 解析并更新任务列表
    for (const auto &order: data->order_list()) {
        LOG(INFO) << order.url();
        TaskItem task{
                .name = order.task_id(),
                .url = order.url(),
                .type = static_cast<TaskType>(order.type()),
                .use_stream = order.use_stream()
        };
        newTasks.push_back(task);
        newOrder.push_back(task.name);
    }

    target->updateTasks(newTasks, newOrder);

    // 获取指向 PlayListResponse 的指针，避免手动分配内存
    PlayListResponse *play_list = res.mutable_play_list_response();
    play_list->set_stream_id(streamId);

    // 获取更新后的任务顺序并添加到 PlayListResponse 中
    auto updatedOrder = target->getTaskOrder();
    play_list->mutable_order_list()->Add(updatedOrder.begin(), updatedOrder.end());
}
