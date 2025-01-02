#include "Handlers.h"
#include "Base.pb.h"

void Handlers::getStreamHandler(const Instance::GetStreamPayload *data, OMNI::Response &res) {
    auto streamId = res.stream_id();
    auto targetOpt = findById(streamId);
    if (!targetOpt.has_value()) {
        res.set_code(OMNI::NOT_FOUND);
        res.set_message("GetStream: 未找到对应 ID 的流");
        return;
    }
    auto target = targetOpt.value();

    auto props = target->get_audio_sender()->audio_props;
    auto task = target->get_audio_sender()->task;
    if (!task) {
        // 当解码出错或队列为空时可以为空，千万不要写 NOT_FOUND
        res.set_code(OMNI::SUCCESS);
        res.set_message("该流存在但目前没有任务噢。");
        return;
    }

    // 使用 Protobuf 的 mutable 方法来避免手动分配内存
    GetStreamResponse *res_data = res.mutable_get_stream_response();
    res_data->set_stream_id(streamId);

    // 设置 current_play
    OrderItem *msg_item = res_data->mutable_current_play();
    msg_item->set_task_id(task->item.name);
    msg_item->set_type(Task2OrderType(task->item.type));
    msg_item->set_url(task->item.url);

    // 设置时间字段
    res_data->set_time_played(static_cast<int32_t>((props.current_samples * 1000L) / props.rate));
    res_data->set_time_total(static_cast<int32_t>((props.total_samples * 1000L) / props.rate));
    res_data->set_play_state(static_cast<OMNI::PlayState>(props.play_state));
    res_data->set_volume(props.volume);
    res_data->set_play_mode(static_cast<OMNI::ConsumerMode>(target->getMode()));
}
