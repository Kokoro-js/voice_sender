#ifndef CHANNEL_JOINED_DATA_H
#define CHANNEL_JOINED_DATA_H

#include <string>
// #include "Poco/JSON/Object.h"

struct ChannelJoinedData {
    std::string ip;
    int port;
    int rtcp_port;
    int audio_ssrc;
    int audio_pt;
    int bitrate;
    bool rtcp_mux;
};

// ChannelJoinedData parseJoinedJsonResponse(const Poco::JSON::Object::Ptr& jsonObj);

#endif // CHANNEL_JOINED_DATA_H
