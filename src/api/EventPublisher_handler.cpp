#include <plog/Log.h>

#include "EventPublisher.h"
#include "Request.pb.h"
#include "Response.pb.h"

#include "handlers/Handlers.h"

// 处理请求/响应逻辑
void EventPublisher::handle_request_response() {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_initialized();

        zmq::message_t request;
        responder_.recv(request, zmq::recv_flags::none);

        OMNI::Request message;
        OMNI::Response response;

        // 使用 FlatBuffers 解析请求数据
        message.ParsePartialFromArray(request.data(), request.size());

        // 处理请求数据，以下是简单的打印，实际应用可以做更复杂的处理
        switch (message.payload_case()) {
            case OMNI::Request::kStartStreamPayload: {
                auto payload = message.release_start_stream_payload();
                Handlers::getInstance().startStreamHandler(payload);
            }; break;
            case OMNI::Request::kRemoveStreamPayload: {
                auto payload = message.release_remove_stream_payload();
                Handlers::getInstance().stopStreamHandler(payload);
            }; break;
            case OMNI::Request::kUpdateStreamPayload: {
                auto payload = message.release_update_stream_payload();
            }; break;
            case OMNI::Request::kGetStreamPayload: {
                auto payload = message.release_get_stream_payload();
            }; break;
            default:
                response.set_code(OMNI::ERROR);
            response.set_message("未找到路径。");
        }

        response.set_code(OMNI::ERROR);
        response.set_message("未找到路径。");

        size_t size = response.ByteSizeLong();
        std::vector<uint8_t> serialized_data(size);
        response.SerializeToArray(serialized_data.data(), size);

        zmq::message_t messageToSend(serialized_data.data(), serialized_data.size());
        responder_.send(messageToSend, zmq::send_flags::none);
        LOGI << "Sent Response";
    } catch (const zmq::error_t& e) {
        LOGE << "ZMQ Error: " << e.what();
    } catch (const std::exception& e) {
        LOGE << "Standard Error: " << e.what();
    }
}
