#pragma once

#include "storage.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

// Parse/validate a transit POST body and return a validated TransitEvent.
// Throws std::runtime_error on validation errors.
TransitEvent make_transit_event_from_json(const std::string& user_id,
                                          const nlohmann::json& body,
                                          std::int64_t now_epoch = 0);
