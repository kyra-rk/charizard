#include "transit_logic.hpp"

#include <ctime>
#include <stdexcept>

TransitEvent make_transit_event_from_json(const std::string& user_id, const nlohmann::json& body,
                                          std::int64_t now_epoch)
{
    if (user_id.empty())
        throw std::runtime_error("user_id must not be empty.");

    if (!body.contains("mode") || !body.contains("distance_km"))
        throw std::runtime_error("missing_fields");

    const std::string  mode     = body["mode"].get<std::string>();
    const double       distance = body["distance_km"].get<double>();
    const std::int64_t ts =
        body.value("ts", now_epoch == 0 ? static_cast<std::int64_t>(std::time(nullptr)) : now_epoch);

    // Delegate validation to the existing TransitEvent ctor in transit_validator.cpp
    return TransitEvent(user_id, mode, distance, ts);
}
