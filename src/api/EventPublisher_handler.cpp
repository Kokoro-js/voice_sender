#include "EventPublisher.h"
#include "Request.pb.h"
#include "Response.pb.h"
#include <google/protobuf/util/time_util.h>

#include "handlers/Handlers.h"

using namespace OMNI::Instance;

// 处理请求/响应逻辑
void EventPublisher::handle_request_response() {
    try {
        zmq::message_t identity;
        zmq::message_t request;
        auto result_identity = router.recv(identity, zmq::recv_flags::dontwait);
        if (!result_identity) {
            return;
        }

        auto result_request = router.recv(request, zmq::recv_flags::none);
        if (!result_request) {
            return;
        }

        OMNI::Request req;
        req.ParsePartialFromArray(request.data(), request.size());

        if (!req.has_stream_request()) {
            return;
        }
        auto message = req.mutable_stream_request();
        auto res = Handlers::get_res(req.id(), message->info().stream_id());
        Handlers &handlers = Handlers::getInstance();
        switch (message->payload_case()) {
            case StreamRequest::kStartStreamPayload:
                handlers.startStreamHandler(message->mutable_start_stream_payload(), res);
                break;
            case StreamRequest::kRemoveStreamPayload:
                handlers.stopStreamHandler(message->mutable_remove_stream_payload(), res);
                break;
            case StreamRequest::kUpdateStreamPayload:
                handlers.updateStreamHandler(message->mutable_update_stream_payload(), res);
                handle_event_publish(res.stream_id(), false);
                break;
            case StreamRequest::kGetStreamPayload:
                handlers.getStreamHandler(message->mutable_get_stream_payload(), res);
                break;
            case StreamRequest::kGetPlayListPayload:
                handlers.getPlayListHandler(message->mutable_get_play_list_payload(), res);
                break;
            case StreamRequest::kUpdatePlayListPayload:
                handlers.updatePlayListHandler(message->mutable_update_play_list_payload(), res);
                break;
            default:
                res.set_code(OMNI::ERROR);
                res.set_message("Unknown request type.");
        }

        auto now = std::chrono::system_clock::now();
        int64_t milliseconds_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
        res.set_timestamp(milliseconds_since_epoch);

        size_t size = res.ByteSizeLong();
        std::vector<uint8_t> serialized_data(size);
        res.SerializeToArray(serialized_data.data(), size);

        zmq::message_t empty_frame;
        zmq::message_t messageToSend(serialized_data.data(), serialized_data.size());

        router.send(identity, zmq::send_flags::sndmore);
        // router.send(empty_frame, zmq::send_flags::sndmore);
        router.send(messageToSend, zmq::send_flags::none);
    } catch (const zmq::error_t &e) {
        LOG(ERROR) << "ZMQ Error: " << e.what();
    } catch (const std::exception &e) {
        LOG(ERROR) << "Standard Error: " << e.what();
    }
}

void EventPublisher::handle_event_publish(const std::string &stream_id, bool isPlayList) {
    OMNI::Request req;
    auto res = Handlers::get_res("", stream_id);
    Handlers &handlers = Handlers::getInstance();

    if (isPlayList) {
        auto payload = req.mutable_stream_request()->mutable_get_play_list_payload();
        handlers.getPlayListHandler(payload, res);
    } else {
        auto payload = req.mutable_stream_request()->mutable_get_stream_payload();
        handlers.getStreamHandler(payload, res);
    }

    auto now = std::chrono::system_clock::now();
    int64_t milliseconds_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    res.set_timestamp(milliseconds_since_epoch);

    publish_event(res);
}
