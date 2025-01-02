#include "Handlers.h"
#include "Base.pb.h"

void Handlers::updateStreamHandler(const Instance::UpdateStreamPayload *data, OMNI::Response &res) {
    auto streamId = res.stream_id();
    auto targetOpt = findById(streamId);
    if (!targetOpt.has_value()) {
        res.set_code(OMNI::NOT_FOUND);
        res.set_message("UpdateStream: 未找到对应 ID 的流");
        return;
    }
    auto target = targetOpt.value();

    auto audio_sender = target->get_audio_sender();
    auto using_decoder = audio_sender->using_decoder;
    auto &props = audio_sender->audio_props;

    // 处理不同的 payload 类型
    try {
        switch (data->action_case()) {
            case OMNI::Instance::UpdateStreamPayload::kSeekPayload: {
                using_decoder->seek(data->seek_payload().second());
                props.current_samples = using_decoder->getCurrentSamples();
                props.do_empty_ring_buffer = true;
            };
                break;
            case OMNI::Instance::UpdateStreamPayload::kSkipPayload: {
                auto next = data->skip_payload().next();
                if (!next.empty()) {
                    if (!target->skipTo(next)) {
                        res.set_code(OMNI::ERROR);
                        res.set_message("无法跳跃到任务 " + next);
                        return;
                    }
                }
                auto offset = data->skip_payload().offset();
                if (offset != 0) {
                    if (!target->skipRelative(offset)) {
                        res.set_code(OMNI::ERROR);
                        res.set_message(&"无法相对跳跃到任务 "[offset]);
                        return;
                    }
                }
                audio_sender->doSkip();
            };
                break;
            case OMNI::Instance::UpdateStreamPayload::kSwitchPlayStatePayload: {
                auto state = data->switch_play_state_payload().play_state();
                audio_sender->switchPlayState(static_cast<::PlayState>(state));
            };
                break;
            case OMNI::Instance::UpdateStreamPayload::kSwitchPlayModePayload: {
                auto mode = data->switch_play_mode_payload().play_mode();
                target->setMode(static_cast<::ConsumerMode>(mode));
            };
                break;
            case OMNI::Instance::UpdateStreamPayload::kSetVolumePayload: {
                auto volume = data->set_volume_payload().volume();
                audio_sender->setVolume(volume);
            };
                break;
        }
    } catch (const std::exception &e) {
        res.set_code(OMNI::ERROR);
        res.set_message(std::string("处理请求时发生错误: ") + e.what());
        return;
    }

/*
    // 获取指向 GetStreamResponse 的指针，避免手动分配内存
    GetStreamResponse *res_data = res.mutable_get_stream_response();

    // 获取指向 OrderItem 的指针，并设置其字段
    OrderItem *msg_item = res_data->mutable_current_play();
    msg_item->set_task_id(task->item.name);
    msg_item->set_type(Task2OrderType(task->item.type));
    msg_item->set_url(task->item.url);

    // 设置时间字段和状态
    res_data->set_time_played(static_cast<int32_t>((props.current_samples * 1000L) / props.rate));
    res_data->set_time_total(static_cast<int32_t>((props.total_samples * 1000L) / props.rate));
    res_data->set_state(static_cast<OMNI::PlayState>(props.play_state));
*/

    return;
}
