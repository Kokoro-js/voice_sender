/*
#include "ChannelJoinedData.h"
#include "Poco/JSON/Object.h"
#include <stdexcept>

ChannelJoinedData parseJoinedJsonResponse(const Poco::JSON::Object::Ptr& jsonObj) {
    if (!jsonObj) throw std::invalid_argument("Invalid JSON object");

    Poco::JSON::Object::Ptr data = jsonObj->getObject("data");
    if (!data) throw std::invalid_argument("Data field missing in JSON response");

    ChannelJoinedData result;
    result.ip = data->optValue<std::string>("ip", "");  // optValue does not throw if the key does not exist
    result.port = data->optValue<int>("port", 0);
    result.rtcp_port = data->optValue<int>("rtcp_port", 0);
    result.audio_ssrc = data->optValue<int>("audio_ssrc", 0);
    result.audio_pt = data->optValue<int>("audio_pt", 0);
    result.bitrate = data->optValue<int>("bitrate", 0);
    result.rtcp_mux = data->optValue<bool>("rtcp_mux", false);

    return result;
}
*/
