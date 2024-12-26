#ifndef EVENTPUBLISHER_H
#define EVENTPUBLISHER_H

#include <zmq.hpp>
#include <string>
#include "Response.pb.h"

class EventPublisher {
public:
    static EventPublisher &getInstance();

    void publish_event(const std::string &event_message);

    void publish_event(const OMNI::Response &response);

    void handle_request_response();

    void handle_event_publish(const std::string &stream_id, bool isPlayList);

private:
    EventPublisher();

    ~EventPublisher();

    EventPublisher(const EventPublisher &) = delete;

    EventPublisher &operator=(const EventPublisher &) = delete;

    void initialize();

    void initialize_publisher();

    void initialize_responder();

    zmq::context_t context_;
    zmq::socket_t publisher_;
    zmq::socket_t router;
    std::string publisher_bind_address_;
    std::string responder_bind_address_;
    bool initialized_;
};

#endif // EVENTPUBLISHER_H