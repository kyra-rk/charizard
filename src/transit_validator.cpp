#include "storage.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

// Allowed transit modes (kept local to this translation unit)
static const std::vector<std::string> kAllowedTransitModes = {"taxi", "car", "bus", "subway", "train", "bike", "walk"};

// Implement the constructor declared in include/storage.hpp.
// Validates the inputs and fills defaults (eg. timestamp -> now if 0).
TransitEvent::TransitEvent(const std::string& user_id_, const std::string& mode_, double distance_km_, std::int64_t ts_)
{
    // basic sanity checks
    if (user_id_.empty())
        throw std::runtime_error("user_id must not be empty.");

    if (distance_km_ < 0.0)
        throw std::runtime_error("Negative value for distance_km is not allowed.");

    if (std::find(kAllowedTransitModes.begin(), kAllowedTransitModes.end(), mode_) == kAllowedTransitModes.end())
        throw std::runtime_error("invalid mode");

    // fill members
    user_id = user_id_;
    mode = mode_;
    distance_km = distance_km_;

    if (ts_ == 0)
    {
        using clock = std::chrono::system_clock;
        ts = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count());
    }
    else
    {
        ts = ts_;
    }
}

